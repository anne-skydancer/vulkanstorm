// Actual shared widget methods are injected by test_native_environment_widgets.py.
// Minimal UI ports record routing, commits and drawing; no window, GL or network.
#include <cassert>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
using S32=int;using U32=unsigned;using F32=float;using F64=double;using MASK=int;using KEY=int;
constexpr int VX=0,VY=1,VZ=2,MASK_CONTROL=1,KEY_DOWN=2,KEY_LEFT=3,KEY_UP=4,KEY_RIGHT=5;
constexpr float F_PI=3.14159265359f,F_PI_BY_TWO=F_PI/2,F_TWO_PI=F_PI*2,F_APPROXIMATELY_ZERO=.00001f,F_E=2.7182818f;
template<class T>T llclamp(T v,T lo,T hi){return std::clamp(v,lo,hi);}template<class T>T llabs(T v){return std::abs(v);}
bool is_approx_zero(float v){return std::abs(v)<.00001f;}float llfastpow(float b,float v){return std::pow(b,v);}
struct LLRect {int mLeft=0,mTop=100,mRight=100,mBottom=0;LLRect()=default;LLRect(int l,int t,int r,int b):mLeft(l),mTop(t),mRight(r),mBottom(b){}int getWidth()const{return mRight-mLeft;}int getHeight()const{return mTop-mBottom;}int getCenterX()const{return (mLeft+mRight)/2;}int getCenterY()const{return(mTop+mBottom)/2;}bool pointInRect(int x,int y)const{return x>=mLeft&&x<mRight&&y>=mBottom&&y<mTop;}};
struct Panel{LLRect rect;const LLRect&getRect()const{return rect;}};
struct LLColor4{float mV[4]={1,1,1,1};static LLColor4 red;const LLColor4&get()const{return*this;}LLColor4 operator%(float)const{return*this;}};LLColor4 LLColor4::red;LLColor4 UI_VERTEX_COLOR;
int lines=0,triangles=0,circles=0,rectangles=0,children=0,sounds=0,colorCalls=0;
struct GL{void color4fv(const float*){++colorCalls;}}gGL;
void gl_line_2d(int,int,int,int,LLColor4){++lines;}void gl_triangle_2d(int,int,int,int,int,int,LLColor4,bool){++triangles;}void gl_rect_2d(LLRect,LLColor4,bool){++rectangles;}void gl_circle_2d(float,float,float,int,bool){++circles;}
void make_ui_sound(const char*){++sounds;}
struct LLView{static bool sDebugRects;bool enabled=true,focused=false,chrome=false;int commits=0;virtual~LLView()=default;bool isInEnabledChain()const{return enabled;}bool getEnabled()const{return enabled;}bool getIsChrome()const{return chrome;}void setFocus(bool v){focused=v;}void onCommit(){++commits;}void draw(){++children;}bool handleMouseDown(int,int,int){return false;}bool handleMouseUp(int,int,int){return false;}};bool LLView::sDebugRects=false;
struct Focus{LLView*captor=nullptr;void setMouseCapture(LLView*p){captor=p;}}gFocusMgr;
struct LLUICtrl:LLView{bool hasMouseCapture()const{return gFocusMgr.captor==this;}};
struct LLVector3{float mV[3];};const LLVector3 VectorZero{{1,0,0}};
struct LLQuaternion{float angle=0;LLVector3 direction{{1,0,0}};void setAngleAxis(float a,float x,float y,float z){assert(std::isfinite(a)&&std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z));angle=a;}LLQuaternion&operator*=(const LLQuaternion&q){angle+=q.angle;return*this;}void normalize(){assert(std::isfinite(angle));}};
LLVector3 operator*(const LLVector3&,const LLQuaternion&q){return q.direction;}
struct LLUIImage{int draws=0;int getWidth()const{return 100;}int getHeight()const{return 100;}void draw(LLRect){++draws;}void draw(LLRect,LLColor4){++draws;}};
struct Label{bool visible=true;void setVisible(bool v){visible=v;}};
struct LLVirtualTrackball:LLUICtrl{
 enum ThumbMode{SUN,MOON};enum DragMode{DRAG_SET,DRAG_SCROLL};
 Panel area;Panel*mTouchArea=&area;int mPrevX=0,mPrevY=0;LLQuaternion mValue;ThumbMode mThumbMode=SUN;DragMode mDragMode=DRAG_SET;float mIncrementMouse=.01f,mIncrementBtn=.03f;
 LLUIImage sphere,moonFront,moonBack,sunFront,sunBack;LLUIImage*mImgSphere=&sphere,*mImgMoonFront=&moonFront,*mImgMoonBack=&moonBack,*mImgSunFront=&sunFront,*mImgSunBack=&sunBack;
 Label labels[4];Label*mLabelN=&labels[0],*mLabelE=&labels[1],*mLabelS=&labels[2],*mLabelW=&labels[3];
 bool pointInTouchCircle(S32,S32)const;void draw();void drawThumb(S32,S32,ThumbMode,bool);bool handleHover(S32,S32,MASK);bool handleMouseDown(S32,S32,MASK);bool handleMouseUp(S32,S32,MASK);bool handleKeyHere(KEY,MASK);void onRotateTopClick();void onRotateBottomClick();void onRotateLeftClick();void onRotateRightClick();void setValueAndCommit(const LLQuaternion&v){mValue=v;onCommit();}static void getAzimuthAndElevation(const LLQuaternion&,F32&,F32&);
};
TRACKBALL_METHODS
struct EditorValue{float value=0;float asReal()const{return value;}};
struct Editor{EditorValue value;EditorValue getValue()const{return value;}};
struct LLXYVector:LLUICtrl{
 Editor xEntry,yEntry;Editor*mXEntry=&xEntry,*mYEntry=&yEntry;
 Panel area;Panel*mTouchArea=&area;float mValueX=0,mValueY=0,mMaxValueX=1,mMaxValueY=1,mIncrementX=.05f,mIncrementY=.05f,mLogScaleX=1,mLogScaleY=1;bool mLogarithmic=false;int mGhostX=0,mGhostY=0;LLColor4 mAreaColor,mGridColor,mGhostColor,mArrowColor;
 void draw();void onEditChange();bool handleHover(S32,S32,MASK);bool handleMouseDown(S32,S32,MASK);bool handleMouseUp(S32,S32,MASK);void setValueAndCommit(float x,float y){assert(std::isfinite(x)&&std::isfinite(y));mValueX=x;mValueY=y;onCommit();}
};
constexpr int ARROW_LENGTH_LONG=10,ARROW_LENGTH_SHORT=3,ARROW_ANGLE=30,CENTER_CIRCLE_RADIUS=2;
XY_METHODS
int main(){
 LLVirtualTrackball t;
 assert(t.handleMouseDown(50,50,0)&&t.focused&&t.hasMouseCapture());
 t.handleHover(50,50,0);assert(t.commits==1); // exact center formerly acos(0/0)
 t.handleHover(100,50,0);assert(t.commits==2); // exact rim remains finite
 t.handleHover(101,50,0);assert(t.commits==2); // outside rejected
 assert(t.handleMouseUp(1000,1000,0)&&!t.hasMouseCapture());
 assert(!t.handleMouseDown(1000,1000,0));
 assert(t.handleMouseDown(50,50,MASK_CONTROL));t.handleHover(60,60,0);assert(t.commits==3);assert(t.handleMouseUp(60,60,0));
 int before=t.commits;for(int k:{KEY_LEFT,KEY_RIGHT,KEY_UP,KEY_DOWN})assert(t.handleKeyHere(k,0));assert(t.commits==before+4);assert(!t.handleKeyHere(999,0));
 assert(t.handleMouseDown(50,50,0));t.enabled=false;t.handleHover(60,60,0);assert(!t.hasMouseCapture());before=t.commits;assert(!t.handleKeyHere(KEY_LEFT,0));t.onRotateTopClick();assert(t.commits==before);
 t.draw();for(auto&label:t.labels)assert(!label.visible);t.enabled=true;
 for(auto mode:{LLVirtualTrackball::SUN,LLVirtualTrackball::MOON})for(float z:{-1.f,1.f}){t.mThumbMode=mode;t.mValue.direction.mV[2]=z;t.draw();}
 assert(t.sunFront.draws&&t.sunBack.draws&&t.moonFront.draws&&t.moonBack.draws);
 LLView::sDebugRects=true;int old=circles;t.draw();assert(circles==old+2);LLView::sDebugRects=false;
 t.area.rect={0,0,0,0};assert(!t.pointInTouchCircle(0,0));assert(!t.handleMouseDown(0,0,0));
 LLXYVector xy;xy.xEntry.value.value=.2f;xy.yEntry.value.value=-.3f;xy.onEditChange();assert(xy.mValueX==.2f&&xy.mValueY==-.3f);xy.enabled=false;before=xy.commits;xy.onEditChange();assert(xy.commits==before);xy.enabled=true;xy.mValueX=xy.mValueY=0;old=circles;int l=lines,r=rectangles;xy.draw();assert(circles==old+1&&lines==l+2&&rectangles==r+1);
 xy.mValueX=.5f;old=triangles;xy.draw();assert(triangles==old+1);assert(xy.handleMouseDown(20,20,0));old=triangles;xy.draw();assert(triangles==old+2);xy.handleHover(75,25,0);assert(xy.mValueX==.5f&&xy.mValueY==-.5f);assert(xy.handleMouseUp(-100,-100,0)&&!xy.hasMouseCapture());
 xy.mLogarithmic=true;xy.mLogScaleX=xy.mLogScaleY=.01f;assert(xy.handleMouseDown(30,30,0));xy.handleHover(51,49,0);assert(xy.mValueX>0&&xy.mValueY<0);xy.enabled=false;before=xy.commits;xy.handleHover(60,60,0);assert(!xy.hasMouseCapture()&&xy.commits==before);
 xy.enabled=true;xy.area.rect={0,0,0,0};before=xy.commits;gFocusMgr.setMouseCapture(&xy);xy.handleHover(0,0,0);assert(!xy.hasMouseCapture()&&xy.commits==before);old=circles;l=children;xy.draw();assert(circles==old&&children==l+1);
}
