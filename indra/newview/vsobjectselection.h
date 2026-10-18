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
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
