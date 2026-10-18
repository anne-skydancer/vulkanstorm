#include <memory>
#include <optional>
#include <functional>
#include <vector>
#include <cassert>
#define VS_NATIVE_VULKAN 1
using LLUUID=int;constexpr int FLAGS_HANDLE_TOUCH=1;bool live=true,avatar=true,source=true,rlv=false,allowed=true,build=false;int retire=0;std::vector<int>events;
void*vs_avatar_character(){return avatar?&avatar:nullptr;}struct VSWorldScene{struct ObjectHandle{int id=7;};struct Attachment{ObjectHandle object;int item=11;};std::vector<Attachment>attachments(){return source?std::vector<Attachment>{{}}:std::vector<Attachment>{};}std::function<bool()>guard(ObjectHandle){return []{return source;};}}world;
struct Context{VSWorldScene&world(){return ::world;}}context;struct Window{bool isNativeVulkan(){return true;}Context*nativeContext(){return &context;}}window;Window*gViewerWindow=&window;
struct Inventory{int getLinkedItemID(int x){events.push_back(1);return x==100?11:x;}}gInventory;
struct VSObjectContext{int flags=1;static std::optional<VSObjectContext>resolve(int id){events.push_back(2);return source&&id==7?std::optional<VSObjectContext>{{}}:std::nullopt;}};
struct RlvActions{static bool isRlvEnabled(){return rlv;}static bool canTouch(const VSObjectContext&){events.push_back(3);return allowed;}};
struct Selection{void deselectAll(){events.push_back(4);if(retire==1)live=false;if(retire==2)source=false;}bool selectFamily(VSWorldScene::ObjectHandle){events.push_back(5);if(retire==3)live=false;return true;}std::shared_ptr<const void>retainSelection(){events.push_back(6);return std::make_shared<int>(1);}auto selected(){return std::vector<VSWorldScene::ObjectHandle>{{},{}};}void setTransient(VSWorldScene::ObjectHandle,bool x){assert(x);events.push_back(7);}}selection;
struct VSNativeSession{static std::shared_ptr<VSNativeSession>owner;static auto active(){return owner;}bool regionReady(){return live;}std::function<bool()>accountGuard(){return []{return live;};}Selection*objectSelection(){return &selection;}};std::shared_ptr<VSNativeSession>VSNativeSession::owner;
struct LLToolMgr{static LLToolMgr*getInstance(){static LLToolMgr s;return &s;}bool inBuildMode(){return build;}};
void handle_object_touch(){events.push_back(8);}bool enable_attachment_touch(const LLUUID&);
LOOKUP
ENABLE
@@TOUCH@@
int main(){for(int mode=0;mode<9;++mode){VSNativeSession::owner=std::make_shared<VSNativeSession>();live=avatar=source=true;rlv=false;allowed=true;build=false;retire=0;if(mode==1)avatar=false;if(mode==2)live=false;if(mode==3)source=false;if(mode==4){rlv=true;allowed=false;}if(mode==5)build=true;if(mode>=6)retire=mode-5;events.clear();handle_attachment_touch(100);bool didTouch=false,transient=false;for(int e:events){didTouch|=e==8;transient|=e==7;}assert(didTouch==(mode==0||mode==5));assert(transient==(mode==0));}events.clear();handle_attachment_touch(999);for(int e:events)assert(e!=4&&e!=8);}
