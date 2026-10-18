// CPU task inventory over the reference HTTP capability / legacy xfer protocols.
// LGPL-2.1, like the viewer.
#pragma once
#include "vsworldscene.h"
#include "llinventory.h"
#include "llviewerinventory.h"
#include <boost/signals2/connection.hpp>
#include <optional>
#include <functional>
#include <memory>
class VSNativeSession;
class LLMessageSystem;

class VSTaskInventory
{
public:
    struct Result
    {
        VSWorldScene::ObjectHandle object;
        U16 serial = 0;
        bool success = false;
        LLInventoryObject::object_list_t contents;
    };
    using Callback = std::function<void(const Result&)>;
    explicit VSTaskInventory(std::weak_ptr<VSNativeSession>);
    ~VSTaskInventory();
    struct State { bool pending = false, dirty = false; S16 serial = 0; };
    State state(const VSWorldScene::ObjectHandle&) const;
    void notify(const VSWorldScene::ObjectHandle&);
    void publishLocalChanges(const VSWorldScene::ObjectHandle&);
    void dirty(const VSWorldScene::ObjectHandle&);
    bool request(const VSWorldScene::ObjectHandle&, Callback);
    std::optional<Result> snapshot(const VSWorldScene::ObjectHandle&) const;
    LLPointer<LLInventoryObject> object(const VSWorldScene::ObjectHandle&, const LLUUID&) const;
    LLPointer<LLViewerInventoryItem> item(const VSWorldScene::ObjectHandle&, const LLUUID&) const;
    std::function<bool()> guard(const VSWorldScene::ObjectHandle&, const LLUUID& item = LLUUID::null) const;
    boost::signals2::connection observe(const VSWorldScene::ObjectHandle&, Callback);
    bool update(const VSWorldScene::ObjectHandle&, LLViewerInventoryItem*, U8 key, bool is_new = false, bool local_only = false);
    bool saveScript(const VSWorldScene::ObjectHandle&, const LLViewerInventoryItem*, bool active, bool is_new = false);
    bool move(const VSWorldScene::ObjectHandle&, const LLUUID& folder, const LLUUID& item);
    bool remove(const VSWorldScene::ObjectHandle&, const LLUUID& item);
    bool completeAsset(const VSWorldScene::ObjectHandle&, const LLUUID& item, const LLUUID& asset, const std::function<bool()>& current);
    void receive(LLMessageSystem&);
    void tick();
    void reset();
private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
