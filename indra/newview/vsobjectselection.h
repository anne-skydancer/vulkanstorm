// Native selection owns real simulator properties and current scene handles. LGPL-2.1.
#pragma once
#include "vsworldscene.h"
#include "vsobjectproperties.h"
#include <boost/signals2/connection.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
class LLMessageSystem;
class VSNativeSession;
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
    bool selectOnly(const VSWorldScene::ObjectHandle&,S32 face=-1,S32 gltfNode=-1,S32 gltfPrimitive=-1);
    bool selectFamily(const VSWorldScene::ObjectHandle&,bool addToEnd=false,bool ignoreSelectOwned=false);
    bool deselectOnly(const VSWorldScene::ObjectHandle&,bool sendToSimulator=true);
    bool deselectFamily(const VSWorldScene::ObjectHandle&,bool sendToSimulator=true,bool includeEntireObject=false);
    bool deselectAll();
    bool contains(const VSWorldScene::ObjectHandle&,S32 face=-1) const;
    std::optional<NodeMetadata> node(const VSWorldScene::ObjectHandle&) const;
    bool setHoverObject(const std::optional<VSWorldScene::ObjectHandle>&,S32 face=-1);
    std::optional<Publication> hoverProperties(const VSWorldScene::ObjectHandle&) const;
    std::optional<Publication> findProperties(const VSWorldScene::ObjectHandle&) const; // Selected valid nodes only, exact reference permission lookup.
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
    std::optional<Publication> properties(const VSWorldScene::ObjectHandle&) const;
    std::function<bool()> selectionGuard() const;
    boost::signals2::connection changed(const std::function<void()>&);
    bool requestFamily(const VSWorldScene::ObjectHandle&,U32 flags,FamilyCallback);
    bool beginHighlightRequest(std::function<bool()>);
    bool highlight(const std::vector<VSWorldScene::ObjectHandle>&,bool replace);
    std::vector<VSWorldScene::ObjectHandle> highlights() const;
    void clearHighlights();
    void receive(LLMessageSystem&);
    void tick();
    void reset();
private:
    void requestHoverProperties();
    void refreshSelectionGeometry();
    void updateAttention();
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
