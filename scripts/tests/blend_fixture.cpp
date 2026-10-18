
#include <array>
#include <cassert>
#include <cmath>
#include <optional>
#include <random>
using COLOR_MASK=unsigned;
enum BLEND_FACTOR {BLEND_FACTOR_ONE=93,BLEND_FACTOR_ZERO=42,BLEND_FACTOR_DEST_COLOR=17,BLEND_FACTOR_SRC_COLOR=65,BLEND_FACTOR_INV_DEST_COLOR=12,BLEND_FACTOR_INV_SRC_COLOR=86,BLEND_FACTOR_DEST_ALPHA=45,BLEND_FACTOR_SRC_ALPHA=62,BLEND_FACTOR_INV_DEST_ALPHA=31,BLEND_FACTOR_INV_SRC_ALPHA=77};
struct State{BLEND_FACTOR SrcBlend,DestBlend,SrcBlendAlpha,DestBlendAlpha;unsigned RenderTargetWriteMask;};
struct Blend{unsigned source,destination,sourceAlpha,destinationAlpha,writeMask;};
State native(const std::optional<Blend>& customBlend){State blend{};NATIVE;return blend;}
float factor(BLEND_FACTOR f,float s,float d,float sa,float da){switch(f){case BLEND_FACTOR_ONE:return 1;case BLEND_FACTOR_ZERO:return 0;case BLEND_FACTOR_DEST_COLOR:return d;case BLEND_FACTOR_SRC_COLOR:return s;case BLEND_FACTOR_INV_DEST_COLOR:return 1-d;case BLEND_FACTOR_INV_SRC_COLOR:return 1-s;case BLEND_FACTOR_DEST_ALPHA:return da;case BLEND_FACTOR_SRC_ALPHA:return sa;case BLEND_FACTOR_INV_DEST_ALPHA:return 1-da;case BLEND_FACTOR_INV_SRC_ALPHA:return 1-sa;}assert(false);return -99;}
float gold(unsigned f,float s,float d,float sa,float da){const float values[]{1,0,d,s,1-d,1-s,da,sa,1-da,1-sa};return values[f];}
int main(){std::mt19937 r(1947);std::uniform_real_distribution<float>v(0,1);for(unsigned a=0;a<10;++a)for(unsigned b=0;b<10;++b)for(unsigned c=0;c<10;++c)for(unsigned d=0;d<10;++d)for(unsigned mask=0;mask<16;++mask){const Blend input{a,b,c,d,mask};const auto state=native(input);assert(state.RenderTargetWriteMask==mask);for(unsigned channel=0;channel<4;++channel){const float source=v(r),dest=v(r),sa=v(r),da=v(r);const float ns=channel==3?sa:source,nd=channel==3?da:dest;const auto sf=channel==3?state.SrcBlendAlpha:state.SrcBlend,df=channel==3?state.DestBlendAlpha:state.DestBlend;const float result=(mask&(1u<<channel))?ns*factor(sf,ns,nd,sa,da)+nd*factor(df,ns,nd,sa,da):nd;const float expected=(mask&(1u<<channel))?ns*gold(channel==3?c:a,ns,nd,sa,da)+nd*gold(channel==3?d:b,ns,nd,sa,da):nd;assert(result==expected);}}
// Original particle base-alpha suppresses glow; emissive pass writes only
// texture-alpha * quantized glow to alpha using ONE/ONE, leaving RGB untouched.
const auto base=native(Blend{7,9,1,9,15});assert(base.SrcBlendAlpha==BLEND_FACTOR_ZERO&&base.DestBlendAlpha==BLEND_FACTOR_INV_SRC_ALPHA);const auto glow=native(Blend{1,0,0,0,8});assert(glow.RenderTargetWriteMask==8&&glow.SrcBlendAlpha==BLEND_FACTOR_ONE&&glow.DestBlendAlpha==BLEND_FACTOR_ONE);
}
