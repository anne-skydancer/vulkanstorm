// Genuine native texture-drop producer used by the shared Face and world tools.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "lltooldraganddrop.h"
#include "vsfacedroppolicy.h"
#include "vsmaterialselection.h"
#include "llagent.h"
#include "llfloatertools.h"
#include "llgltfmateriallist.h"
#include "llviewermenu.h"
#include "llviewerstats.h"
#include "llsdutil.h"
#if VS_NATIVE_VULKAN
#include "vsmaterialassets.h"
#include "llviewerwindow.h"
#include "vsvulkancontext.h"

namespace
{
struct FaceTextureDrop
{
    S32 side=0;
    VSFaceTextureDrop kind=VSFaceTextureDrop::Diffuse;
    LLUUID base;
    LLTextureEntry source;
    LLPointer<LLGLTFMaterial> override;
    LLMaterialPtr legacy;
    std::shared_ptr<VSMaterialAssets::PreparedPut> put;
};

bool sameDropSource(const VSMaterialObject& object,const FaceTextureDrop& operation)
{
    const auto* entry=object.getTE(operation.side);
    if(!entry || object.getRenderMaterialID(operation.side)!=operation.base || *entry!=operation.source)return false;
    const auto* a=entry->getGLTFMaterialOverride();const auto* b=operation.source.getGLTFMaterialOverride();
    if(bool(a)!=bool(b) || (a && *a!=*b))return false;
    const auto& l=entry->getMaterialParams();const auto& r=operation.source.getMaterialParams();
    return bool(l)==bool(r) && (!l || llsd_equals(l->asLLSD(),r->asLLSD()));
}

bool prepareFaceTextureDrop(const VSTaskInventoryObject& task,const VSMaterialObject& object,
    FaceTextureDrop& operation,const LLUUID& asset,LLGLTFMaterial::TextureInfo channel)
{
    const auto* entry=object.getTE(operation.side);if(!entry)return false;
    operation.source=*entry;operation.base=object.getRenderMaterialID(operation.side);
    // Snapshot mutable materials so an unrelated edit cannot mutate our origin.
    if(auto* value=entry->getGLTFMaterialOverride())operation.source.setGLTFMaterialOverride(new LLGLTFMaterial(*value));
    if(entry->getMaterialParams().notNull())operation.source.setMaterialParams(new LLMaterial(*entry->getMaterialParams()));
    if(operation.kind==VSFaceTextureDrop::PBROverride)
    {
        if(operation.base.isNull() || task.getCapability("ModifyMaterialParams").empty())return false;
        if(task.requiresInventoryFetch()){task.requestInventory();return false;}
        const auto* item=object.getInventoryItemByAsset(operation.base);
        if(item && !item->getPermissions().allowModifyBy(gAgentID))return false;
        operation.override=entry->getGLTFMaterialOverride()?new LLGLTFMaterial(*entry->getGLTFMaterialOverride()):new LLGLTFMaterial();
        switch(channel)
        {
        case LLGLTFMaterial::GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS:operation.override->setOcclusionRoughnessMetallicId(asset,true);break;
        case LLGLTFMaterial::GLTF_TEXTURE_INFO_EMISSIVE:operation.override->setEmissiveId(asset,true);break;
        case LLGLTFMaterial::GLTF_TEXTURE_INFO_NORMAL:operation.override->setNormalId(asset,true);break;
        default:operation.override->setBaseColorId(asset,true);break;
        }
    }
    else if(operation.kind==VSFaceTextureDrop::Normal || operation.kind==VSFaceTextureDrop::Specular)
    {
        if(!gFloaterTools)return false;
        operation.legacy=gFloaterTools->createDefaultMaterial(entry->getMaterialParams());
        if(operation.legacy.isNull())return false;
        if(operation.kind==VSFaceTextureDrop::Normal)operation.legacy->setNormalID(asset);
        else operation.legacy->setSpecularID(asset);
        auto* assets=gViewerWindow->nativeContext()->world().legacyMaterialAssets(task.getHost());
        if(!assets)return false;
        operation.put=assets->preparePut(task.nativeObject(),U8(operation.side),*operation.legacy);
        if(!operation.put)return false;
    }
    return bool(task);
}

bool publishFaceTextureDrops(const VSTaskInventoryObject& task,LLInventoryItem* sourceItem,
    LLToolDragAndDrop::ESource source,const LLUUID& sourceID,bool removePBR,
    std::vector<FaceTextureDrop>& operations,bool protect,bool sendTE)
{
    if(!task || !sourceItem || operations.empty())return false;
    // Protection may synchronously delete the no-copy inventory item and notify UI.
    LLPointer<LLInventoryItem> item(sourceItem);
    const LLUUID asset=item->getAssetUUID();
    const auto current=task.objectGuard();if(!current || !current())return false;
    auto object=VSMaterialObject::find(task.nativeObject());if(!object)return false;
    for(const auto& operation:operations)if(!sameDropSource(object,operation))return false;
    if(removePBR && task.getCapability("ModifyMaterialParams").empty())return false;
    if(protect && !LLToolDragAndDrop::handleDropMaterialProtections(task,item,source,sourceID))return false;
    if(!current() || item->getAssetUUID()!=asset)return false;
    // Inventory observers can replace a face/source during the no-copy transfer.
    for(const auto& operation:operations)if(!sameDropSource(object,operation))return false;
    if(sendTE)add(LLStatViewer::EDIT_TEXTURE,1);
    for(const auto& operation:operations)
    {
        if(!current() || !sameDropSource(object,operation))return false;
        auto& world=gViewerWindow->nativeContext()->world();
        if(removePBR)
        {
            // Same base-material reset as LLViewerObject::setRenderMaterialID:
            // preserve texture transforms, discard the remaining override fields.
            world.editTextureEntry(task.nativeObject(),U32(operation.side),[](LLTextureEntry& value)
            {value.setBaseMaterial();value.setGLTFRenderMaterial(nullptr);return TEM_CHANGE_TEXTURE;});
            if(!world.previewMaterialID(task.nativeObject(),U32(operation.side),LLUUID::null))return false;
            LLGLTFMaterialList::queueApply(task.getID(),operation.side,LLUUID::null,nullptr,current,task.getCapability("ModifyMaterialParams"));
            if(!current())return false;
        }
        if(operation.kind==VSFaceTextureDrop::PBROverride)
        {
            world.editTextureEntry(task.nativeObject(),U32(operation.side),[&](LLTextureEntry& value)
            {return value.setGLTFMaterialOverride(operation.override);});
            object.queueModify(operation.side,operation.override);
        }
        else if(operation.legacy.notNull())
        {
            auto* assets=world.legacyMaterialAssets(task.getHost());
            // The charged PUT was accepted before transferring any inventory or
            // publishing local material state; only retirement can invalidate it.
            if(!assets || !assets->put(operation.put))return false;
            object.setTEMaterialParams(U8(operation.side),operation.legacy);
        }
        else object.setTEImage(U8(operation.side),asset);
        if(operation.kind!=VSFaceTextureDrop::PBROverride)dialog_refresh_all();
    }
    if(!current())return false;
    LLGLTFMaterialList::flushUpdates(nullptr);
    if(sendTE && current())object.sendTEUpdate();
    return current();
}
}
#endif

