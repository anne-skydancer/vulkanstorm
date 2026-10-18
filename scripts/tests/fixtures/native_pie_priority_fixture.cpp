#include <vector>
#include <random>
#include <cassert>
#include <algorithm>
using U8=unsigned char;using MASK=int;constexpr int MASK_NONE=0,CLICK_ACTION_DISABLED=255;
struct LLPrimitive{static bool isPrimitive(U8 p){return p==9;}};
std::vector<int> trace;
struct LLViewerObject{int id;bool attach,touch;U8 pcode,action;LLViewerObject*root;LLViewerObject*getRootEdit(){trace.push_back(10+id);return root;}bool isAttachment(){trace.push_back(20+id);return attach;}U8 getPCode(){trace.push_back(30+id);return pcode;}U8 getClickAction(){trace.push_back(40+id);return action;}bool flagHandleTouch(){trace.push_back(50+id);return touch;}};
POLICY
PEER
USE
REFERENCE
int main(){std::mt19937 rng(775);LLViewerObject a{1},b{2},pa{3},pb{4};const U8 actions[]={0,1,2,255};for(int iteration=0;iteration<80000;++iteration){for(auto*o:{&a,&b,&pa,&pb}){o->attach=rng()%2;o->touch=rng()%2;o->pcode=(rng()%2)?9:47;o->action=actions[rng()%4];}a.root=rng()%3?&pa:nullptr;b.root=rng()%3?&pb:nullptr;auto*transparent=rng()%5?&a:nullptr;auto*visible=rng()%5?&b:nullptr;if(rng()%8==0)visible=transparent;int mask=rng()%3;trace.clear();int expected=reference(mask,transparent,visible);auto expectedTrace=trace;trace.clear();bool actual=vs_pie_prefer_visible(ReferencePiePriority{},mask,transparent,visible);assert(actual==bool(expected));assert(trace==expectedTrace);}}
