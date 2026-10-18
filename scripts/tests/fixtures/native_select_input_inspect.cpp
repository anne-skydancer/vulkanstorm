#include <cassert>
#include <functional>
#include <memory>
#include <vector>
#define VS_NATIVE_VULKAN 1
std::vector<int> events;
struct LLViewerObject{};
struct LLPickInfo{int mObjectID=1;std::function<bool()>mNativeRequestCurrent,mNativeObjectCurrent;LLViewerObject*getObject()const;};
LLViewerObject object;LLViewerObject* LLPickInfo::getObject()const{return mObjectID?&object:nullptr;}
struct Settings{bool getBOOL(const char*){return true;}}gSavedSettings;
struct SelectRect{void handleNativeObjectSelection(const LLPickInfo&,bool,bool){events.push_back(1);}void handleObjectSelection(const LLPickInfo&,bool,bool){events.push_back(1);}void handlePick(const LLPickInfo&){events.push_back(3);}};
struct Selection{int count=1;int objectCount(){return count;}int getObjectCount(){return count;}};
Selection selection;
struct LLSelectMgr{static LLSelectMgr*getInstance(){static LLSelectMgr x;return &x;}Selection*getSelection(){return &selection;}};
struct LLEditMenuHandler{static LLSelectMgr*gEditMenuHandler;};LLSelectMgr*LLEditMenuHandler::gEditMenuHandler=nullptr;
struct Context{Context&world(){return *this;}bool object(int id){return id!=0;}};
struct Window{bool native=true;Context context;bool isNativeVulkan(){return native;}Context*nativeContext(){return &context;}};
Window window;Window*gViewerWindow=&window;
struct VSNativeSession{static std::shared_ptr<VSNativeSession> live;static auto active(){return live;}std::shared_ptr<Selection>retainObjectSelection(){return {live,&selection};}};
std::shared_ptr<VSNativeSession> VSNativeSession::live=std::make_shared<VSNativeSession>();
struct LLToolCompInspect{
 static LLToolCompInspect*live;static bool instanceExists(){return live;}static LLToolCompInspect*getInstance(){return live;}
 bool mMouseDown=true,mIsToolCameraActive=true;SelectRect rect,*mSelectRect=&rect;std::function<void()>duringSet;
 void setCurrentTool(SelectRect*){events.push_back(2);if(duringSet)duringSet();}
 static void pickCallback(const LLPickInfo&);static void referenceCallback(const LLPickInfo&);
};LLToolCompInspect*LLToolCompInspect::live=nullptr;
REFERENCE
ACTUAL
struct Result{std::vector<int>trace;bool camera;bool handler;};
Result run(bool native,bool reference,bool down,int hit,int count){LLToolCompInspect tool;LLToolCompInspect::live=&tool;tool.mMouseDown=down;selection.count=count;window.native=native;events.clear();LLEditMenuHandler::gEditMenuHandler=nullptr;LLPickInfo pick;pick.mObjectID=hit;if(reference)tool.referenceCallback(pick);else tool.pickCallback(pick);return {events,tool.mIsToolCameraActive,LLEditMenuHandler::gEditMenuHandler!=nullptr};}
int main(){for(bool down:{false,true})for(int hit:{0,1})for(int count:{0,1,4}){auto r=run(false,true,down,hit,count);auto gl=run(false,false,down,hit,count);auto n=run(true,false,down,hit,count);assert(r.trace==gl.trace&&r.camera==gl.camera&&r.handler==gl.handler);assert(r.trace==n.trace&&r.camera==n.camera&&r.handler==n.handler);}
 LLToolCompInspect tool;LLToolCompInspect::live=&tool;window.native=true;events.clear();LLPickInfo stale;stale.mNativeRequestCurrent=[]{return false;};tool.pickCallback(stale);assert(events.empty());stale.mNativeRequestCurrent={};stale.mNativeObjectCurrent=[]{return false;};tool.pickCallback(stale);assert(events.empty());
 bool current=true;LLPickInfo reenter;reenter.mNativeRequestCurrent=[&]{return current;};tool.duringSet=[&]{current=false;};tool.pickCallback(reenter);assert((events==std::vector<int>{2}));
}
