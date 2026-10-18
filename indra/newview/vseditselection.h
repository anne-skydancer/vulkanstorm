// Genuine native object/property views for the existing Build controllers.
// LGPL-2.1, like the viewer.
#pragma once
#include "vsmaterialselection.h"
#include "vsobjecteditparams.h"
#include <memory>
#include <vector>

class VSEditObject : public VSMaterialObject
{
public:
    explicit VSEditObject(const LLUUID& id) : VSMaterialObject(VSMaterialObject::find(id)) {}
    explicit VSEditObject(const VSWorldScene::ObjectHandle& token) : VSMaterialObject(VSMaterialObject::find(token)) {}
    bool                       permMove() const;
    bool                       flagObjectPermanent() const;
    bool                       flagCharacter() const;
    bool                       flagAnimSource() const { return (flags() & FLAGS_ANIM_SOURCE) != 0; }
    bool                       getIncludeInSearch() const;
    U8                         getClickAction() const;
    bool                       isRootEdit() const;
    U32                        flags() const;
    bool                       isAttachment() const { return task().isAttachment(); }
    LLUUID                     getAttachmentItemID() const { return task().getAttachmentItemID(); }
    bool                       payable() const;
    VSEditObject*              getRootEdit() const;
    VSEditObject*              getRoot() const;
    VSEditObject*              getParent() const;
    std::vector<VSEditObject*> getChildren() const;
    S32                        numChildren() const { return S32(getChildren().size()); }
    bool                       isRoot() const { return !getParent(); }
    bool                       isAvatar() const { return task().isAvatar(); }
    bool                       permYouOwner() const { return task().permYouOwner(); }
    bool                       isSelected() const;
    LLVector3                  getPosition() const;
    LLQuaternion               getRotation() const;
    LLVector3                  getPositionEdit() const;
    LLQuaternion               getRotationEdit() const;
    LLVector3                  getPositionRegion() const;
    LLQuaternion               getRotationRegion() const;
    LLVector3d                 getPositionGlobal() const;
    bool                       setPosition(const LLVector3&, bool damped = false);
    bool                       setPositionParent(const LLVector3& value, bool damped = false) { return setPosition(value, damped); }
    bool                       setPositionEdit(const LLVector3&, bool damped = false);
    bool                       setRotation(const LLQuaternion&, bool damped = false);
    std::shared_ptr<VSObjectSelection::SavedTransform> transformState() const;
    bool                                               saveUnselectedChildrenPosition(std::vector<LLVector3>&);
    bool                                               saveUnselectedChildrenRotation(std::vector<LLQuaternion>&);
    bool                           resetChildrenRotationAndPosition(const std::vector<LLQuaternion>&, const std::vector<LLVector3>&);
    bool                           resetChildrenPosition(const LLVector3&, bool simplified = false, bool skipAvatarChild = false);
    LLVector3                      getScale() const;
    bool                           setScale(const LLVector3&, bool damped = false);
    void                           updateVolume(const LLVolumeParams&);
    bool                           isMesh() const;
    bool                           isFlexible() const;
    bool                           canBeFlexible() const;
    bool                           setIsFlexible(bool);
    U32                            getExtendedMeshFlags() const;
    void                           setExtendedMeshFlags(U32);
    bool                           isAnimatedObject() const;
    bool                           canBeAnimatedObject() const;
    void                           refreshBakeTexture();
    U8                             getPCode() const { return task().getPCode(); }
    bool                           isDead() const { return !bool(*this); }
    LLPointer<LLVolume>            getVolume() const;
    const VSObjectEditParameters*  parameters() const;
    VSObjectEditParameters*        mutableParameters();
    void                           parameterChanged(U16, bool);
    bool                           setParameterEntry(U16, const LLNetworkData&, bool localOrigin);
    bool                           setParameterEntryInUse(U16, bool, bool localOrigin);
    const LLLightParams*           getLightParams() const;
    LLLightParams*                 getLightParams();
    const LLLightImageParams*      getLightImageParams() const;
    LLLightImageParams*            getLightImageParams();
    const LLReflectionProbeParams* getReflectionProbeParams() const;
    LLReflectionProbeParams*       getReflectionProbeParams();
    const LLFlexibleObjectData*    getFlexibleObjectData() const;
    const LLSculptParams*          getSculptParams() const;
    bool                           getIsLight() const;
    void                           setIsLight(bool);
    LLColor3                       getLightLinearColor() const;
    LLColor3                       getLightLinearBaseColor() const;
    LLColor3                       getLightSRGBColor() const;
    LLColor3                       getLightSRGBBaseColor() const;
    void                           setLightLinearColor(const LLColor3&);
    void                           setLightSRGBColor(const LLColor3&);
    F32                            getLightIntensity() const;
    F32                            getLightRadius() const;
    F32                            getLightFalloff(F32 fudge_factor = 1.f) const;
    void                           setLightIntensity(F32);
    void                           setLightRadius(F32);
    void                           setLightFalloff(F32);
    const LLVector3&               getSpotLightParams() const;
    void                           setSpotLightParams(LLVector3);
    const LLUUID&                  getLightTextureID() const;
    void                           setLightTextureID(LLUUID);
    bool                           isLightSpotlight() const;
    bool                           isReflectionProbe() const;
    bool                           setIsReflectionProbe(bool);
    F32                            getReflectionProbeAmbiance() const;
    F32                            getReflectionProbeNearClip() const;
    bool                           getReflectionProbeIsBox() const;
    bool                           getReflectionProbeIsDynamic() const;
    bool                           getReflectionProbeIsMirror() const;
    bool                           setReflectionProbeAmbiance(F32);
    bool                           setReflectionProbeNearClip(F32);
    bool                           setReflectionProbeIsBox(bool);
    bool                           setReflectionProbeIsDynamic(bool);
    bool                           setReflectionProbeIsMirror(bool);
    VSObjectCostValues             costs() const;
    F32                            getObjectCost() const { return costs().object; }
    F32                            getLinksetCost() const { return costs().linkset; }
    F32                            getPhysicsCost() const { return costs().physics; }
    F32                            getLinksetPhysicsCost() const { return costs().linksetPhysics; }
    void                           setObjectCostStale();
    U8                             getPhysicsShapeType() const;
    F32                            getPhysicsGravity() const;
    F32                            getPhysicsDensity() const;
    F32                            getPhysicsFriction() const;
    F32                            getPhysicsRestitution() const;
    void                           setPhysicsShapeType(U8);
    void                           setPhysicsGravity(F32);
    void                           setPhysicsDensity(F32);
    void                           setPhysicsFriction(F32);
    void                           setPhysicsRestitution(F32);
    void                           updateFlags(bool physicsChanged = false);
    bool                           flagUsePhysics() const;
    bool                           flagTemporaryOnRez() const;
    bool                           flagPhantom() const;
    bool                           flagVolumeDetect() const;
    bool                           setFlags(U32, bool);
    bool                           setFlagsWithoutUpdate(U32, bool);
    void                           setClickAction(U8);
    U8                             getMaterial() const;
    bool                           setMaterial(U8);
    void                           sendMaterialUpdate() const;
    void                           sendShapeUpdate() const;

private:
    std::unique_ptr<VSObjectEditParameters>                    mParameters;
    std::weak_ptr<const VSObjectEditParameters>                mParameterOwner;
    std::shared_ptr<const VSResourceBudget::Reservation>       mParameterResidency;
    mutable std::shared_ptr<VSObjectSelection::SavedTransform> mTransform;
    mutable std::unique_ptr<VSEditObject>                      mRoot, mParent, mTopRoot;
    mutable std::vector<std::unique_ptr<VSEditObject>>         mChildren;
    void getInventoryContents(LLInventoryObject::object_list_t& out) const { task().getInventoryContents(out); }
};

