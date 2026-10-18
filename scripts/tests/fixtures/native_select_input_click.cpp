#include <cassert>
#include <functional>
#define VS_NATIVE_VULKAN 1
using S32=int;using MASK=int;
struct Settings{bool getBOOL(const char*){return true;}}gSavedSettings;
template<class T>struct LLCachedControl{LLCachedControl(Settings&,const char*){}operator T()const{return true;}};
struct ID{bool present=true;bool notNull()const{return present;}};
struct Object{bool present=true;bool notNull()const{return present;}};
struct LLPickInfo{ID mObjectID;Object object;std::function<bool()>mNativeObjectCurrent;Object getObject()const{return object;}};
struct Window{bool native=false;LLPickInfo next;bool isNativeVulkan(){return native;}LLPickInfo pickImmediate(S32,S32,bool,bool,bool,bool,bool){return next;}}window;
Window*gViewerWindow=&window;
struct LLTool{int down=0,up=0;bool handleMouseDown(S32,S32,MASK){++down;return false;}bool handleMouseUp(S32,S32,MASK){++up;return false;}};
struct LLToolSelect:LLTool{LLPickInfo mPick;bool mIgnoreGroup=false;int nativeSelect=0,referenceSelect=0;
 bool handleMouseDown(S32,S32,MASK);bool handleMouseUp(S32,S32,MASK);
 void handleNativeObjectSelection(const LLPickInfo&,bool ignore,bool temp){assert(ignore&&!temp);++nativeSelect;}
 void handleObjectSelection(const LLPickInfo&,bool ignore,bool temp){assert(ignore&&!temp);++referenceSelect;}
};
DOWN
UP
int main(){LLToolSelect tool;window.next.object.present=false;window.next.mObjectID.present=true;window.native=true;assert(tool.handleMouseDown(1,2,0));assert(tool.down==1);assert(!tool.handleMouseUp(1,2,0));assert(tool.nativeSelect==1&&tool.referenceSelect==0&&tool.up==1);
 window.next.mNativeObjectCurrent=[]{return false;};assert(!tool.handleMouseDown(1,2,0));window.next.mNativeObjectCurrent={};window.next.mObjectID.present=false;assert(!tool.handleMouseDown(1,2,0));
 window.native=false;window.next.object.present=true;assert(tool.handleMouseDown(1,2,0));tool.handleMouseUp(1,2,0);assert(tool.referenceSelect==1&&tool.nativeSelect==1);
}
