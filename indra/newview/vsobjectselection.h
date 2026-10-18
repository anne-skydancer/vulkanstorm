// Native selection owns real simulator properties and current scene handles. LGPL-2.1.
#pragma once
#include "vsworldscene.h"
#include "llgltfmaterial.h"
#include "vsobjectproperties.h"
#include <boost/signals2/connection.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
class LLMessageSystem;
class VSNativeSession;
class VSEditNode;
struct VSTextureScaleRatios;
class VSObjectSelection
{
public:
    struct Publication
    {
        VSWorldScene::ObjectHandle object;
        std::shared_ptr<const VSObjectProperties> properties;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
    };
    // Native selection metadata belongs to real current simulator handles.
    // Public signatures reserved for the shared media focus peer adapter.
    struct NodeMetadata
    {
        U32 faceMask=0;
        S32 lastFace=0,gltfNode=-1,gltfPrimitive=-1;
        bool transient=false,individual=false,primary=false;
    };
    // Real selected-node editor state. Retained by that node's lifetime and
    // account identity, independent of transient Build row wrappers.
    struct SavedTransform
    {
        LLVector3 mSavedPositionLocal,mLastPositionLocal,mLastMoveLocal;
        LLVector3d mSavedPositionGlobal;
        LLVector3 mSavedScale,mLastScale;
        LLQuaternion mSavedRotation,mLastRotation;
        bool mDuplicated=false;
        LLVector3d mDuplicatePos;
        LLQuaternion mDuplicateRot;
        std::shared_ptr<VSTextureScaleRatios> textureRatios;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
        std::vector<LLVector3> mUnselectedChildrenPositions;
        std::vector<VSWorldScene::ObjectHandle> positionChildren,rotationChildren;
        std::shared_ptr<const VSResourceBudget::Reservation> positionResidency,rotationResidency;
        std::size_t rotationBase=0;
    };
    std::shared_ptr<SavedTransform> savedTransform(const VSWorldScene::ObjectHandle&) const;
    std::shared_ptr<VSEditNode> editNode(const VSWorldScene::ObjectHandle&);
    bool saveSelectedObjectTransform();
    std::optional<LLBBox> savedSelectionBBox() const;
    bool saveUnselectedChildrenPosition(const VSWorldScene::ObjectHandle&,std::vector<LLVector3>&);
    bool saveUnselectedChildrenRotation(const VSWorldScene::ObjectHandle&,std::vector<LLQuaternion>&);
    bool resetChildrenRotationAndPosition(const VSWorldScene::ObjectHandle&,const std::vector<LLQuaternion>&,const std::vector<LLVector3>&);
    bool resetChildrenPosition(const VSWorldScene::ObjectHandle&,const LLVector3&,bool simplified=false,bool skipAvatarChild=false);

    struct SavedMaterials
    {
        bool ready=false;
        uuid_vec_t textures;
        uuid_vec_t ids;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
        std::vector<LLPointer<LLGLTFMaterial>> overrides;
    };
    std::shared_ptr<SavedMaterials> savedMaterials(const VSWorldScene::ObjectHandle&) const;
    // Permission refresh uses authenticated ObjectProperties exactly like
    // LLSelectMgr::saveSelectedObjectTextures; never saves arbitrary local IDs.
    bool refreshSavedTextures();
    bool hideGLTFMaterial();
    bool showGLTFMaterial();
    struct SavedColors
    {
        std::vector<LLColor4> colors, shiny;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
    };
    bool saveColors(const VSWorldScene::ObjectHandle&,bool shiny);
    std::shared_ptr<const SavedColors> savedColors(const VSWorldScene::ObjectHandle&) const;

