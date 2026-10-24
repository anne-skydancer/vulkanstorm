// Actual simulator/selection material owners for the shared editor visitors.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsmaterialselection.h"
#include "vsfaceimagemetadata.h"
#include "llagent.h"
#include "message.h"
#include "llgltfmateriallist.h"
#include "llinventorymodel.h"
#include "llviewertexturelist.h"
#include "llviewerobject.h"
#include "llviewertexture.h"
#include "llvovolume.h"
#include "llviewerwindow.h"
#include "llviewernetwork.h"
#include "vsnativeim.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#endif

VSMaterialObject::VSMaterialObject(LLViewerObject* object)
{
    if (object) mObject = VSTaskInventoryObject::find(object->getID());
}
VSMaterialObject VSMaterialObject::find(const LLUUID& id)
{
    VSMaterialObject result;
    result.mObject = VSTaskInventoryObject::find(id);
    return result;
}
bool VSMaterialObject::permCopy() const
{
    if (!*this) return false;
    if (!mObject.native()) return mObject.legacy()->permCopy();
#ifdef HACKED_GODLIKE_VIEWER
    return true;
#else
# ifdef TOGGLE_HACKED_GODLIKE_VIEWER
    if (LLGridManager::instance().isInSLBeta() && gAgent.getGodLevel() >= GOD_MAINTENANCE) return true;
# endif
    const auto root = mObject.getRootEdit();
    return root && (root.objectFlags() & FLAGS_OBJECT_COPY) != 0;
#endif
}
bool VSMaterialObject::getVolume() const
{
    return *this && (mObject.native() ? mObject.getPCode() == LL_PCODE_VOLUME : mObject.legacy()->getVolume() != nullptr);
}
bool VSMaterialObject::permTransfer() const
{
    if (!*this) return false;
    if (!mObject.native()) return mObject.legacy()->permTransfer();
#ifdef HACKED_GODLIKE_VIEWER
    return true;
#else
# ifdef TOGGLE_HACKED_GODLIKE_VIEWER
    if (LLGridManager::instance().isInSLBeta() && gAgent.getGodLevel() >= GOD_MAINTENANCE) return true;
# endif
    const auto root = mObject.getRootEdit();
    return root && (root.objectFlags() & FLAGS_OBJECT_TRANSFER) != 0;
#endif
}
S32 VSMaterialObject::getNumTEs() const
{
    if (!*this) return 0;
#if VS_NATIVE_VULKAN
    if (mObject.native()) return gViewerWindow->nativeContext()->world().textureEntryCount(mObject.nativeObject());
#endif
    return mObject.legacy()->getNumTEs();
}
S32 VSMaterialObject::getNumFaces() const
{
    if (!*this) return 0;
#if VS_NATIVE_VULKAN
    if (mObject.native())
        return S32(gViewerWindow->nativeContext()->world().faceCount(mObject.nativeObject()).value_or(0));
#endif
    return mObject.legacy()->getNumFaces();
}
LLUUID VSMaterialObject::getRenderMaterialID(S32 side) const
{
    if (!*this || side < 0 || side >= getNumTEs()) return {};
#if VS_NATIVE_VULKAN
    if (mObject.native()) return gViewerWindow->nativeContext()->world().renderMaterialID(mObject.nativeObject(), U32(side));
#endif
    return mObject.legacy()->getRenderMaterialID(side);
}
LLTextureEntry* VSMaterialObject::getTE(S32 side) const
{
    if (!*this || side < 0 || side >= getNumTEs()) return nullptr;
#if VS_NATIVE_VULKAN
    if (mObject.native())
    {
        return gViewerWindow->nativeContext()->world().materialEntry(mObject.nativeObject(), U32(side));
    }
#endif
    return mObject.legacy()->getTE(side);
}
LLViewerInventoryItem* VSMaterialObject::getInventoryItemByAsset(const LLUUID& asset) const
{
    if (!*this) return nullptr;
    if (!mObject.native()) return mObject.legacy()->getInventoryItemByAsset(asset);
    LLInventoryObject::object_list_t contents;
    mObject.getInventoryContents(contents);
    for (const auto& entry : contents)
        if (auto* item = dynamic_cast<LLViewerInventoryItem*>(entry.get()); item && item->getAssetUUID() == asset)
            return item;
    return nullptr;
}
VSMaterialObject VSMaterialObject::getRootEdit() const
{
    const auto root = mObject.getRootEdit();
    return root ? find(root.getID()) : VSMaterialObject{};
}
std::optional<LLPermissions> VSMaterialObject::permissions() const
{
    if (!*this) return {};
#if VS_NATIVE_VULKAN
    if (mObject.native())
    {
        const auto owner = VSNativeSession::active();
        const auto* selection = owner ? owner->existingObjectSelection() : nullptr;
        const auto publication = selection ? selection->findProperties(mObject.nativeObject()) : std::nullopt;
        return publication ? std::optional<LLPermissions>(publication->properties->permissions()) : std::nullopt;
    }
#endif
    const auto* value = LLSelectMgr::instance().findObjectPermissions(mObject.legacy());
    return value ? std::optional<LLPermissions>(*value) : std::nullopt;
}
void VSMaterialObject::previewMaterial(S32 side, const LLUUID& id) const
{
    if (!*this || !permModify() || !getVolume()) return;
#if VS_NATIVE_VULKAN
    if (mObject.native())
    {
        gViewerWindow->nativeContext()->world().previewMaterialID(mObject.nativeObject(), U32(side), id);
        return;
    }
#endif
    auto* object = static_cast<LLVOVolume*>(mObject.legacy());
    object->setRenderMaterialID(side, id, false);
    object->updateTEMaterialTextures(side);
}
void VSMaterialObject::queueModify(S32 side, const LLGLTFMaterial* material) const
{
    if (!*this) return;
#if VS_NATIVE_VULKAN
    if (mObject.native())
    {
        if (getRenderMaterialID(side).notNull())
            LLGLTFMaterialList::queueModify(getID(), side, material, mObject.objectGuard(),
                mObject.getCapability("ModifyMaterialParams"));
        return;
    }
#endif
    LLGLTFMaterialList::queueModify(mObject.legacy(), side, material);
}
VSMaterialNode::VSMaterialNode(LLSelectNode* node)
    : mSavedGLTFOverrideMaterials(node->mSavedGLTFOverrideMaterials), mObject(node->getObject()), mLegacy(node)
{}
#if VS_NATIVE_VULKAN
VSMaterialNode::VSMaterialNode(std::shared_ptr<VSObjectSelection> selection,
    const VSWorldScene::ObjectHandle& token, std::shared_ptr<VSObjectSelection::SavedMaterials> saved)
    : mSavedGLTFOverrideMaterials(saved->overrides), mObject(VSMaterialObject::find(token)),
      mSelection(std::move(selection)), mToken(token), mSaved(std::move(saved))
{}
#endif
bool VSMaterialNode::isTESelected(S32 side) const
{
#if VS_NATIVE_VULKAN
    if (mSelection) return mSelection->contains(mToken, side);
#endif
    return mLegacy && mLegacy->isTESelected(side);
}
VSMaterialSelection::VSMaterialSelection()
{
#if VS_NATIVE_VULKAN
    if (gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        mNative = true;
        const auto owner = VSNativeSession::active();
        if (owner) mSelection = owner->retainObjectSelection();
        return;
    }
#endif
    mLegacy = LLSelectMgr::instance().getSelection();
}
bool VSMaterialSelection::isEmpty() const { return getObjectCount() == 0; }
S32 VSMaterialSelection::getObjectCount() const
{
#if VS_NATIVE_VULKAN
    if (mNative) return mSelection ? mSelection->objectCount() : 0;
#endif
    return mLegacy->getObjectCount();
}
VSMaterialObject VSMaterialSelection::getFirstObject() const
{
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        const auto values = mSelection ? mSelection->selected() : std::vector<VSWorldScene::ObjectHandle>{};
        return values.empty() ? VSMaterialObject{} : VSMaterialObject::find(values.front());
    }
