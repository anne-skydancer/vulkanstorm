// Actual native beacon publisher/line encoder, with CPU recording resource ports.
#include <cassert>
#include <array>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>
using U64=unsigned long long;
struct LLVector3{float mV[3]={};bool operator==(const LLVector3&v)const{return mV[0]==v.mV[0]&&mV[1]==v.mV[1]&&mV[2]==v.mV[2];}};using F32=float;constexpr int VX=0,VY=1,VZ=2;
struct LLColor4{float mV[4]={1,1,1,1};};LLColor4 linearColor4(LLColor4 v){return v;}
struct Beacon{LLVector3 mPositionAgent;LLColor4 mColor;int mLineWidth=4;};std::vector<Beacon> inputs;
struct ObjectList{std::vector<Beacon>queue;void resetObjectBeacons(){queue.clear();}const std::vector<Beacon>&debugBeacons()const{return queue;}}gObjectList;
bool active=true;struct LLFloaterTelehub{static bool renderBeacons(){return active;}static void addBeacons(){gObjectList.queue=inputs;}};
struct VSNativeSession{struct AccountHandle{int generation=0;};int generation=1;bool current(AccountHandle a)const{return a.generation==generation;}AccountHandle account()const{return{generation};}};
struct VSWorldRenderer{struct Vertex{float x,y,z,nx,ny,nz,u,v;};using Mesh=std::shared_ptr<int>;struct Packet{Mesh mesh;std::array<float,4>color{};bool fullbright=false,alpha=false,overlay=false,depthTest=true,depthWriteAlpha=false;float screenLinePixels=0;};int serial=0,refuseAt=-1;bool publishMesh(Mesh&mesh,const std::vector<Vertex>&v,const std::vector<unsigned>&i){++serial;assert(v.size()==90&&i.size()==90);if(serial==refuseAt)return false;mesh=std::make_shared<int>(serial);return true;}};
struct VSWorldScene{bool allow=true;std::shared_ptr<int>reserveSpatialBytes(std::size_t n){assert(n==180*(sizeof(VSWorldRenderer::Vertex)+sizeof(unsigned)));return allow?std::make_shared<int>(1):nullptr;}};
KERNEL
struct VSBeaconPresentation{VSBeaconPresentation(VSWorldRenderer&,VSWorldScene&);~VSBeaconPresentation();void reset();void append(const VSNativeSession&,std::vector<VSWorldRenderer::Packet>&);struct Impl;std::unique_ptr<Impl>mImpl;};
PUBLICATION
int main(){VSWorldRenderer renderer;VSWorldScene scene;VSNativeSession owner;VSBeaconPresentation presentation(renderer,scene);
inputs={{{1,2,3},{{1,1,0,1}},4},{{4,5,6},{{1,.5f,0,.8f}},4}};
std::vector<VSWorldRenderer::Packet>out;presentation.append(owner,out);assert(out.size()==4&&renderer.serial==4);for(int i=0;i<4;++i){assert(out[i].overlay&&out[i].alpha&&out[i].fullbright&&out[i].screenLinePixels==4);assert(out[i].depthTest==(i>=2)&&out[i].depthWriteAlpha==(i>=2));}assert(out[0].color[3]==.25f&&out[1].color[3]==.2f&&out[2].color[3]==1&&out[3].color[3]==.8f);assert(gObjectList.queue.empty());
out.clear();presentation.append(owner,out);assert(out.size()==4&&renderer.serial==4); // camera-only frames reuse actual meshes
inputs[0].mPositionAgent.mV[0]=7;renderer.refuseAt=renderer.serial+2;out.clear();presentation.append(owner,out);assert(out.size()==2&&out[0].color[0]==1&&out[0].color[1]==.5f); // no old first marker at a false position
renderer.refuseAt=-1;out.clear();presentation.append(owner,out);assert(out.size()==4);
inputs[0].mPositionAgent.mV[0]=8;scene.allow=false;out.clear();presentation.append(owner,out);assert(out.size()==2);scene.allow=true;out.clear();presentation.append(owner,out);assert(out.size()==4);
++owner.generation;active=false;out.clear();presentation.append(owner,out);assert(out.empty());presentation.reset();assert(gObjectList.queue.empty());
}
