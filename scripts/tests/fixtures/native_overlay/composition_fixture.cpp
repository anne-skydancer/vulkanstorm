#include <cassert>
#include <vector>
#include <functional>
std::vector<int> events;
constexpr int RESOURCE_STATE_TRANSITION_MODE_TRANSITION=0,CLEAR_DEPTH_FLAG=1;
struct Context{void SetRenderTargets(int,int*,int,int){events.push_back(20);}void SetViewports(int,int*,int,int){events.push_back(21);}void ClearDepthStencil(int,int,int,int,int){events.push_back(22);}}storage;
void render(const std::vector<bool>& packets,const std::function<void()>& beforeHud){
 EMPTY
 Context*context=&storage;int target=0,depth=0,viewport=0,width=640,height=480;
 SEAM
 for(bool hud:packets){if(hud){FIRST}events.push_back(hud?2:1);}
 FINISH
}
int main(){
 const auto overlay=[](){events.push_back(9);};
 render({},overlay);assert((events==std::vector<int>{9}));events.clear();
 render({false,false},overlay);assert((events==std::vector<int>{1,1,9,20,21}));events.clear();
 render({false,true,true},overlay);assert((events==std::vector<int>{1,9,20,21,22,2,2}));events.clear();
 render({true},{});assert((events==std::vector<int>{22,2}));
}
