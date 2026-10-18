#include <vector>
#include <tuple>
#include <cassert>
using MASK=int;using S32=int;struct LLWindow{};struct LLCoordGL{int x=1,y=2;};enum{CLICK_BUTTON4=40,CLICK_BUTTON5=50};
struct Input{std::vector<std::tuple<int,bool>>events;void handleMouse(LLWindow*,LLCoordGL,MASK,int b,bool d){events.emplace_back(b,d);}}gViewerInput;
struct LLViewerWindow{bool mNativeVulkan=false;std::vector<std::tuple<int,bool>>events;bool nativeMouse(LLCoordGL,MASK,int b,bool d){events.emplace_back(b,d);return false;}bool handleOtherMouse(LLWindow*,LLCoordGL,MASK,S32,bool);};
METHOD
int main(){for(bool native:{false,true})for(int b=0;b<8;++b)for(bool down:{false,true}){LLViewerWindow w;w.mNativeVulkan=native;gViewerInput.events.clear();assert(w.handleOtherMouse(nullptr,{},3,b,down));const auto expected=b==4?CLICK_BUTTON4:CLICK_BUTTON5;if(b==4||b==5){auto expectedEvents=std::vector<std::tuple<int,bool>>{{expected,down}};assert(gViewerInput.events==expectedEvents);assert(w.events.empty());}else assert(w.events.empty()&&gViewerInput.events.empty());}}
