// Actual native beacon publisher/line encoder, with CPU recording resource ports.
#include <cassert>
#include <array>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>
#include <string>
#include <algorithm>
#include <initializer_list>
#include <cmath>
#include <map>
#include <set>
using U8=unsigned char;using S32=int;using U64=unsigned long long;template<class T>T llclamp(T v,T lo,T hi){return std::clamp(v,lo,hi);}
struct LLVector3{float mV[3]={};static const LLVector3 zero;LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{}explicit LLVector3(const float*p):mV{p[0],p[1],p[2]}{}bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1])&&std::isfinite(mV[2]);}LLVector3 scaledVec(const LLVector3&v)const{return{mV[0]*v.mV[0],mV[1]*v.mV[1],mV[2]*v.mV[2]};}LLVector3 operator+(const LLVector3&v)const{return{mV[0]+v.mV[0],mV[1]+v.mV[1],mV[2]+v.mV[2]};}LLVector3 operator-(const LLVector3&v)const{return{mV[0]-v.mV[0],mV[1]-v.mV[1],mV[2]-v.mV[2]};}bool operator!=(const LLVector3&v)const{return !(*this==v);}bool operator==(const LLVector3&v)const{return mV[0]==v.mV[0]&&mV[1]==v.mV[1]&&mV[2]==v.mV[2];}};const LLVector3 LLVector3::zero;using F32=float;constexpr int VX=0,VY=1,VZ=2;struct LLVector3d{double mdV[3]={};};
struct LLColor4{float mV[4]={1,1,1,1};LLColor4()=default;LLColor4(float r,float g,float b,float a):mV{r,g,b,a}{}LLColor4(std::initializer_list<float>v){std::copy(v.begin(),v.end(),mV);}};LLColor4 linearColor4(LLColor4 v){return v;}
struct Beacon{LLVector3 mPositionAgent;LLColor4 mColor;int mLineWidth=4;std::string mString;LLColor4 mTextColor;};using LLDebugBeacon=Beacon;std::vector<Beacon> inputs;
struct ObjectList{std::vector<Beacon>queue;void resetObjectBeacons(){queue.clear();}const std::vector<Beacon>&debugBeacons()const{return queue;}void addDebugBeacon(const LLVector3&p,const std::string&s,const LLColor4&c,const LLColor4&t,int w){queue.push_back({p,c,w,s,t});}}gObjectList;
bool active=true;struct LLFloaterTelehub{static bool renderBeacons(){return active;}static void addBeacons(){gObjectList.queue=inputs;}};
struct VSNativeSession{struct AccountHandle{int generation=0;};int generation=1;bool current(AccountHandle a)const{return a.generation==generation;}AccountHandle account()const{return{generation};}};
struct VSWorldRenderer{struct Vertex{float x,y,z,nx,ny,nz,u,v;};using Mesh=std::shared_ptr<int>;struct Packet{Mesh mesh;std::array<float,4>color{};bool fullbright=false,alpha=false,overlay=false,depthTest=true,depthWriteAlpha=false,hud=false;std::optional<bool>depthWrite;float screenLinePixels=0;std::array<float,4>regionOffset{};};int serial=0,refuseAt=-1;bool publishMesh(Mesh&mesh,const std::vector<Vertex>&v,const std::vector<unsigned>&i){++serial;assert((v.size()==90&&i.size()==90)||(v.size()==42&&i.size()==42));if(serial==refuseAt)return false;mesh=std::make_shared<int>(serial);return true;}};
struct LLHUDObject{enum{LL_HUD_TEXT};virtual~LLHUDObject()=default;static LLHUDObject*addHUDObject(int);};
struct LLHUDText:LLHUDObject{bool dead=false,z=true;std::string text;LLColor4 color;LLVector3 position;int edits=0;void markDead(){dead=true;}void setZCompare(bool b){z=b;}void setString(const std::string&s){text=s;++edits;}void setColor(LLColor4 c){color=c;}void setPositionAgent(LLVector3 p){position=p;}};
std::vector<std::unique_ptr<LLHUDText>>labels;LLHUDObject*LLHUDObject::addHUDObject(int){labels.emplace_back(std::make_unique<LLHUDText>());return labels.back().get();}
template<class T>struct LLPointer{T*p=nullptr;operator bool()const{return p!=nullptr;}T*operator->(){return p;}LLPointer&operator=(T*v){p=v;return*this;}};
struct Sky{LLVector3 sun{1,0,0},moon{0,1,0};LLVector3 getSunDirection(){return sun;}LLVector3 getMoonDirection(){return moon;}};struct LLEnvironment{std::shared_ptr<Sky>sky;static LLEnvironment&instance(){static LLEnvironment v;return v;}auto getCurrentSky(){return sky;}};
struct Settings{bool sun=false,moon=false;bool getBOOL(const char*n){return std::string(n)=="sunbeacon"?sun:moon;}}gSavedSettings;struct LLPipeline{static constexpr int DebugBeaconLineWidth=3;static bool getRenderSoundBeacons(){return false;}static bool getRenderBeacons(){return false;}static bool getRenderRegionCornerBeacons(){return false;}};
struct LLAudioEngine{std::vector<LLVector3d>sourcePositions()const{return{};}}*gAudiop=nullptr;struct LLAgent{LLVector3 getPosAgentFromGlobal(const LLVector3d&g)const{return{float(g.mdV[0]),float(g.mdV[1]),float(g.mdV[2])};}}gAgent;
struct VSRegionContext{F32 getWidth()const{return 256;}F32 getHeight()const{return 256;}LLVector3d getPosGlobalFromRegion(const LLVector3&p)const{return{{p.mV[0],p.mV[1],p.mV[2]}};}};std::optional<VSRegionContext>vs_current_region_context(){return std::nullopt;}std::optional<F32>vs_region_height(const LLVector3d&){return std::nullopt;}
struct VSWorldScene{struct DebugBox{std::array<std::array<float,3>,2>bounds;std::array<float,4>color;F32 width=1;bool depthTest=true,hud=false;};void appendObjectBeacons(std::vector<VSWorldRenderer::Packet>&,std::vector<DebugBox>&){}bool allow=true,agent=false;LLVector3 position{10,20,30};bool agentPosition(LLVector3&p){p=position;return agent;}
std::shared_ptr<int>reserveSpatialBytes(std::size_t n){assert(n==180*(sizeof(VSWorldRenderer::Vertex)+sizeof(unsigned))||n==42*(sizeof(VSWorldRenderer::Vertex)+sizeof(unsigned)));return allow?std::make_shared<int>(1):nullptr;}};
KERNEL
struct VSBeaconPresentation{VSBeaconPresentation(VSWorldRenderer&,VSWorldScene&);~VSBeaconPresentation();void reset();void append(const VSNativeSession&,std::vector<VSWorldRenderer::Packet>&);struct Impl;std::unique_ptr<Impl>mImpl;};
PUBLICATION
int main(){VSWorldRenderer renderer;VSWorldScene scene;VSNativeSession owner;VSBeaconPresentation presentation(renderer,scene);
inputs={{{1,2,3},{{1,1,0,1}},4},{{4,5,6},{{1,.5f,0,.8f}},4}};
std::vector<VSWorldRenderer::Packet>out;presentation.append(owner,out);assert(out.size()==4&&renderer.serial==4);for(int i=0;i<4;++i){assert(out[i].overlay&&out[i].alpha&&out[i].fullbright&&out[i].screenLinePixels==4);assert(out[i].depthTest==(i>=2)&&out[i].depthWriteAlpha==(i>=2));}assert(out[0].color[3]==63.f/255.f&&out[1].color[3]==.2f&&out[2].color[3]==1&&out[3].color[3]==.8f);assert(gObjectList.queue.empty());
out.clear();presentation.append(owner,out);assert(out.size()==4&&renderer.serial==4); // camera-only frames reuse actual meshes
inputs[0].mPositionAgent.mV[0]=7;renderer.refuseAt=renderer.serial+2;out.clear();presentation.append(owner,out);assert(out.size()==2&&out[0].color[0]==1&&out[0].color[1]==127.f/255.f); // no old first marker at a false position
renderer.refuseAt=-1;out.clear();presentation.append(owner,out);assert(out.size()==4);
inputs[0].mPositionAgent.mV[0]=8;scene.allow=false;out.clear();presentation.append(owner,out);assert(out.size()==2);scene.allow=true;out.clear();presentation.append(owner,out);assert(out.size()==4);

inputs[0].mString="native label";inputs[0].mTextColor=LLColor4(.2f,.3f,.4f,.8f);out.clear();presentation.append(owner,out);assert(labels.size()==1&&!labels[0]->dead&&labels[0]->text=="native label"&&!labels[0]->z&&labels[0]->position==inputs[0].mPositionAgent);const auto edits=labels[0]->edits;out.clear();presentation.append(owner,out);assert(labels[0]->edits==edits);
inputs[0].mString="changed";inputs[0].mPositionAgent.mV[1]+=5;out.clear();presentation.append(owner,out);assert(labels[0]->text=="changed"&&labels[0]->position==inputs[0].mPositionAgent);
inputs[0].mString.clear();out.clear();presentation.append(owner,out);assert(labels[0]->dead);
LLEnvironment::instance().sky=std::make_shared<Sky>();scene.agent=true;gSavedSettings.sun=gSavedSettings.moon=true;out.clear();presentation.append(owner,out);assert(out.size()==6);for(int i=4;i<6;++i)assert(out[i].fullbright&&out[i].alpha&&out[i].overlay&&!out[i].depthTest&&!out[i].depthWriteAlpha&&out[i].screenLinePixels==3&&out[i].color[3]==63.f/255.f);int serial=renderer.serial;out.clear();presentation.append(owner,out);assert(renderer.serial==serial&&out.size()==6);
LLEnvironment::instance().sky->sun.mV[0]=.5f;renderer.refuseAt=renderer.serial+1;out.clear();presentation.append(owner,out);assert(out.size()==5);renderer.refuseAt=-1;out.clear();presentation.append(owner,out);assert(out.size()==6);
gSavedSettings.sun=gSavedSettings.moon=false;
++owner.generation;active=false;out.clear();presentation.append(owner,out);assert(out.empty());
Beacon generic;generic.mString="current-frame";generic.mPositionAgent={2,3,4};gObjectList.queue.push_back(generic);out.clear();presentation.append(owner,out);assert(out.size()==2&&gObjectList.queue.empty()&&labels.back()->text=="current-frame"&&!labels.back()->dead);out.clear();presentation.append(owner,out);assert(out.empty()&&labels.back()->dead);
presentation.reset();assert(gObjectList.queue.empty());
}
