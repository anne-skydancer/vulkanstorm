#include <array>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <random>
#include <cmath>
#include <cassert>
#include <stdexcept>
using F32=float;using S32=int;using U32=unsigned;constexpr F32 DEG_TO_RAD=3.14159265358979323846f/180;
struct LLColor4{float mV[4];LLColor4(float r=1,float g=1,float b=1,float a=1):mV{r,g,b,a}{};void setVec(const float* p){std::copy_n(p,4,mV);}static const LLColor4 white;};const LLColor4 LLColor4::white{};
struct LLRect {S32 mLeft=0,mTop=720,mRight=1280,mBottom=0;S32 getWidth()const{return mRight-mLeft;}S32 getHeight()const{return mTop-mBottom;}void stretch(int n){mLeft-=n;mBottom-=n;mRight+=n;mTop+=n;}};
struct LLViewerTexture{int id=0,w=512,h=512;int getWidth(){return w;}int getHeight(){return h;}};
template<class T>struct Ptr{T* p=nullptr;Ptr()=default;Ptr(T* p):p(p){};bool notNull()const{return p!=nullptr;}T*operator->()const{return p;}operator T*()const{return p;}};
struct LLUIImage;using LLUIImagePtr=Ptr<LLUIImage>;
struct Vertex {int image;std::array<float,8> v;};std::vector<Vertex> actual;
std::array<float,2> parentOffset{};
struct Recorder {
 std::array<F32,6> transform{1,0,0,1,0,0};std::vector<std::array<F32,6>> stack;LLColor4 color;F32 u=0,v=0;int image=0;
 Recorder* getTexUnit(int){return this;}void bind(LLViewerTexture* p){image=p->id;}void unbind(int){image=0;}
 void color4fv(const float*p){color.setVec(p);}void color4f(float r,float g,float b,float a){color={r,g,b,a};}
 void pushMatrix(){stack.push_back(transform);}void popMatrix(){assert(!stack.empty());transform=stack.back();stack.pop_back();}
 void translatef(F32 x,F32 y,F32){transform[4]+=transform[0]*x+transform[2]*y;transform[5]+=transform[1]*x+transform[3]*y;}
 void rotatef(F32 angle,F32,F32,F32){const auto p=transform;const float c=std::cos(angle*DEG_TO_RAD),s=std::sin(angle*DEG_TO_RAD);transform[0]=p[0]*c+p[2]*s;transform[1]=p[1]*c+p[3]*s;transform[2]=p[2]*c-p[0]*s;transform[3]=p[3]*c-p[1]*s;}
 void begin(int){}void end(){}void texCoord2f(float a,float b){u=a;v=b;}
 void vertex2i(int x,int y){actual.push_back({image,{transform[0]*x+transform[2]*y+transform[4],transform[1]*x+transform[3]*y+transform[5],u,v,color.mV[0],color.mV[1],color.mV[2],color.mV[3]}});}
}gGL;
struct LLRender{enum{TRIANGLES=1};};struct LLTexUnit{enum{TT_TEXTURE=0};};
struct LLRender2D {
 static inline bool native=false;using native_triangle_t=std::array<std::array<float,6>,3>;
 static bool isNativeUI(){return native;}static void setNativeColor(const LLColor4&c){gGL.color=c;}
 static void nativeTriangle(const native_triangle_t& t){for(const auto&v:t)actual.push_back({0,{v[0]+parentOffset[0],v[1]+parentOffset[1],0,0,v[2],v[3],v[4],v[5]}});}
};
struct LLUIImage{
 using native_triangles_t=std::array<std::array<float,8>,6>;int id=0,w=512,h=512;
 int getWidth(){return w;}int getHeight(){return h;}
 void drawNativeTriangles(const native_triangles_t&t){for(auto v:t){v[0]+=parentOffset[0];v[1]+=parentOffset[1];actual.push_back({id,v});}}
};
PAINTER
struct Timer{bool started=false;float elapsed=0;bool getStarted()const{return started;}float getElapsedTimeF32()const{return elapsed;}void start(){started=true;elapsed=0;}void stop(){started=false;}};
struct Window{int getWindowWidthScaled()const{return 1280;}int getWindowHeightScaled()const{return 720;}}window;Window*gViewerWindow=&window;
struct DummyLog{template<class T>DummyLog&operator<<(const T&){return*this;}};
#define LL_DEBUGS(...) DummyLog()
#define LL_ENDL 0
constexpr float SHINE_TIME=.5f,SHINE_WIDTH=.6f,SHINE_OPACITY=.3f,FALL_TIME=.6f;constexpr int BORDER_WIDTH=6,TOP_PANEL_HEIGHT=30;
template<class T>T llmin(T a,T b){return std::min(a,b);}int ll_round(float x){return int(std::floor(x+.5f));}
float clamp_rescale(float v,float a,float b,float c,float d){return c+std::clamp((v-a)/(b-a),0.f,1.f)*(d-c);}float lerp(float a,float b,float t){return a+(b-a)*t;}
struct LLCriticalDamp{static float getInterpolant(float){return .3f;}};
struct LLLocalClipRect{explicit LLLocalClipRect(LLRect){}};
std::vector<std::array<float,8>> rectangles;
void gl_rect_2d(const LLRect&r,const LLColor4&c){gGL.color=c;rectangles.push_back({float(r.mLeft),float(r.mTop),float(r.mRight),float(r.mBottom),c.mV[0],c.mV[1],c.mV[2],c.mV[3]});}
void gl_rect_2d(const LLRect&r){gl_rect_2d(r,gGL.color);}void gl_drop_shadow(int l,int t,int r,int b,const LLColor4&c,int){gl_rect_2d(LLRect{l,t,r,b},c);}
struct Fields {
 int mCurImageIndex=0;LLRect mImageRect[2]{{10,710,1270,10},{25,680,1200,30}};
 LLViewerTexture gl[2]{{1,512,1024},{2,1024,512}};LLUIImage vk[2]{{1,512,1024},{2,1024,512}};
 Ptr<LLViewerTexture> mViewerImage[2];struct Native{LLUIImagePtr image;};Native mNativeImages[2];Ptr<int> mPreviewImageEncoded;
 bool scaled=false,ready=true,mImageScaled[2]{false,false};int mWidth[2]{400,800},mHeight[2]{700,400};bool mNeedsFlash=false;float mFlashAlpha=.2f;
 Timer mShineAnimTimer,mFallAnimTimer;int mShineCountdown=0;int nonnull=1;
 Fields(){bind();}void bind(){for(int i=0;i<2;++i){mViewerImage[i]=&gl[i];mNativeImages[i].image=&vk[i];}mPreviewImageEncoded=&nonnull;}
 auto getCurrentImage(){return mViewerImage[mCurImageIndex];}LLRect getRect()const{return {};}LLRect getImageRect()const{return mImageRect[mCurImageIndex];}
 bool isImageScaled()const{return scaled;}int getWidth()const{return mWidth[mCurImageIndex];}int getHeight()const{return mHeight[mCurImageIndex];}bool getSnapshotUpToDate()const{return ready;}
 bool hasImage(U32 i)const{return mViewerImage[i].notNull();}int imageWidth(U32 i)const{return gl[i].w;}int imageHeight(U32 i)const{return gl[i].h;}
};
struct ReferenceSnapshot:Fields{void draw();};struct LLSnapshotLivePreview:Fields{void draw();};
REFERENCE
CURRENT
int main(){
 std::mt19937 rng(944603);std::uniform_real_distribution<float>n(0,1);
 for(unsigned trial=0;trial<12000;++trial){
  ReferenceSnapshot reference;reference.mCurImageIndex=trial%2;reference.scaled=trial%3==0;reference.mNeedsFlash=trial%2;reference.mFlashAlpha=n(rng);reference.mShineCountdown=trial%5;
  reference.mShineAnimTimer={bool(trial%3),n(rng)};reference.mFallAnimTimer={bool(trial%4),n(rng)};reference.mImageScaled[0]=trial%7;reference.mImageScaled[1]=trial%11;reference.ready=trial%19;
  LLSnapshotLivePreview current;static_cast<Fields&>(current)=static_cast<const Fields&>(reference);current.bind();
  parentOffset={float(trial%150),float(trial%99)};gGL={};gGL.transform[4]=parentOffset[0];gGL.transform[5]=parentOffset[1];actual.clear();rectangles.clear();LLRender2D::native=false;reference.draw();auto expected=actual;auto expectedRects=rectangles;
  gGL={};actual.clear();rectangles.clear();LLRender2D::native=true;current.draw();
  assert(expected.size()==actual.size()&&rectangles==expectedRects);
  for(unsigned i=0;i<actual.size();++i){assert(expected[i].image==actual[i].image);for(unsigned c=0;c<8;++c){if(expected[i].image==0&&(c==2||c==3))continue;assert(std::abs(expected[i].v[c]-actual[i].v[c])<.001f);}}
  assert(current.mShineCountdown==reference.mShineCountdown&&current.mNeedsFlash==reference.mNeedsFlash&&current.mFlashAlpha==reference.mFlashAlpha);assert(current.mShineAnimTimer.started==reference.mShineAnimTimer.started);
 }
}
