#include <cassert>
#include <functional>
#include <string>
#include <utility>
#include <vector>
struct LLUUID { int id=0; bool isNull() const { return !id; } };
namespace LLFolderType { enum EType { FT_NONE }; }
const std::string ROOT_FIRESTORM_FOLDER="Firestorm", FS_BRIDGE_FOLDER="#LSL Bridge";
struct Category {
    LLUUID id; std::string name;
    const auto& getUUID() const { return id; }
    const auto& getName() const { return name; }
};
struct LLInventoryModel {
    using item_array_t=std::vector<int>;
    using cat_array_t=std::vector<Category*>;
    int parent=10, requests=0;
    item_array_t items; cat_array_t cats;
    std::vector<std::function<void(const LLUUID&)>> pending;
    LLUUID findCategoryByName(const std::string&) { return {parent}; }
    void getDirectDescendentsOf(LLUUID,cat_array_t*& c,item_array_t*& i) { c=&cats; i=&items; }
    LLUUID getRootFolderID() { return {1}; }
    void createNewCategory(LLUUID,LLFolderType::EType,const std::string&,std::function<void(const LLUUID&)> callback)
    { ++requests; pending.push_back(std::move(callback)); }
    void finish(int id) { auto callback=std::move(pending.front()); pending.erase(pending.begin()); callback({id}); }
} gInventory;
int operation=1;
auto fs_bridge_account_guard() { return [epoch=operation] { return epoch==operation; }; }
struct FSLSLBridge {
    LLUUID mBridgeFolderID;
    using inventory_func_type=std::function<void(const LLUUID&)>;
    void setupFSCategory(inventory_func_type);
};
void FSLSLBridge::setupFSCategory(inventory_func_type callback) METHOD
int main() {
    FSLSLBridge bridge;
    int delivered=0;
    const auto callback=[&](const LLUUID& id) { delivered=id.id; };
    Category existing{{20},FS_BRIDGE_FOLDER}; gInventory.cats={&existing};
    bridge.setupFSCategory(callback);
    assert(delivered==20 && bridge.mBridgeFolderID.id==20 && gInventory.requests==0);
    gInventory.cats.clear(); bridge.mBridgeFolderID={}; delivered=0;
    bridge.setupFSCategory(callback); ++operation; gInventory.finish(21);
    assert(!delivered && bridge.mBridgeFolderID.isNull());
    gInventory.parent=0; int before=gInventory.requests;
    bridge.setupFSCategory(callback); ++operation; gInventory.finish(30);
    assert(gInventory.pending.empty() && gInventory.requests==before+1 && !delivered);
    bridge.setupFSCategory(callback); gInventory.finish(31);
    assert(gInventory.pending.size()==1); ++operation; gInventory.finish(32);
    assert(!delivered && bridge.mBridgeFolderID.isNull());
    bridge.setupFSCategory(callback); gInventory.finish(40); gInventory.finish(41);
    assert(delivered==41 && bridge.mBridgeFolderID.id==41);
}