#endif
    return VSMaterialObject(mLegacy->getFirstObject());
}
boost::signals2::connection VSMaterialSelection::changed(const std::function<void()>& callback) const
{
#if VS_NATIVE_VULKAN
    if (mNative) return mSelection ? mSelection->changed(callback) : boost::signals2::connection{};
#endif
    return LLSelectMgr::instance().mUpdateSignal.connect(callback);
}
std::function<bool()> VSMaterialSelection::guard() const
{
#if VS_NATIVE_VULKAN
    if (mNative) return mSelection ? mSelection->selectionGuard() : std::function<bool()>([] { return false; });
#endif
    return [] { return true; };
}
boost::signals2::connection VSMaterialSelection::materialsChanged(const std::function<void(const LLUUID&, S32)>& callback) const
{
#if VS_NATIVE_VULKAN
    if (mNative && mSelection) return mSelection->materialsChanged(callback);
#endif
    return {};
}
bool VSMaterialSelection::materialsPending() const
{
#if VS_NATIVE_VULKAN
    if (mNative && mSelection)
        for (const auto& token : mSelection->selected())
        {
            auto object = VSMaterialObject::find(token);
            for (S32 side = 0; object && side < object->getNumTEs(); ++side)
                if (mSelection->contains(token, side) && object->getRenderMaterialID(side).notNull())
                {
                    auto* base = gGLTFMaterialList.getMaterial(object->getRenderMaterialID(side));
                    if (!base || base->isFetching()) return true;
                }
        }
#endif
    return false;
}


