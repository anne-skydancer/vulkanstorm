// Real reference/native inspection peers for the existing Inspect controller. LGPL-2.1.
#pragma once
#include "llselectmgr.h"
#include "llvovolume.h"
#include "llgltfmaterial.h"
#include "llmaterial.h"
#include "vsnativeim.h"
#include <memory>
#include <vector>
#if VS_NATIVE_VULKAN
#include "vseditselection.h"
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#include "llviewerwindow.h"
#include "vsuiimageprovider.h"
#endif
class VSInspectionObject
{
    LLPointer<LLViewerObject> mLegacy;
#if VS_NATIVE_VULKAN
    std::unique_ptr<VSEditObject> mNative;
    std::optional<VSWorldScene::ObjectMetrics> mMetrics;
#endif
public:
    explicit VSInspectionObject(LLViewerObject* object):mLegacy(object){}
#if VS_NATIVE_VULKAN
    explicit VSInspectionObject(const VSWorldScene::ObjectHandle& token):mNative(std::make_unique<VSEditObject>(token))
    { if (*mNative && gViewerWindow && gViewerWindow->nativeContext()) mMetrics=gViewerWindow->nativeContext()->world().objectMetrics(token); }
    VSEditObject* native()const{return mNative && *mNative ? mNative.get() : nullptr;}
#endif
    LLViewerObject* legacy()const{return mLegacy.get();}
    LLUUID getID()const{
#if VS_NATIVE_VULKAN
        if(mNative)return mNative->getID();
#endif
        return mLegacy ? mLegacy->getID() : LLUUID::null;
    }
    bool isRoot()const{
#if VS_NATIVE_VULKAN
        if(mNative)return mNative->isRoot();
#endif
        return mLegacy && mLegacy->isRoot();
    }
    bool isRootEdit()const{
#if VS_NATIVE_VULKAN
        if(mNative)return mNative->isRootEdit();
#endif
        return mLegacy && mLegacy->isRootEdit();
    }
    S32 getNumFaces()const{
#if VS_NATIVE_VULKAN
        if(mNative)return mMetrics ? S32(mMetrics->faces.value_or(0)) : 0;
#endif
        return mLegacy ? mLegacy->getNumFaces() : 0;
    }
    S32 getNumVisibleFaces()const{
#if VS_NATIVE_VULKAN
        if(mNative)return mMetrics ? S32(mMetrics->publishedFaces) : 0;
#endif
        return mLegacy ? mLegacy->getNumVisibleFaces() : 0;
    }
    S32 getNumVertices()const{
#if VS_NATIVE_VULKAN
        if(mNative)return mMetrics ? S32(mMetrics->vertices.value_or(0)) : 0;
#endif
        return mLegacy ? mLegacy->getNumVertices() : 0;
    }
    S32 getNumIndices()const{
#if VS_NATIVE_VULKAN
        if(mNative)return mMetrics ? S32(mMetrics->triangles.value_or(0)*3) : 0;
#endif
        return mLegacy ? mLegacy->getNumIndices() : 0;
    }
    F32 linksetRenderCost()const{
#if VS_NATIVE_VULKAN
        if(mNative)return mMetrics && gViewerWindow && gViewerWindow->nativeContext() ?
            gViewerWindow->nativeContext()->world().objectLinksetRenderCost(mMetrics->object).value_or(0) : 0;
#endif
        auto* volume=dynamic_cast<LLVOVolume*>(mLegacy.get());
        if(!volume)return 0;
        LLVOVolume::texture_cost_t textures;
        F32 cost=F32(volume->getRenderCost(textures));
        for(const auto& child:volume->getChildren())
            if(auto* linked=dynamic_cast<LLVOVolume*>(child.get()))cost+=linked->getRenderCost(textures);
        for(const auto& texture:textures)cost+=LLVOVolume::getTextureCost(texture);
        return cost;
    }
    void fingerprint(std::vector<std::string>& out)const{
        out.push_back(getID().asString());
        for(S32 n:{getNumFaces(),getNumVisibleFaces(),getNumVertices(),getNumIndices()})out.push_back(std::to_string(n));
#if VS_NATIVE_VULKAN
        if(auto* object=native())
            for(S32 face=0;face<object->getNumTEs();++face)
                if(const auto* entry=object->getTE(face))
                {
                    const auto add=[&](const LLUUID& id){out.push_back(id.asString());out.push_back(std::to_string(VSUIRenderer::imageDescription(vs_ui_image_snapshot(id)).generation));};
                    if(const auto* material=entry->getGLTFRenderMaterial())for(const auto& id:material->mTextureId)add(id);
                    else{add(entry->getID());if(entry->getMaterialParams().notNull()){add(entry->getMaterialParams()->getNormalID());add(entry->getMaterialParams()->getSpecularID());}}
                }
#endif
    }
};
struct VSInspectionNode
{
    std::unique_ptr<VSInspectionObject> object;
    LLPermissions permissions;
    const LLPermissions* mPermissions=&permissions;
    std::string mName,mDescription;
    U64 mCreationDate=0;
    bool valid=false;
    VSInspectionObject* getObject()const{return object.get();}
    explicit VSInspectionNode(LLSelectNode* node):object(std::make_unique<VSInspectionObject>(node->getObject())),permissions(*node->mPermissions),mName(node->mName),mDescription(node->mDescription),mCreationDate(node->mCreationDate),valid(node->mValid){}
#if VS_NATIVE_VULKAN
    VSInspectionNode(const std::shared_ptr<VSObjectSelection>& selection,const VSWorldScene::ObjectHandle& token):object(std::make_unique<VSInspectionObject>(token))
    {
        if(const auto value=selection->properties(token);value && value->properties)
        {const auto& data=*value->properties;permissions=data.permissions();mName=data.name;mDescription=data.desc;mCreationDate=data.creation_date;valid=data.complete;}
    }
#endif
};
class VSInspectionSelection
{
    LLObjectSelectionHandle mLegacy;
    std::vector<std::unique_ptr<VSInspectionNode>>mStorage;
    std::vector<VSInspectionNode*>mNodes,mRoots;
    S32 mObjectCount=0,mRootCount=0;
    std::function<bool()>mCurrent=vs_native_im_guard();
public:
    VSInspectionSelection()
    {
#if VS_NATIVE_VULKAN
        if(const auto owner=VSNativeSession::active())
        {
            const auto selection=owner->retainObjectSelection();
            if(!selection)return;
            mCurrent=selection->selectionGuard();
            mObjectCount=S32(selection->objectCount());mRootCount=S32(selection->rootObjectCount());
            for(const auto& token:selection->selected())
            {
                auto node=std::make_unique<VSInspectionNode>(selection,token);
                if(node->valid){mNodes.push_back(node.get());if(node->getObject()->isRootEdit() && !selection->node(token)->individual)mRoots.push_back(node.get());}
                mStorage.push_back(std::move(node));
            }
            return;
        }
#endif
        mLegacy=LLSelectMgr::instance().getSelection();
        mObjectCount=mLegacy->getObjectCount();mRootCount=mLegacy->getRootObjectCount();
        for(auto it=mLegacy->valid_begin();it!=mLegacy->valid_end();++it)
        {auto node=std::make_unique<VSInspectionNode>(*it);mNodes.push_back(node.get());if(LLObjectSelection::is_valid_root()(*it))mRoots.push_back(node.get());mStorage.push_back(std::move(node));}
    }
    static std::vector<std::string> nativeFingerprint()
    {
        std::vector<std::string> result;
#if VS_NATIVE_VULKAN
        const auto owner = VSNativeSession::active();
        const auto selection = owner ? owner->objectSelection() : nullptr;
        if (!selection) return result;
        // Geometry/image identity only: do not rebuild/copy selected property
        // names, permission rows or editor nodes in every visible frame.
        for (const auto& token : selection->selected())
        {
            VSInspectionObject object(token);
            object.fingerprint(result);
        }
#endif
        return result;
    }
    bool current()const{return mCurrent();}
    auto valid_begin(){return mNodes.begin();}auto valid_end(){return mNodes.end();}
    auto valid_root_begin(){return mRoots.begin();}auto valid_root_end(){return mRoots.end();}
    S32 getObjectCount()const{return mObjectCount;}S32 getRootObjectCount()const{return mRootCount;}
    std::vector<std::string>fingerprint()const{std::vector<std::string>result;for(const auto&node:mStorage)node->getObject()->fingerprint(result);return result;}
};
