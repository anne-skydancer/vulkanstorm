#include <cassert>
struct V2{float a,b;V2(float x,float y):a(x),b(y){}};struct C2{int x,y;C2(int a,int b):x(a),y(b){}};
constexpr int MASK_NONE=0;
struct LLPickInfo{enum{PICK_INVALID};int mKeyMask;void(*mPickCallback)();int mPickType;bool mWantSurfaceInfo;int mObjectFace;V2 mUVCoords,mSTCoords;C2 mXYCoords;int mIntersection,mNormal,mTangent,mBinormal;void*mHUDIcon;bool mPickTransparent,mPickRigged,mPickParticle,mPickUnselectable;LLPickInfo();};
BODY
int main(){for(int n=0;n<100;n++){LLPickInfo pick;assert(!pick.mPickUnselectable&&!pick.mPickRigged&&!pick.mPickTransparent&&!pick.mPickParticle);}}