LLVector3 VSMaterialObject::getScale() const
{
    if(!*this)return LLVector3::zero;
#if VS_NATIVE_VULKAN
    if(mObject.native())
    {
        const auto transform=gViewerWindow->nativeContext()->world().objectLocalTransform(mObject.nativeObject());
        return transform?transform->scale:LLVector3::zero;
    }
#endif
    return mObject.legacy()->getScale();
}
void VSMaterialObject::sendTEUpdate() const
{
    if(!*this)return;
#if VS_NATIVE_VULKAN
    if(mObject.native())
    {
        LLMessageSystem* message=gMessageSystem;if(!message)return;
        message->newMessage("ObjectImage");message->nextBlock("AgentData");
        message->addUUID("AgentID",gAgent.getID());message->addUUID("SessionID",gAgent.getSessionID());
        message->nextBlock("ObjectData");message->addU32("ObjectLocalID",mObject.getLocalID());
        if(gViewerWindow->nativeContext()->world().packTextureEntries(mObject.nativeObject(),*message))message->sendReliable(mObject.getHost());
        return;
    }
#endif
    mObject.legacy()->sendTEUpdate();
}

S32 VSMaterialObject::setTEColor(U8 side,const LLColor4& color) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setColor(color);});
#endif
    return mObject.legacy()->setTEColor(side,color);
}

S32 VSMaterialObject::setTEFullbright(U8 side,U8 value) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setFullbright(value);});
#endif
    return mObject.legacy()->setTEFullbright(side,value);
}

S32 VSMaterialObject::setTEGlow(U8 side,F32 value) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setGlow(value);});
#endif
    return mObject.legacy()->setTEGlow(side,value);
}

S32 VSMaterialObject::setTEScale(U8 side,F32 s,F32 t) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setScale(s,t);});
#endif
    return mObject.legacy()->setTEScale(side,s,t);
}

S32 VSMaterialObject::setTEScaleS(U8 side,F32 value) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setScaleS(value);});
#endif
    return mObject.legacy()->setTEScaleS(side,value);
}

S32 VSMaterialObject::setTEScaleT(U8 side,F32 value) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setScaleT(value);});
#endif
    return mObject.legacy()->setTEScaleT(side,value);
}

S32 VSMaterialObject::setTEOffset(U8 side,F32 s,F32 t) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setOffset(s,t);});
#endif
    return mObject.legacy()->setTEOffset(side,s,t);
}

S32 VSMaterialObject::setTEOffsetS(U8 side,F32 value) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setOffsetS(value);});
#endif
    return mObject.legacy()->setTEOffsetS(side,value);
}

S32 VSMaterialObject::setTEOffsetT(U8 side,F32 value) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setOffsetT(value);});
#endif
    return mObject.legacy()->setTEOffsetT(side,value);
}

S32 VSMaterialObject::setTERotation(U8 side,F32 value) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setRotation(value);});
#endif
    return mObject.legacy()->setTERotation(side,value);
}

S32 VSMaterialObject::setTEBumpmap(U8 side,U8 value) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setBumpmap(value);});
#endif
    return mObject.legacy()->setTEBumpmap(side,value);
}

S32 VSMaterialObject::setTEBumpShiny(U8 side,U8 value) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setBumpShiny(value);});
#endif
    return mObject.legacy()->setTEBumpShiny(side,value);
}

S32 VSMaterialObject::setTEBumpShinyFullbright(U8 side,U8 value) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setBumpShinyFullbright(value);});
#endif
    return mObject.legacy()->setTEBumpShinyFullbright(side,value);
}

