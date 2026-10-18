// Real shared material-editor peers; no fabricated legacy scene objects.
// LGPL-2.1, like the viewer.
#pragma once
#include "vstaskinventoryobject.h"
#include "llselectmgr.h"
#include "lltextureentry.h"
#include <functional>
#include <map>
#include <memory>
#if VS_NATIVE_VULKAN
#include "vsobjectselection.h"
#endif

class LLMediaEntry;

class VSMaterialObject
{
public:
    VSMaterialObject() = default;
    explicit VSMaterialObject(LLViewerObject*);
    static VSMaterialObject find(const LLUUID&);
#if VS_NATIVE_VULKAN
    static VSMaterialObject find(const VSWorldScene::ObjectHandle&);
#endif
    explicit operator bool() const { return bool(mObject); }
    const VSMaterialObject* operator->() const { return this; }
    LLUUID getID() const { return mObject.getID(); }
    bool permModify() const { return mObject.permModify(); }
    bool permCopy() const;
    bool permTransfer() const;
    bool getVolume() const;
    U8 getPCode() const { return mObject.getPCode(); }
    bool isAttachment() const { return mObject.isAttachment(); }
    LLVector3 getScale() const;
    void sendTEUpdate() const;
    S32 setTEColor(U8,const LLColor4&) const;
    S32 setTEFullbright(U8,U8) const;
    S32 setTEGlow(U8,F32) const;
    S32 setTEScale(U8,F32,F32) const;
    S32 setTEScaleS(U8,F32) const;
    S32 setTEScaleT(U8,F32) const;
    S32 setTEOffset(U8,F32,F32) const;
    S32 setTEOffsetS(U8,F32) const;
    S32 setTEOffsetT(U8,F32) const;
    S32 setTERotation(U8,F32) const;
    S32 setTEBumpmap(U8,U8) const;
    S32 setTEBumpShiny(U8,U8) const;
    S32 setTEBumpShinyFullbright(U8,U8) const;
    S32 setTETexGen(U8,U8) const;
    S32 setTEMediaFlags(U8,U8) const;
    const LLMediaEntry* mediaData(U8) const;
    bool mediaDataReady() const;
    bool syncMediaData(U8,const LLSD&,bool merge,bool ignoreAgent,bool updatePlayer=true) const;
    bool sendMediaDataUpdate() const;
    S32 setTEShiny(U8,U8) const;
    S32 setTEImage(U8,const LLUUID&) const;
    void setTEMaterialParams(U8,LLMaterialPtr) const;
    S32 getNumTEs() const;
    S32 getNumFaces() const;
    bool isImageAlphaBlended(U8) const;
    LLUUID getRenderMaterialID(S32) const;
    LLTextureEntry* getTE(S32) const;
    bool isInventoryPending() const { return mObject.isInventoryPending(); }
    bool isPermanentEnforced() const { return mObject.isPermanentEnforced(); }
    LLViewerInventoryItem* getInventoryItemByAsset(const LLUUID&) const;
    VSMaterialObject getRootEdit() const;
    std::optional<LLPermissions> permissions() const;
    void previewMaterial(S32, const LLUUID&) const;
    bool setRenderMaterialID(S32,const LLUUID&,bool updateServer=true,bool localOrigin=true) const;
    bool setGLTFMaterialOverride(S32,const LLGLTFMaterial*) const;
    bool clearTEWaterExclusion(U8) const;
    bool queueApply(S32,const LLUUID&,const LLGLTFMaterial* overrideMaterial=nullptr) const;
    void queueModify(S32, const LLGLTFMaterial*) const;
    const VSTaskInventoryObject& task() const { return mObject; }
private:
    VSTaskInventoryObject mObject;
};

class VSMaterialNode
{
public:
    explicit VSMaterialNode(LLSelectNode*);
#if VS_NATIVE_VULKAN
    VSMaterialNode(std::shared_ptr<VSObjectSelection>, const VSWorldScene::ObjectHandle&,
        std::shared_ptr<VSObjectSelection::SavedMaterials>);
#endif
    VSMaterialObject getObject() const { return mObject; }
    bool isTESelected(S32) const;
    std::vector<LLPointer<LLGLTFMaterial>>& mSavedGLTFOverrideMaterials;
private:
    VSMaterialObject mObject;
    LLSelectNode* mLegacy = nullptr;
#if VS_NATIVE_VULKAN
    std::shared_ptr<VSObjectSelection> mSelection;
    VSWorldScene::ObjectHandle mToken;
    std::shared_ptr<VSObjectSelection::SavedMaterials> mSaved;
#endif
};

class VSMaterialSelection
{
public:
    VSMaterialSelection();
    const VSMaterialSelection* operator->() const { return this; }
    bool native() const { return mNative; }
    bool isEmpty() const;
    S32 getObjectCount() const;
    VSMaterialObject getFirstObject() const;
    bool materialsPending() const;
    boost::signals2::connection changed(const std::function<void()>&) const;
    boost::signals2::connection materialsChanged(const std::function<void(const LLUUID&, S32)>&) const;
    std::function<bool()> guard() const;
    template<class F> bool applyToTEs(F* func, bool firstonly = false) const
    {
        if (!mNative) return mLegacy->applyToTEs(func, firstonly);
        bool result = !firstonly;
#if VS_NATIVE_VULKAN
        if (!mSelection) return result;
        const auto current=guard();if(!current())return false;
        // Snapshot identities, not an iterator into a reentrant selection list.
        for (const auto& token : mSelection->selected())
        {
            if(!current())return false;
            auto object = VSMaterialObject::find(token);
            if (!object || !mSelection->node(token)) continue;
            const S32 count = llmin(object->getNumTEs(), object->getNumFaces());
            for (S32 side = 0; side < count; ++side)
                if (mSelection->contains(token, side))
                {
                    if(!current())return false;
                    const bool r = func->apply(object, side);
                    if(!current())return false;
                    if (firstonly && r) return true;
                    result = result && r;
                }
        }
#endif
        return result;
    }
    template<class F> bool applyToNodes(F* func, bool firstonly = false) const
    {
        if (!mNative) return mLegacy->applyToNodes(func, firstonly);
        bool result = !firstonly;
#if VS_NATIVE_VULKAN
        if (!mSelection) return result;
        const auto current=guard();if(!current())return false;
        for (const auto& token : mSelection->selected())
            if (auto saved = mSelection->savedMaterials(token))
            {
                if(!current())return false;
                VSMaterialNode node(mSelection, token, std::move(saved));
                const bool r = func->apply(&node);
                if(!current())return false;
                if (firstonly && r) return true;
                result = result && r;
            }
#endif
        return result;
    }
private:
    bool mNative = false;
    mutable LLObjectSelectionHandle mLegacy;
#if VS_NATIVE_VULKAN
    std::shared_ptr<VSObjectSelection> mSelection;
#endif
};
