#include <cassert>
#include <memory>
#include <vector>
#include <cmath>
#include <functional>
#include <cstdint>
using S32=int; using U32=unsigned; using U64=uint64_t;
constexpr int VX=0,VY=1,VZ=2;
struct LLVector3 {float mV[3]={1,1,1};bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1])&&std::isfinite(mV[2]);}};
struct LLVector2 {float mV[2];};struct LLGLTFMaterial {static constexpr int GLTF_TEXTURE_INFO_COUNT=4;};
struct VSTextureScaleRatios {std::vector<LLVector3> values;std::shared_ptr<const void>residency;};
struct Saved {std::shared_ptr<VSTextureScaleRatios>textureRatios;};
struct VSWorldScene {struct ObjectHandle {int id=1;};bool alive=true,budget=true;int leases=0;std::size_t charged=0;std::function<void()>onReserve;bool current(ObjectHandle)const{return alive;}std::shared_ptr<const void>reserveSpatialBytes(std::size_t n){charged=n;if(onReserve)onReserve();if(!budget)return {}; ++leases;return {new int(1),[this](const void*p){--leases;delete static_cast<const int*>(p);}};}};
struct Context{VSWorldScene world_;VSWorldScene&world(){return world_;}};struct Window{Context ctx;bool present=true;Context*nativeContext(){return present?&ctx:nullptr;}};Window* gViewerWindow=nullptr;
struct VSObjectSelection {std::shared_ptr<Saved>saved=std::make_shared<Saved>();bool selected=true;std::shared_ptr<Saved>savedTransform(VSWorldScene::ObjectHandle)const{return selected?saved:nullptr;}bool node(VSWorldScene::ObjectHandle)const{return selected;}};
struct ObjectState {bool present=true,entries=true;S32 count=3;LLVector3 scale;};ObjectState state;
struct VSMaterialObject {static VSMaterialObject find(int){return {};}operator bool()const{return state.present;}S32 getNumTEs()const{return state.count;}const int*getTE(S32)const{static int x;return state.entries?&x:nullptr;}LLVector3 getScale()const{return state.scale;}};
std::function<void()>captureHook;int captures=0;
void vs_capture_texture_scale_ratios(VSMaterialObject*,VSTextureScaleRatios&r){++captures;r.values.resize(state.count);if(captureHook)captureHook();}
CAPTURE
int main(){Window w;gViewerWindow=&w;VSObjectSelection selection;VSWorldScene::ObjectHandle token;auto&world=w.ctx.world_;
 auto reset=[&]{selection.saved->textureRatios.reset();selection=VSObjectSelection();state=ObjectState();world.alive=world.budget=true;world.onReserve={};captureHook={};captures=0;assert(world.leases==0);};
 assert(vs_capture_selected_texture_ratios(selection,token));assert(captures==1&&world.leases==1&&selection.saved->textureRatios->values.size()==3&&world.charged>sizeof(VSTextureScaleRatios));
 // A refused new manipulation must retire its previous snapshot, never reuse it.
 world.budget=false;assert(!vs_capture_selected_texture_ratios(selection,token));assert(!selection.saved->textureRatios&&world.leases==0);
 reset();state.entries=false;assert(!vs_capture_selected_texture_ratios(selection,token)&&captures==0&&world.leases==0);
 reset();state.scale.mV[1]=0;assert(!vs_capture_selected_texture_ratios(selection,token)&&captures==0);
 reset();state.scale.mV[2]=NAN;assert(!vs_capture_selected_texture_ratios(selection,token)&&captures==0);
 reset();state.count=46;assert(!vs_capture_selected_texture_ratios(selection,token)&&captures==0);
 reset();captureHook=[&]{world.alive=false;};assert(!vs_capture_selected_texture_ratios(selection,token)&&!selection.saved->textureRatios&&world.leases==0);
 reset();auto old=selection.saved;captureHook=[&]{selection.saved=std::make_shared<Saved>();};assert(!vs_capture_selected_texture_ratios(selection,token)&&!old->textureRatios&&!selection.saved->textureRatios&&world.leases==0);
 reset();captureHook=[&]{++state.count;};assert(!vs_capture_selected_texture_ratios(selection,token)&&!selection.saved->textureRatios&&world.leases==0);
 reset();selection.selected=false;assert(!vs_capture_selected_texture_ratios(selection,token)&&captures==0);
 reset();gViewerWindow=nullptr;assert(!vs_capture_selected_texture_ratios(selection,token)&&captures==0);
}
