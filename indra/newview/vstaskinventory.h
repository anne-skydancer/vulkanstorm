// CPU task inventory over the reference HTTP capability / legacy xfer protocols.
// LGPL-2.1, like the viewer.
#pragma once
#include "vsworldscene.h"
#include "llinventory.h"
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
    bool request(const VSWorldScene::ObjectHandle&, Callback);
    void receive(LLMessageSystem&);
    void tick();
    void reset();
private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
