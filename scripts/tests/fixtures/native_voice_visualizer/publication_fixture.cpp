#include <array>
#include <vector>
#include <memory>
#include <optional>
#include <algorithm>
#include <cassert>
using F32=float;using S32=int;
int gSavedSettings;template<class T>struct LLCachedControl{LLCachedControl(int&,const char*,T){}operator T()const{return true;}};
struct LLVector3d{double mV[3]{};LLVector3d()=default;LLVector3d(double x,double y,double z):mV{x,y,z}{};LLVector3d operator-(const LLVector3d&b)const{return{mV[0]-b.mV[0],mV[1]-b.mV[1],mV[2]-b.mV[2]};}bool operator==(const LLVector3d&)const=default;};
struct LLVector3{float mV[3]{};static const LLVector3 zero;LLVector3()=default;explicit LLVector3(const LLVector3d&d):mV{float(d.mV[0]),float(d.mV[1]),float(d.mV[2])}{}};
const LLVector3 LLVector3::zero{};
struct LLColor4{float mV[4]{1,1,1,1};LLColor4()=default;explicit LLColor4(const float*c){std::copy_n(c,4,mV);}};
struct LLRender{enum eGeomModes{TRIANGLE_STRIP};};
namespace VSWorldContracts{enum class AddressMode{Clamp,Repeat};struct Sampler{AddressMode u{},v{};bool mipmaps=false;unsigned anisotropy=1;};}
struct VSUIRenderer{using Image=std::shared_ptr<int>;};
struct VSWorldRenderer{
 struct Vertex{float x,y,z,nx,ny,nz,u,v;};using Mesh=std::shared_ptr<std::vector<Vertex>>;
 struct Packet{Mesh mesh;VSUIRenderer::Image image;std::array<float,4>regionOffset{},color{};bool alpha=false,fullbright=false,overlay=false,doubleSided=false,depthTest=false,depthWriteAlpha=true;VSWorldContracts::Sampler imageSampler;};
 bool refuse=false;unsigned uploads=0;bool publishMesh(Mesh&out,const std::vector<Vertex>&v,const std::vector<unsigned>&indices){++uploads;assert(indices==std::vector<unsigned>({0,1,2,3,4,5}));if(refuse)return false;out=std::make_shared<std::vector<Vertex>>(v);return true;}
 static const std::vector<Vertex>&geometryVertices(const Mesh&m){return*m;}
};
struct Agent{LLVector3d origin{20,30,40};LLVector3d getPosGlobalFromAgent(const LLVector3&){return origin;}}gAgent;
VSUIRenderer::Image vs_ui_named_image_snapshot(const char*name){return std::make_shared<int>(name[12]=='d'?0:1);}
constexpr int NUM_VOICE_SYMBOL_WAVES=7;
struct LLVoiceVisualizer{
 struct NativeDraw{std::vector<VSWorldRenderer::Vertex>vertices;LLColor4 color;};std::array<NativeDraw,8>mNativeDraws;
 struct NativePublication{VSWorldRenderer::Mesh mesh;LLVector3d origin;LLColor4 color;};std::array<NativePublication,8>mNativePublications;std::array<VSUIRenderer::Image,2>mNativeImages;
 bool mDead=false,enabled=true;float x=1,color=.4f;
 template<class Painter>void renderSymbol(Painter&p){if(!enabled)return;p.pipeline();p.bind(0);float c[4]{1,1,1,color};p.color4fv(c);for(int i=0;i<6;++i){p.texCoord2i(i%2,i%3?1:0);float v[3]{x+float(i),2,3};p.vertex3fv(v);}}
 void appendNative(VSWorldRenderer&,const LLVector3d&,std::vector<VSWorldRenderer::Packet>&);
};
METHOD
int main(){LLVoiceVisualizer owner;VSWorldRenderer renderer;std::vector<VSWorldRenderer::Packet>packets;LLVector3d origin{10,15,20};owner.appendNative(renderer,origin,packets);assert(packets.size()==1&&renderer.uploads==1);auto first=packets[0];assert((*first.mesh)[0].x==11&&(*first.mesh)[0].y==17&&(*first.mesh)[0].z==23&&(*first.mesh)[0].v==1);assert(first.alpha&&first.fullbright&&first.overlay&&first.doubleSided&&first.depthTest&&!first.depthWriteAlpha);assert(first.color[3]==.4f);assert(!first.imageSampler.mipmaps&&first.imageSampler.anisotropy==16&&first.imageSampler.u==VSWorldContracts::AddressMode::Repeat);auto capacity=owner.mNativeDraws[0].vertices.capacity();packets.clear();owner.color=.8f;owner.appendNative(renderer,origin,packets);assert(renderer.uploads==1&&packets[0].mesh==first.mesh&&packets[0].color[3]==.8f&&capacity==owner.mNativeDraws[0].vertices.capacity());renderer.refuse=true;owner.x=5;packets.clear();owner.appendNative(renderer,{100,200,300},packets);assert(renderer.uploads==2&&packets[0].mesh==first.mesh&&packets[0].regionOffset[0]==-90&&packets[0].color[3]==.8f);renderer.refuse=false;packets.clear();owner.appendNative(renderer,origin,packets);assert(renderer.uploads==3&&packets[0].mesh!=first.mesh&&(*packets[0].mesh)[0].x==15);owner.enabled=false;packets.clear();owner.appendNative(renderer,origin,packets);assert(packets.empty());owner.mDead=true;owner.enabled=true;owner.appendNative(renderer,origin,packets);assert(packets.empty());}