S32 VSMaterialObject::setTETexGen(U8 side,U8 value) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setTexGen(value);});
#endif
    return mObject.legacy()->setTETexGen(side,value);
}

const LLMediaEntry* VSMaterialObject::mediaData(U8 side) const
{
    if(!*this)return nullptr;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().mediaEntry(mObject.nativeObject(),side);
#endif
    const auto* entry=getTE(side);return entry?entry->getMediaData():nullptr;
}
bool VSMaterialObject::mediaDataReady() const
{
    if(!*this)return false;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().mediaDataReady(mObject.nativeObject());
#endif
    return true;
}
bool VSMaterialObject::syncMediaData(U8 side,const LLSD& data,bool merge,bool ignoreAgent,bool updatePlayer) const
{
    if(!*this || !permModify())return false;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().syncMediaData(mObject.nativeObject(),side,data,merge,ignoreAgent,updatePlayer);
#endif
    auto* object=dynamic_cast<LLVOVolume*>(mObject.legacy());
    if(!object)return false;
    if(updatePlayer)object->syncMediaData(side,data,merge,ignoreAgent);
    else if(auto* entry=object->getTE(side)){if(merge)entry->mergeIntoMediaData(data);else entry->updateMediaData(data);}
    else return false;
    return bool(*this);
}
bool VSMaterialObject::sendMediaDataUpdate() const
{
    if(!*this || !permModify())return false;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().sendMediaDataUpdate(mObject.nativeObject());
#endif
    auto* object=dynamic_cast<LLVOVolume*>(mObject.legacy());
    if(!object)return false;
    object->sendMediaDataUpdate();return bool(*this);
}
S32 VSMaterialObject::setTEMediaFlags(U8 side,U8 value) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())
    {
        auto& world=gViewerWindow->nativeContext()->world();const auto token=mObject.nativeObject();
        const auto* entry=getTE(side);if(!entry)return TEM_CHANGE_NONE;
        if((value&LLTextureEntry::MF_HAS_MEDIA) && !entry->hasMedia() && !world.prepareMediaData(token))return TEM_CHANGE_NONE;
        if(!(value&LLTextureEntry::MF_HAS_MEDIA) && entry->hasMedia() && mediaDataReady() &&
            !world.syncMediaData(token,side,LLSD(),false,true))return TEM_CHANGE_NONE;
        return world.editTextureEntry(token,side,[&](LLTextureEntry& current){return current.setMediaFlags(value);});
    }
#endif
    return mObject.legacy()->setTEMediaFlags(side,value);
}

S32 VSMaterialObject::setTEShiny(U8 side,U8 value) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setShiny(value);});
#endif
    return mObject.legacy()->setTEShiny(side,value);
}

S32 VSMaterialObject::setTEImage(U8 side,const LLUUID& id) const
{
    if(!*this)return TEM_CHANGE_NONE;
#if VS_NATIVE_VULKAN
    if(mObject.native())return gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){return entry.setID(id);});
#endif
    mObject.legacy()->setTEImage(side,LLViewerTextureManager::getFetchedTexture(id));return TEM_CHANGE_TEXTURE;
}

void VSMaterialObject::setTEMaterialParams(U8 side,LLMaterialPtr material) const
{
    if(!*this)return;
#if VS_NATIVE_VULKAN
    if(mObject.native())
    {
        gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),side,[&](LLTextureEntry& entry){entry.setMaterialParams(material);return TEM_CHANGE_TEXTURE;});
        return;
    }
#endif
    mObject.legacy()->setTEMaterialParams(side,material);
}

#if VS_NATIVE_VULKAN
VSMaterialObject VSMaterialObject::find(const VSWorldScene::ObjectHandle& token)
{
    VSMaterialObject result;
    result.mObject = VSTaskInventoryObject::find(token);
    return result;
}
#endif