class VSEditNode
{
public:
    VSEditNode(const std::shared_ptr<VSObjectSelection>&, const VSWorldScene::ObjectHandle&);
    bool isTESelected(S32 face) const;
    bool matches(const std::shared_ptr<const VSObjectProperties>& properties, bool individual) const
    { return mProperties == properties && mIndividualSelection == individual; }

    VSEditObject*              getObject() { return mObject ? &mObject : nullptr; }
    const VSEditObject*        getObject() const { return mObject ? &mObject : nullptr; }
    VSWorldScene::ObjectHandle source;
    bool                       mValid = false, mIndividualSelection = false;
    std::string                mName, mDescription;
    LLPermissions              permissionValue;
    LLPermissions* const       mPermissions = &permissionValue;
    LLSaleInfo                 mSaleInfo;
    LLAggregatePermissions     mAggregateTexturePerm, mAggregateTexturePermOwner;

private:
    friend class VSObjectSelection;
    std::shared_ptr<const VSResourceBudget::Reservation> mResidency;
    VSEditObject                                         mObject;
    std::weak_ptr<VSObjectSelection> mSelection;
    std::shared_ptr<const VSObjectProperties>            mProperties;
};

class VSEditSelection
{
public:
    VSEditSelection();
    VSEditSelection* getSelection() { return this; }
    VSEditNode*      getFirstNode();
    VSEditNode*      getFirstRootNode();
    VSEditObject*    getFirstObject()
    {
        const auto node = getFirstNode();
        return node ? node->getObject() : nullptr;
    }
    VSEditObject* getFirstRootObject()
    {
        const auto node = getFirstRootNode();
        return node ? node->getObject() : nullptr;
    }
    VSEditObject* getPrimaryObject();
    S32           getRootObjectCount() const { return S32(mRoots.size()); }
    S32           getObjectCount() const { return S32(mNodes.size()); }
    S32           getNumNodes() const { return getObjectCount(); }
    bool isEmpty() const { return mNodes.empty(); }
    S32 getTECount() const;
    ESelectType getSelectType() const;
    F32 getSelectedLinksetCost();
    bool objectsCrossParcel() const;
    bool          isAttachment() const;
    bool          selectionAllPCode(U8) const;
    bool          selectGetCreator(LLUUID&, std::string&) const;
    bool          selectGetOwner(LLUUID&, std::string&) const;
    bool          selectGetLastOwner(LLUUID&, std::string&) const;
    bool          selectGetGroup(LLUUID&) const;
    bool          selectIsGroupOwned() const;
    bool          selectGetPerm(U8, U32*, U32*);
    bool          selectGetSaleInfo(LLSaleInfo&);
    void          selectGetAggregateSaleInfo(U32&, bool&, bool&, S32&, S32&);
    bool          selectGetEditMoveLinksetPermissions(bool&, bool&);
    bool          selectGetAggregateTexturePermissions(LLAggregatePermissions&);
    bool          selectGetModify();
    bool          selectGetRootsModify();
    bool          selectGetNonPermanentEnforced();
    bool          selectGetRootsNonPermanentEnforced();
    bool          selectGetNonPathfinding();
    bool          selectGetRootsNonPathfinding();
    bool          selectGetPermanent();
    bool          selectGetRootsPermanent();
    bool          selectGetCharacter();
    bool          selectGetRootsCharacter();
    bool          selectGetRootsCopy();
    bool          selectGetRootsTransfer();
    bool          selectionGetClickAction(U8*);
    bool          selectionGetIncludeInSearch(bool*);
    void          selectionSetObjectName(const std::string&);
    void          selectionSetObjectDescription(const std::string&);
    void          selectionSetObjectSaleInfo(const LLSaleInfo&);
    void          selectionSetObjectPermissions(U8, bool, U32, bool override = false);
    void          selectionSetClickAction(U8);
    void          selectionSetIncludeInSearch(bool);
    bool          saveSelectedObjectTransform(EActionType);
    bool          adjustTexturesByScale(bool send, bool stretch);
    void          updateSelectionCenter()
    {
        if (mSelection && mCurrent())
            mSelection->updateSelectionCenter();
    }
    void sendMultipleUpdate(U32 type)
    {
        if (mSelection && mCurrent())
            mSelection->sendMultipleUpdate(type);
    }
    bool                                      selectionSetGLTFMaterial(const LLUUID&);
    void                                      selectionSetAlphaOnly(F32);
    bool                                      selectionSetMedia(U8,const LLSD&);
    void                                      sendGodlikeRequest(const std::string&,const std::string&);
    void                                      selectionUpdatePhantom(bool);
    void                                      selectionUpdatePhysics(bool);
    void                                      selectionUpdateTemporary(bool);
    void                                      selectionSetDensity(F32);
    void                                      selectionSetFriction(F32);
    void                                      selectionSetGravity(F32);
    void                                      selectionSetRestitution(F32);
    void                                      selectionSetPhysicsType(U8);
    void                                      selectionSetMaterial(U8);
    void                                      sendGroup(const LLUUID&);
    void                                      sendOwner(const LLUUID&, const LLUUID&, bool override = false);
    auto                                      begin() { return mNodes.begin(); }
    auto                                      end() { return mNodes.end(); }
    auto                                      root_begin() { return mRoots.begin(); }
    auto                                      root_end() { return mRoots.end(); }
    const std::shared_ptr<VSObjectSelection>& nativeSelection() const { return mSelection; }
    std::function<bool()>                     guard() const { return mCurrent; }

private:
    std::shared_ptr<VSObjectSelection> mSelection;
    std::function<bool()>              mCurrent = []
    {
        return false;
    };
    std::vector<std::shared_ptr<VSEditNode>> mStorage;
    std::vector<VSEditNode*>                 mNodes, mRoots, mRootObjects;
    template<class Get>
    bool identity(Get get, LLUUID& result) const;
    void send(const char*, bool roots, const std::function<void(LLMessageSystem&)>& header,
              const std::function<void(LLMessageSystem&, const VSEditNode&)>& body);
};