bool LLToolDragAndDrop::dropTextureOneFace(const VSTaskInventoryObject& task,S32 side,
    LLInventoryItem* item,ESource source,const LLUUID& sourceID,bool removePBR,S32 channel)
{
    if(!task || !item || side<0)return false;
    if(!task.native())
    {dropTextureOneFace(task.legacy(),side,item,source,sourceID,removePBR,channel);return true;}
#if VS_NATIVE_VULKAN
    const auto current=task.objectGuard();
    auto object=VSMaterialObject::find(task.nativeObject());
    if(!current || !current() || !object || !object.permModify() || object.task().isPermanentEnforced() || side>=object.getNumTEs())return false;
    const auto& permissions=item->getPermissions();
    const bool visible=gFloaterTools && gFloaterTools->getVisible();
    const auto kind=vs_face_texture_drop(object.getRenderMaterialID(side).notNull(),removePBR,channel,
        visible,visible?gFloaterTools->getTextureDropChannel():LLRender::NUM_TEXTURE_CHANNELS,
        LLRender::NUM_TEXTURE_CHANNELS,permissions.allowOperationBy(PERM_COPY,gAgent.getID()),
        permissions.allowOperationBy(PERM_TRANSFER,gAgent.getID()));
    if(kind==VSFaceTextureDrop::Refuse)return false;
    auto pbrChannel=LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR;
    if(visible && gFloaterTools->getPBRDropChannel()!=LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT)
        pbrChannel=gFloaterTools->getPBRDropChannel();
    auto residency=gViewerWindow->nativeContext()->world().reserveSpatialBytes(sizeof(FaceTextureDrop)+2*sizeof(LLGLTFMaterial)+2*sizeof(LLMaterial));
    if(!residency)return false;
    std::vector<FaceTextureDrop> operations(1);operations.front().side=side;operations.front().kind=kind;
    if(!prepareFaceTextureDrop(task,object,operations.front(),item->getAssetUUID(),pbrChannel) || !current())return false;
    return publishFaceTextureDrops(task,item,source,sourceID,removePBR,operations,
        kind!=VSFaceTextureDrop::PBROverride,kind!=VSFaceTextureDrop::PBROverride);
#else
    return false;
#endif
}

bool LLToolDragAndDrop::dropTextureAllFaces(const VSTaskInventoryObject& task,LLInventoryItem* item,
    ESource source,const LLUUID& sourceID,bool removePBR)
{
    if(!task || !item)return false;
    if(!task.native()){dropTextureAllFaces(task.legacy(),item,source,sourceID,removePBR);return true;}
#if VS_NATIVE_VULKAN
    const auto current=task.objectGuard();auto object=VSMaterialObject::find(task.nativeObject());
    if(!current || !current() || !object || !object.permModify() || object.task().isPermanentEnforced())return false;
    const auto& permissions=item->getPermissions();
    const bool overrideAllowed=permissions.allowOperationBy(PERM_COPY,gAgent.getID()) &&
        permissions.allowOperationBy(PERM_TRANSFER,gAgent.getID());
    const S32 count=object.getNumTEs();if(count<=0 || count>45)return false;
    auto residency=gViewerWindow->nativeContext()->world().reserveSpatialBytes(std::size_t(count)*(sizeof(FaceTextureDrop)+2*sizeof(LLGLTFMaterial)+2*sizeof(LLMaterial)));
    if(!residency)return false;
    bool protect=removePBR;std::vector<FaceTextureDrop> operations;operations.reserve(count);
    for(S32 side=0;side<object.getNumTEs();++side)
    {
        const bool pbr=object.getRenderMaterialID(side).notNull();protect=protect || !pbr;
        if(pbr && !removePBR && !overrideAllowed)continue;
        FaceTextureDrop operation;operation.side=side;
        operation.kind=pbr && !removePBR?VSFaceTextureDrop::PBROverride:VSFaceTextureDrop::Diffuse;
        if(!prepareFaceTextureDrop(task,object,operation,item->getAssetUUID(),LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR) || !current())return false;
        operations.emplace_back(std::move(operation));
    }
    return publishFaceTextureDrops(task,item,source,sourceID,removePBR,operations,protect,true);
#else
    return false;
#endif
}
