
#include <vector>
#include <algorithm>
#include <cassert>
#include <random>
#include <cmath>
#include <limits>
#include <cstring>
using U8=unsigned char;using U16=unsigned short;using U32=unsigned;using S32=int;using F32=float;
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE
#define LL_LIKELY(v) (v)
#define LL_UNLIKELY(v) (v)
#define llassert assert
#define SHOW_ASSERT
#define GL_UNSIGNED_BYTE 1
#define GL_RGBA 2
#define GL_SRGB_ALPHA 3
struct Log{template<class T>Log&operator<<(T){return *this;}};
#define LL_WARNS_ONCE(v) Log{}
#define LL_ENDL 0
S32 llfloor(float v){return int(std::floor(v));}template<class T>T llmax(T a,T b){return std::max(a,b);}
struct LLVector2{float mV[2];bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1]);}};
struct LLImageGL{U8*mPickMask=nullptr;U16 mPickMaskWidth=0,mPickMaskHeight=0;bool mNeedsAlphaAndPickMask=true;unsigned mFormatType=GL_UNSIGNED_BYTE,mFormatPrimary=GL_RGBA;U32 createPickMask(S32,S32);void freePickMask();void updatePickMask(S32,S32,const U8*);bool getMask(const LLVector2&)const;~LLImageGL(){freePickMask();}};
REFERENCE
// Original cursor/volume picking policies with lazy renderer-peer inputs. LGPL-2.1.
POLICY
int main(){std::mt19937 rng(33);std::uniform_real_distribution<float>uv(-1000,1000);for(unsigned width=1;width<80;width++)for(unsigned height=1;height<50;height++){
std::vector<U8>bottom(width*height*4),top(bottom.size());for(auto&b:bottom)b=U8(rng());for(unsigned y=0;y<height;y++)std::copy_n(bottom.data()+y*width*4,width*4,top.data()+(height-1-y)*width*4);
LLImageGL reference;reference.updatePickMask(width,height,bottom.data());for(unsigned n=0;n<100;n++){LLVector2 point{{uv(rng),uv(rng)}};if(n==0)point.mV[0]=std::numeric_limits<float>::quiet_NaN();assert(reference.getMask(point)==VSPickPolicy::textureMask(width,height,top,point.mV[0],point.mV[1]));}}
}