bool VSMaterialObject::queueApply(S32 side,const LLUUID& id,const LLGLTFMaterial* overrideMaterial) const
{
    if(!*this)return false;
#if VS_NATIVE_VULKAN
    if(mObject.native())
    {
        const auto current=mObject.objectGuard();const auto cap=mObject.getCapability("ModifyMaterialParams");
        if(!current || !current() || cap.empty())return false;
        LLGLTFMaterialList::queueApply(getID(),side,id,overrideMaterial,current,cap);
        return current();
    }
#endif
    LLGLTFMaterialList::queueApply(mObject.legacy(),side,id,overrideMaterial);return true;
}
bool VSMaterialObject::setGLTFMaterialOverride(S32 side,const LLGLTFMaterial* value) const
{
    if(!*this || side<0 || side>=getNumTEs())return false;
    LLPointer<LLGLTFMaterial> copy=value ? new LLGLTFMaterial(*value) : nullptr;
#if VS_NATIVE_VULKAN
    if(mObject.native())
    {
        const auto current=mObject.objectGuard();if(!current || !current())return false;
        const auto mutation=gViewerWindow->nativeContext()->world().editTextureEntry(mObject.nativeObject(),U32(side),
            [copy](LLTextureEntry& entry){entry.setGLTFMaterialOverride(copy);entry.setGLTFRenderMaterial(nullptr);return TEM_CHANGE_TEXTURE;});
        return mutation!=TEM_CHANGE_NONE && current();
    }
#endif
    mObject.legacy()->setTEGLTFMaterialOverride(U8(side),copy);return true;
}
bool VSMaterialObject::setRenderMaterialID(S32 side,const LLUUID& id,bool updateServer,bool localOrigin) const
{
    if(!*this)return false;
#if VS_NATIVE_VULKAN
    if(mObject.native())
    {
        if(updateServer && (!localOrigin || mObject.getCapability("ModifyMaterialParams").empty()))return false;
        auto& world=gViewerWindow->nativeContext()->world();
        const auto current=mObject.objectGuard();if(!current || !current())return false;
        const bool hiding=world.legacyMaterialPreview(mObject.nativeObject());
        const LLUUID effective=hiding && id.notNull()?LLUUID::null:id;
        const S32 begin=side==-1 ? 0 : llmax(side,0);
        const S32 end=side==-1 ? getNumTEs() : llmin(side+1,getNumTEs());
        for(S32 face=begin;face<end;++face)
        {
            if(!current())return false;
            const bool changed=!localOrigin || getRenderMaterialID(face)!=effective;
            if(updateServer || (!hiding && changed))
            {
                const auto mutation=world.editTextureEntry(mObject.nativeObject(),U32(face),[updateServer,hiding](LLTextureEntry& entry)
                {
                    if(updateServer)entry.setBaseMaterial();
                    if(!hiding)entry.setGLTFRenderMaterial(nullptr);
                    return TEM_CHANGE_TEXTURE;
                });
                if(mutation==TEM_CHANGE_NONE || !current())return false;
            }
            if(!world.previewMaterialID(mObject.nativeObject(),U32(face),effective))return false;
            if(updateServer)
            {
                const auto* entry=getTE(face);
                if(!queueApply(face,effective,entry ? entry->getGLTFMaterialOverride() : nullptr))return false;
            }
        }
        return current();
    }
#endif
    mObject.legacy()->setRenderMaterialID(side,id,updateServer,localOrigin);return true;
}
bool VSMaterialObject::clearTEWaterExclusion(U8 side) const
{
    if(!*this || !permModify())return false;
    if(!mObject.native()){mObject.legacy()->clearTEWaterExclusion(side);return true;}
    const auto* entry=getTE(side);if(!entry)return false;
    if(entry->getID()!=IMG_ALPHA_GRAD)return true;
    if(setTEImage(side,DEFAULT_OBJECT_TEXTURE)==TEM_CHANGE_NONE)return false;
    U32 s,t;if(!LLPrimitive::getTESTAxes(side,&s,&t))return true;
    const auto scale=getScale();setTEScale(side,scale.mV[s]*2.f,scale.mV[t]*2.f);
    if(!*this)return false;
    sendTEUpdate();return bool(*this);
}

// Same primary-format policy as LLViewerObject; normalized upload RGBA is not
// source alpha metadata, and unknown decoder format is not invented.
bool VSMaterialObject::isImageAlphaBlended(U8 side) const
{
    if(!mObject)return false;
    if(mObject.legacy())return mObject.legacy()->isImageAlphaBlended(side);
#if VS_NATIVE_VULKAN
    const auto image=vs_face_image(this,side);
    const auto format=image?image->getPrimaryFormat():GL_RGB;
    return format==GL_RGBA || format==GL_ALPHA;
#else
    return false;
#endif
}
