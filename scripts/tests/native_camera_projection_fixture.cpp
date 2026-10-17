#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <array>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
using F32=float;using U32=unsigned;using S32=int;
constexpr int VX=0,VY=1,VZ=2,VW=3,NUM_VALUES_IN_MAT4=4;constexpr float F_PI=3.14159265358979323846f;
template<class T>T llclamp(T v,T a,T b){return std::clamp(v,a,b);}int lltrunc(float v){return int(v);}bool finiteVector(const struct LLVector3&);
struct LLVector3 {
 float mV[3]{};LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{};operator glm::vec3()const{return {mV[0],mV[1],mV[2]};}
 void setVec(float x,float y,float z){mV[0]=x;mV[1]=y;mV[2]=z;}
 bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1])&&std::isfinite(mV[2]);}
 float magVecSquared()const{return *this**this;}float magVec()const{return std::sqrt(magVecSquared());}
 float normalize(){float n=magVec();if(n>0)*this/=n;return n;}
 LLVector3 operator-(const LLVector3& v)const{return {mV[0]-v.mV[0],mV[1]-v.mV[1],mV[2]-v.mV[2]};}
 LLVector3 operator+(const LLVector3& v)const{return {mV[0]+v.mV[0],mV[1]+v.mV[1],mV[2]+v.mV[2]};}
 LLVector3 operator*(float v)const{return {mV[0]*v,mV[1]*v,mV[2]*v};}
 float operator*(const LLVector3& v)const{return mV[0]*v.mV[0]+mV[1]*v.mV[1]+mV[2]*v.mV[2];}
 LLVector3 operator%(const LLVector3& v)const{return {mV[1]*v.mV[2]-mV[2]*v.mV[1],mV[2]*v.mV[0]-mV[0]*v.mV[2],mV[0]*v.mV[1]-mV[1]*v.mV[0]};}
 LLVector3& operator/=(float v){for(auto& x:mV)x/=v;return *this;}
};
bool finiteVector(const LLVector3& v){return v.isFinite();}
struct LLMatrix4 {
 float mMatrix[4][4];LLMatrix4(){setZero();for(int i=0;i<4;++i)mMatrix[i][i]=1;}LLMatrix4(const float* v){std::memcpy(mMatrix,v,sizeof mMatrix);}
 void setZero(){std::memset(mMatrix,0,sizeof mMatrix);}
 void setFwdCol(const LLVector3&);void setLeftCol(const LLVector3&);void setUpCol(const LLVector3&);
};
MATRIX_METHODS
struct LLCoordGL{int mX=0,mY=0;};
struct LLRect{int mLeft=0,mTop=600,mRight=800,mBottom=0;int getWidth()const{return mRight-mLeft;}int getHeight()const{return mTop-mBottom;}int getCenterX()const{return (mLeft+mRight)/2;}int getCenterY()const{return (mTop+mBottom)/2;}};
struct Scale{float mV[2]{1,1};};
struct Window{bool native=true;LLRect raw;Scale scale;bool isNativeVulkan()const{return native;}LLRect getWorldViewRectRaw()const{return raw;}const Scale& getDisplayScale()const{return scale;}LLRect getWorldViewRectScaled()const{return {int(raw.mLeft/scale.mV[0]),int(raw.mTop/scale.mV[1]),int(raw.mRight/scale.mV[0]),int(raw.mBottom/scale.mV[1])};}}window;
Window* gViewerWindow=&window;int gGLViewport[4]={0,0,0,0},glCalls=0;
glm::mat4 get_current_modelview(){++glCalls;return glm::mat4(1);}glm::mat4 get_current_projection(){++glCalls;return glm::mat4(1);}
CONSTANTS
struct LLViewerCamera {
 LLVector3 mOrigin{321,654,30},mXAxis{1,0,0},mYAxis{0,1,0},mZAxis{0,0,1};mutable LLMatrix4 mProjectionMatrix,mModelviewMatrix;
 float view=F_PI/3,aspect=4.f/3,nearClip=.1f,farClip=128;
 float getView()const{return view;}float getAspect()const{return aspect;}float getNear()const{return nearClip;}float getFar()const{return farClip;}
 const LLVector3& getOrigin()const{return mOrigin;}const LLVector3& getAtAxis()const{return mXAxis;}
 void getMatrixToLocal(LLMatrix4&)const;const LLMatrix4& getModelview()const;const LLMatrix4& getProjection()const;void calcProjection(float)const;
 void projectScreenToPosAgent(S32,S32,LLVector3*)const;bool projectPosAgentToScreen(const LLVector3&,LLCoordGL&,bool)const;bool projectPosAgentToScreenEdge(const LLVector3&,LLCoordGL&)const;
};
struct VSWorldRenderer{struct View{std::array<float,16> projection{};};};
struct VSWorldScene{static VSWorldRenderer::View cameraView(const LLVector3&,const LLVector3&,const LLVector3&,unsigned,unsigned,float);};
CAMERA_METHODS
NATIVE_VIEW
PROJECTION_METHODS
int main(){
 LLViewerCamera camera;
 for(float scale:{1.f,1.5f,2.f})for(int direction=0;direction<4;++direction){
  window.raw={0,600,800,0};window.scale.mV[0]=window.scale.mV[1]=scale;
  if(direction==0){camera.mXAxis={1,0,0};camera.mYAxis={0,1,0};camera.mZAxis={0,0,1};}
  else if(direction==1){camera.mXAxis={0,1,0};camera.mYAxis={-1,0,0};camera.mZAxis={0,0,1};}
  else {
   camera.mXAxis={1,.5f,direction==2?.3f:-.3f};camera.mXAxis.normalize();
   camera.mYAxis=LLVector3(0,0,1)%camera.mXAxis;camera.mYAxis.normalize();camera.mZAxis=camera.mXAxis%camera.mYAxis;
   if(direction==3){camera.mYAxis=camera.mYAxis*std::cos(.2f)+camera.mZAxis*std::sin(.2f);camera.mZAxis=camera.mXAxis%camera.mYAxis;}
  }
  const auto native=VSWorldScene::cameraView(camera.mOrigin,camera.mXAxis,camera.mZAxis,800,600,128);
  for(auto offset:std::array<LLVector3,5>{{{0,0,0},{2,0,0},{-2,0,0},{0,2,0},{0,-2,0}}}){
   auto point=camera.mOrigin+camera.mXAxis*20+camera.mYAxis*offset.mV[0]+camera.mZAxis*offset.mV[1];
   LLCoordGL screen;assert(camera.projectPosAgentToScreen(point,screen,true));
   auto clip=glm::make_mat4(native.projection.data())*glm::vec4(glm::vec3(point),1);auto ndc=glm::vec3(clip)/clip.w;
   assert(std::abs(screen.mX-(ndc.x+1)*400/scale)<1.01f && std::abs(screen.mY-(ndc.y+1)*300/scale)<1.01f);
   LLVector3 ray;camera.projectScreenToPosAgent(int(screen.mX*scale),int(screen.mY*scale),&ray);auto actual=ray-camera.mOrigin;actual.normalize();auto desired=point-camera.mOrigin;desired.normalize();assert(actual*desired>.9999f);
  }
  LLCoordGL edge;auto left=camera.mOrigin+camera.mXAxis*20+camera.mYAxis*100;
  assert(!camera.projectPosAgentToScreen(left,edge,true) && edge.mX==0);
  assert(camera.projectPosAgentToScreenEdge(left,edge) && edge.mX==0);
  assert(!camera.projectPosAgentToScreen(camera.mOrigin,edge,true));
  assert(!camera.projectPosAgentToScreenEdge(camera.mOrigin,edge));
 }
 assert(glCalls==0);window.raw.mRight=0;LLCoordGL screen;assert(!camera.projectPosAgentToScreen(camera.mOrigin+camera.mXAxis,screen,true));
 window.native=false;window.raw.mRight=800;camera.projectPosAgentToScreen(camera.mOrigin+camera.mXAxis,screen,false);assert(glCalls>0);
}
