#include <cassert>
#include <array>
#include <string>
#include <vector>
using U8=unsigned char;using S32=int;
enum ESelectType{SELECT_TYPE_WORLD,SELECT_TYPE_HUD,SELECT_TYPE_ATTACHMENT};
namespace REFERENCE{
COMMON
REFERENCE_METHOD
}
namespace CURRENT{
COMMON
SHARED
CURRENT_WRAPPER
}
int main(){
 for(unsigned bits=0;bits<65536;++bits){
  REFERENCE::calls.clear();CURRENT::calls.clear();REFERENCE::bits=CURRENT::bits=bits;
  REFERENCE::LLViewerObject a;CURRENT::LLViewerObject b;REFERENCE::LLSelectMgr x;CURRENT::LLSelectMgr y;
  x.mForceSelection=y.mForceSelection=bits&2;
  const bool r=x.canSelectObject(bits&1?&a:nullptr,bits&4),c=y.canSelectObject(bits&1?&b:nullptr,bits&4);
  assert(r==c);assert(REFERENCE::calls==CURRENT::calls);
 }
}