    bool selectOnly(const VSWorldScene::ObjectHandle&,S32 face=-1,S32 gltfNode=-1,S32 gltfPrimitive=-1);
    bool selectFamily(const VSWorldScene::ObjectHandle&,bool addToEnd=false,bool ignoreSelectOwned=false);
    bool deselectOnly(const VSWorldScene::ObjectHandle&,bool sendToSimulator=true);
    bool deselectFamily(const VSWorldScene::ObjectHandle&,bool sendToSimulator=true,bool includeEntireObject=false);
    bool deselectAll();
    bool contains(const VSWorldScene::ObjectHandle&,S32 face=-1) const;
    bool setFaceSelected(const VSWorldScene::ObjectHandle&,S32 face,bool selected);
    std::optional<NodeMetadata> node(const VSWorldScene::ObjectHandle&) const;
    bool setHoverObject(const std::optional<VSWorldScene::ObjectHandle>&,S32 face=-1);
    std::optional<Publication> hoverProperties(const VSWorldScene::ObjectHandle&) const;
    std::optional<VSWorldScene::ObjectHandle> hoverObject() const;
    std::optional<Publication> findProperties(const VSWorldScene::ObjectHandle&) const; // Selected valid nodes only, exact reference permission lookup.
    bool seedSelectedProperties(const VSWorldScene::ObjectHandle&,const VSObjectProperties&);
    void convertTransient();
    std::shared_ptr<const void> retainSelection() const;
    bool setForceSelection(bool);
    bool setAllowSelectAvatar(bool);
    bool allowSelectAvatar() const;
    void validateSelection();
    std::optional<LLBBox> boundingBox() const;
    LLVector3d centerGlobal() const;
    bool showSelection() const;
    void setTransient(const VSWorldScene::ObjectHandle&,bool);
    void promoteSelectionToRoot();
    void demoteSelectionToIndividuals();
    using FamilyCallback=std::function<void(const std::optional<Publication>&)>;
    explicit VSObjectSelection(std::weak_ptr<VSNativeSession>);
    ~VSObjectSelection();
    enum class SelectionType { World, Attachment, HUD };
    struct SelectionTypeState {bool selected=false;std::optional<SelectionType> type;};
    SelectionTypeState selectionType() const;
    bool canSelect(const VSWorldScene::ObjectHandle&,bool forceSelection=false,bool ignoreSelectOwned=false) const;
    bool select(const std::vector<VSWorldScene::ObjectHandle>&);
    std::vector<VSWorldScene::ObjectHandle> selected() const;
    bool visitSelected(const std::function<bool(const VSWorldScene::ObjectHandle&,const NodeMetadata&)>&) const;
    std::optional<VSWorldScene::ObjectHandle> firstRootObject(bool nonRootOK=false) const;
    std::optional<VSWorldScene::ObjectHandle> primaryObject() const;
    S32 rootObjectCount() const;
    S32 objectCount() const;
    bool selectionAllPCode(U8) const;
    bool selectGetGroup(LLUUID&) const;
    bool selectGetAllValid() const;
    bool allowOperationOnNode(const VSWorldScene::ObjectHandle&, PermissionBit, U64 groupProxyPower) const;
    void sendSelect();
    void sendDetach();
    bool addGridObject(const VSWorldScene::ObjectHandle&);
    void clearGridObjects();
    bool getGrid(S32 mode,LLVector3& origin,LLQuaternion& rotation,LLVector3& scale,bool forSnapGuides=false);
    bool enableLinkObjects() const;
    bool enableUnlinkObjects() const;
    bool linkObjects();
    bool unlinkObjects();
    void sendLink();
    void sendDelink();
    void sendMultipleUpdate(U32 type);
    bool canUndo() const;
    bool canRedo() const;
    bool canDoDelete() const;
    bool canDuplicate() const;
    bool canDeselect() const {return objectCount()!=0;}
    void undo();
    void redo();
    void selectDelete();
    void selectDuplicate(const LLVector3&,bool selectCopy);
    void repeatDuplicate();

    enum class SendOrder{RootsOnly,Individuals,RootsFirst,ChildrenFirst};
    bool sendEditCommand(const char*,SendOrder,const std::function<void(LLMessageSystem&)>& header,
        const std::function<void(LLMessageSystem&,const VSWorldScene::ObjectHandle&)>& body);

    void overrideObjectUpdates();
    bool resetObjectOverrides();
    void resetAvatarOverrides();
    void overrideAvatarUpdates();
    bool rebuildObject(const VSWorldScene::ObjectHandle&);
    void updateSelectionCenter();

    std::optional<Publication> properties(const VSWorldScene::ObjectHandle&) const;
    std::function<bool()> selectionGuard() const;
    boost::signals2::connection changed(const std::function<void()>&);
    boost::signals2::connection materialsChanged(const std::function<void(const LLUUID&, S32)>&);
    bool requestFamily(const VSWorldScene::ObjectHandle&,U32 flags,FamilyCallback);
    bool beginHighlightRequest(std::function<bool()>);
    bool highlight(const std::vector<VSWorldScene::ObjectHandle>&,bool replace);
    std::vector<VSWorldScene::ObjectHandle> highlights() const;
    void clearHighlights();
    bool updateRectangle(const LLCamera&,bool grow,bool shrink,bool subtract,bool limit,F32 distanceSquared,
                         const LLVector3& avatar,const std::function<bool()>& operation);
    bool commitRectangle(bool subtract,const std::function<bool()>& operation);
    void clearRectangle();
    std::vector<VSWorldScene::ObjectHandle> rectangleHighlights() const;
    void receive(LLMessageSystem&);
    void tick();
    void reset();
private:
    void requestHoverProperties();
    void bindEditMenuHandler();
    void deleteConfirmed();
    void refreshSelectionGeometry();
    void updateAttention();
    struct Impl;
    bool captureSavedMaterials(const VSWorldScene::ObjectHandle&,const std::shared_ptr<const VSObjectProperties>&);
    std::shared_ptr<Impl> mImpl;
};
