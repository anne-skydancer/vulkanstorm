#include <memory>
#include <functional>
#include <optional>
#include <cassert>
using U8=unsigned char;using U64=unsigned long long;using LLUUID=int;struct LLPickInfo{};
bool alive=true,targetCurrent=true,selectionCurrent=true,failLease=false;std::function<void()>duringSelect;std::weak_ptr<int>lease;
struct VSWorldScene{struct ObjectHandle{int id=1;};ObjectHandle token;std::optional<ObjectHandle>object(int id){return id==token.id?std::optional<ObjectHandle>{token}:std::nullopt;}std::function<bool()>guard(const ObjectHandle&){return []{return targetCurrent;};}}world;
struct Context{VSWorldScene&world(){return ::world;}}context;struct Window{Context*nativeContext(){return &context;}}window;Window*gViewerWindow=&window;
struct Selection{struct Connection{};std::function<bool()>selectionGuard(){return [] {return selectionCurrent;};}Connection changed(std::function<void()>){return {};}}selection;
struct VSNativeSession{static std::shared_ptr<VSNativeSession>activeOwner;static auto active(){return activeOwner;}bool regionReady(){return alive;}std::function<bool()>accountGuard(){return []{return alive;};}Selection*existingObjectSelection(){return &selection;}};std::shared_ptr<VSNativeSession>VSNativeSession::activeOwner;
struct LLToolSelect{static std::shared_ptr<const void>handleNativeObjectSelection(const LLPickInfo&,bool,bool,bool){if(duringSelect){auto cb=duringSelect;duringSelect={};cb();}if(failLease)return {};auto l=std::make_shared<int>(1);lease=l;return l;}};
struct LLToolPie{struct NativeClickIntent{VSWorldScene::ObjectHandle object;std::weak_ptr<VSNativeSession>owner;std::function<bool()>account,source,selection;std::shared_ptr<const void>lease;Selection::Connection changed;U8 action=0;};std::unique_ptr<NativeClickIntent>mNativeClickIntent;U64 mNativeClickGeneration=0;U8 mClickAction=2;void beginNativeClickAction(const LLPickInfo&,const LLUUID&,std::function<bool()>,bool);static void selectionPropertiesReceived(){};};
METHOD
int main(){for(int mode=0;mode<6;++mode){LLToolPie pie;alive=targetCurrent=selectionCurrent=true;failLease=false;duringSelect={};VSNativeSession::activeOwner=std::make_shared<VSNativeSession>();if(mode==1)duringSelect=[]{alive=false;};if(mode==2)duringSelect=[]{targetCurrent=false;};if(mode==3)failLease=true;if(mode==4)duringSelect=[&]{++pie.mNativeClickGeneration;};if(mode==5)duringSelect=[&]{pie.mClickAction=3;pie.beginNativeClickAction({},1,[]{return true;},false);};pie.beginNativeClickAction({},1,[]{return true;},false);if(mode==0||mode==5){assert(pie.mNativeClickIntent);assert(pie.mNativeClickIntent->action==(mode==5?3:2));assert(pie.mNativeClickIntent->lease);if(mode==0)assert(!lease.expired());}else{assert(!pie.mNativeClickIntent&&lease.expired());}}}
