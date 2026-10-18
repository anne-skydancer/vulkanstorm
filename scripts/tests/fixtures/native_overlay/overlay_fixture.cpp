#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
using U64=unsigned long long;using F32=float;
struct LLUUID{int id=0;bool operator==(const LLUUID&)const=default;};
struct LLCoordGL{int mX=0,mY=0;};
struct LLRectf{float mLeft,mTop,mRight,mBottom;LLRectf(float l,float t,float r,float b):mLeft(l),mTop(t),mRight(r),mBottom(b){}};
struct LLColor3{float mV[3]={1,1,1};};
struct LLColor4{float mV[4];LLColor4(float a,float b,float c,float d):mV{a,b,c,d}{}};
template<class T>T llclamp(T x,T low,T high){return std::clamp(x,low,high);}
template<class T>struct Tween{T value;int calls=0;T get(){++calls;return value;}};
namespace VSUIRenderer{struct Texture{unsigned width=2,height=2;std::vector<std::uint8_t> pixels;};using Image=std::shared_ptr<const Texture>;std::array<unsigned,2> imageExtent(const Image&i){assert(i);return{i->width,i->height};}const auto&imagePixels(const Image&i){assert(i);return i->pixels;}}
POLICY
namespace LLRender2D{bool isNativeUI(){return true;}}
struct VSNativeSession{enum class Phase{Connected,Closed};U64 gen=3;Phase state=Phase::Connected;Phase phase()const{return state;}U64 generation()const{return gen;}static std::shared_ptr<VSNativeSession> current;static auto active(){return current;}};std::shared_ptr<VSNativeSession> VSNativeSession::current=std::make_shared<VSNativeSession>();
struct Window{int getWorldViewWidthScaled(){return 200;}int getWorldViewHeightScaled(){return 100;}}window;Window*gViewerWindow=&window;
struct LLVisualEffectParams{};
struct Texture{LLUUID id;int boost=0,stats=0,width=0,height=0,saves=0;LLUUID getID(){return id;}int getBoostLevel(){return boost;}void setBoostLevel(int b){boost=b;}void forceToSaveRawImage(int){++saves;}void addTextureStats(float){++stats;}void setKnownDrawSize(int w,int h){width=w;height=h;}};
namespace LLGLTexture{enum{BOOST_NONE,BOOST_PREVIEW,LOD_TEXTURE};}
namespace LLViewerTexture{enum{LOD_TEXTURE};}
constexpr int FTT_DEFAULT=0,MIPMAP_YES=1;
namespace LLViewerTextureManager{std::shared_ptr<Texture> last;auto getFetchedTexture(LLUUID id,int,int,int,int){last=std::make_shared<Texture>();last->id=id;return last;}}
namespace LLUI{int requests=0;void getUIImageByID(LLUUID){++requests;}}
VSUIRenderer::Image loaded;std::vector<VSUIRenderer::Image> drawn;std::vector<LLColor4> colors;
VSUIRenderer::Image vs_ui_image_snapshot(LLUUID){return loaded;}
void vs_draw_ui_image(const VSUIRenderer::Image&i,const LLRectf&r,const LLColor4&c){assert(r.mLeft==0&&r.mTop==100&&r.mRight==200&&r.mBottom==0);drawn.push_back(i);colors.push_back(c);}
class RlvOverlayEffect{public:std::shared_ptr<Texture>m_pImage;bool m_fBlockTouch=true;int m_nImageOrigBoost=0;U64 mNativeAccount=0;VSUIRenderer::Image mNativePresentedImage;Tween<LLColor3>m_Color;Tween<float>m_nAlpha{.7f};void clearImage();void setImage(const LLUUID&);bool hitTest(const LLCoordGL&)const;void run(const LLVisualEffectParams*);};
CLEAR
SET
bool RlvOverlayEffect::hitTest(const LLCoordGL& ptMouse)const{if(!m_pImage)return false;HIT std::abort();}
void RlvOverlayEffect::run(const LLVisualEffectParams*){RUN std::abort();}
int main(){
 RlvOverlayEffect fx;assert(!fx.hitTest({0,0}));fx.setImage({12});assert(fx.mNativeAccount==3&&fx.m_pImage->boost==LLGLTexture::BOOST_PREVIEW);assert(fx.hitTest({0,0}));
 auto a=std::make_shared<VSUIRenderer::Texture>();a->pixels.assign(16,255);a->pixels[11]=0;loaded=a;
 fx.run(nullptr);assert(drawn.size()==1&&drawn.back()==a&&fx.mNativePresentedImage==a&&colors.back().mV[3]==.7f);assert(!fx.hitTest({0,0}));
 auto b=std::make_shared<VSUIRenderer::Texture>();b->pixels.assign(16,255);loaded=b;assert(!fx.hitTest({0,0})); // queued image has not been drawn
 fx.run(nullptr);assert(fx.hitTest({0,0})&&fx.mNativePresentedImage==b);loaded.reset();fx.run(nullptr);assert(drawn.back()==b); // replacement refusal retains exact accepted image
 fx.m_fBlockTouch=false;assert(!fx.hitTest({0,0}));fx.m_fBlockTouch=true;
 VSNativeSession::current->gen=4;const auto count=drawn.size();assert(!fx.hitTest({0,0}));fx.run(nullptr);assert(drawn.size()==count&&!fx.mNativePresentedImage); // old account cannot draw/rebind
 fx.setImage({12});assert(fx.mNativeAccount==4);loaded=a;fx.run(nullptr);assert(fx.mNativePresentedImage==a);auto old=fx.m_pImage;fx.clearImage();assert(!fx.m_pImage&&!fx.mNativePresentedImage&&!fx.mNativeAccount&&old->boost==0);assert(!fx.hitTest({0,0}));
 fx.setImage({15});VSNativeSession::current->state=VSNativeSession::Phase::Closed;assert(!fx.hitTest({0,0}));fx.run(nullptr);assert(!fx.mNativePresentedImage);
}
