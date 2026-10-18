#include <cassert>
#include <memory>
#include <string>
using F32=float;
namespace VSWorldContracts{enum class AlphaMode{Opaque,Mask,Blend};struct Image{AlphaMode alpha=AlphaMode::Opaque;bool alphaMask=false;};}
struct LLColor4{float mV[4]{1,1,1,1};};struct LLTextureEntry{LLColor4 color;float glow=0;bool fullbright=false;const LLColor4&getColor()const{return color;}float getGlow()const{return glow;}bool getFullbright()const{return fullbright;}};
struct LLPipeline{static inline bool sNoAlpha=false;};bool nonDeferred=false,deferred=false;int gSavedSettings=0;
template<class T>struct LLCachedControl{bool*value=nullptr;LLCachedControl(int,const char*name):value(std::string(name)=="RenderAutoMaskAlphaDeferred"?&deferred:&nonDeferred){}operator T()const{return *value;}};
namespace VSUIRenderer{VSWorldContracts::Image imageDescription(const std::shared_ptr<VSWorldContracts::Image>&i){return *i;}}
POLICY
NATIVE
ORIGINAL
int main()
{
 unsigned cases=0;
 for(unsigned flags=0;flags<256;++flags)for(unsigned present=0;present<2;++present)for(unsigned explicitAlpha=0;explicitAlpha<2;++explicitAlpha)
 for(unsigned sourceAlpha=0;sourceAlpha<3;++sourceAlpha)for(unsigned textureAlpha=0;textureAlpha<3;++textureAlpha)for(unsigned mask=0;mask<2;++mask)
 {
  LLTextureEntry entry;entry.color.mV[3]=(flags&1)?.5f:1.f;entry.glow=(flags&2)?1.f:0.f;entry.fullbright=flags&4;
  const bool hud=flags&8,rigged=flags&16;LLPipeline::sNoAlpha=flags&32;nonDeferred=flags&64;deferred=flags&128;
  auto image=std::make_shared<VSWorldContracts::Image>();image->alpha=VSWorldContracts::AlphaMode(textureAlpha);image->alphaMask=mask;
  if(!present)image.reset();const auto alpha=VSWorldContracts::AlphaMode(sourceAlpha);
  assert(original(entry,image,alpha,explicitAlpha,hud,rigged)==classifyFaceAlpha(entry,image.get(),alpha,explicitAlpha,hud,rigged));++cases;
 }
 assert(cases==18432);
}
