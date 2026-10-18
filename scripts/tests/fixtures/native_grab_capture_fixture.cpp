#define VS_NATIVE_VULKAN 1
#include <memory>
#include <functional>
#include <optional>
#include <cassert>
struct Token{int host=1,id=2,local=3,region=4,generation=5,kind=6,account=7,accountGeneration=8;};
struct World{std::optional<Token> item=Token{};bool live=true;int guards=0;std::optional<Token>object(int){return item;}std::function<bool()>guard(const Token&){++guards;return [this]{return live;};}};
struct Context{World model;World&world(){return model;}};
struct Window{bool native=true;Context*context=nullptr;bool isNativeVulkan(){return native;}Context*nativeContext(){return context;}}window;Window*gViewerWindow=&window;
struct LLPickInfo{int mObjectID=2;std::function<bool()>mNativeObjectCurrent;};
struct LLToolGrabBase{struct NativeGrab{Token object;std::function<bool()>current;};std::shared_ptr<NativeGrab>mNativeGrab;void captureNativeObject(const LLPickInfo&);};
METHOD
int main(){Context context;window.context=&context;LLToolGrabBase tool;LLPickInfo pick;bool pickLive=true;pick.mNativeObjectCurrent=[&]{return pickLive;};tool.captureNativeObject(pick);assert(tool.mNativeGrab &&context.model.guards==1);auto saved=tool.mNativeGrab;tool.captureNativeObject(pick);assert(tool.mNativeGrab==saved &&context.model.guards==1);
pickLive=false;tool.captureNativeObject(pick);assert(!tool.mNativeGrab);pickLive=true;
int Token::*fields[]={&Token::host,&Token::id,&Token::local,&Token::region,&Token::generation,&Token::kind,&Token::account,&Token::accountGeneration};
for(auto field:fields){context.model.item=Token{};tool.captureNativeObject(pick);auto old=tool.mNativeGrab;++((*context.model.item).*field);tool.captureNativeObject(pick);assert(tool.mNativeGrab &&tool.mNativeGrab!=old &&tool.mNativeGrab->object.*field==(*context.model.item).*field);}
context.model.item.reset();tool.captureNativeObject(pick);assert(!tool.mNativeGrab);context.model.item=Token{};tool.captureNativeObject(pick);context.model.live=false;auto old=tool.mNativeGrab;tool.captureNativeObject(pick);assert(tool.mNativeGrab!=old);window.context=nullptr;tool.captureNativeObject(pick);assert(!tool.mNativeGrab);
}
