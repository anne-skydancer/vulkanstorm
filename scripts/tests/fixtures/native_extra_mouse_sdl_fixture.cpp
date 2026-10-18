#include <vector>
#include <tuple>
#include <cassert>
constexpr int SDL_BUTTON_LEFT=1,SDL_BUTTON_MIDDLE=2,SDL_BUTTON_RIGHT=3,SDL_BUTTON_X1=4,SDL_BUTTON_X2=5;
struct Coord{};
struct Callbacks{std::vector<std::tuple<int,int>>events;
void handleMouseDown(void*,Coord,int){events.emplace_back(1,1);}void handleDoubleClick(void*,Coord,int){events.emplace_back(2,1);}void handleRightMouseDown(void*,Coord,int){events.emplace_back(3,1);}void handleMiddleMouseDown(void*,Coord,int){events.emplace_back(4,1);}void handleMouseUp(void*,Coord,int){events.emplace_back(1,0);}void handleRightMouseUp(void*,Coord,int){events.emplace_back(3,0);}void handleMiddleMouseUp(void*,Coord,int){events.emplace_back(4,0);}void handleOtherMouseDown(void*,Coord,int,int b){events.emplace_back(5,b);}void handleOtherMouseUp(void*,Coord,int,int b){events.emplace_back(6,b);}void handleScrollWheel(void*,int b){events.emplace_back(7,b);}};
struct Window{bool mUseGL=true;Callbacks*mCallbacks;struct Event{struct Button{int button;}button;}event;Coord openGlCoord;int mask=0;bool isDoubleClick=false;void down(){DOWN}void up(){UP}};
int main(){Callbacks callbacks;Window w;w.mCallbacks=&callbacks;for(bool gl:{false,true})for(int b=4;b<=5;++b){w.mUseGL=gl;w.event.button.button=b;callbacks.events.clear();w.down();w.up();auto expected=gl?std::vector<std::tuple<int,int>>{{7,b==4?-1:1}}:std::vector<std::tuple<int,int>>{{5,b},{6,b}};assert(callbacks.events==expected);}}
