// Diagnostic-only terrain pending/replacement regression. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsterrainreplay.h"
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#include "vsworldscene.h"
#include "vsworldrenderer.h"
#include "vsuiresources.h"
#include "llviewerwindow.h"
#include "llimage.h"
#include "llimagej2c.h"
#include "vsuiimageprovider.h"
#include "llbitpack.h"
#include "message.h"
#pragma push_macro("Bool")
#pragma push_macro("False")
#pragma push_macro("True")
#pragma push_macro("MAP_TYPE")
#undef Bool
#undef False
#undef True
#undef MAP_TYPE
#include <DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <DiligentCore/Common/interface/RefCntAutoPtr.hpp>
#pragma pop_macro("MAP_TYPE")
#pragma pop_macro("True")
#pragma pop_macro("False")
#pragma pop_macro("Bool")
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <vector>
#include <array>
namespace {
void requireTerrain(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
const LLUUID detailID("c3800000-0000-0000-0000-000000000001");
struct TerrainReplay {
 unsigned phase=0;
 bool complete=false;
 LLSD evidence;
 std::unique_ptr<VSWorldRenderer> renderer;
 Diligent::RefCntAutoPtr<Diligent::ITexture> target;
 void capture(VSVulkanContext& owner,Diligent::IRenderDevice* device,Diligent::IDeviceContext* context,
              Diligent::ITextureView* windowTarget,unsigned width,unsigned height) {
  using namespace Diligent;
  if(complete)return;
  auto& world=owner.world();auto& resources=owner.resources();
  if(!world.terrainStatus()["received_patches"].asInteger())return;
  const LLVector3 eye(8,8,10);
  auto packets=world.packets(eye,64,64,64);
  packets.erase(std::remove_if(packets.begin(),packets.end(),[](const auto& p){
    return !p.terrain || p.terrainKey!=0 || p.regionOffset[0]!=0 || p.regionOffset[1]!=0;
  }),packets.end());
  if(packets.empty()) {
   requireTerrain(!world.terrainStatus()["published_meshes"].asInteger(),"Received terrain mesh disappeared while its texture was pending");return;
  }
  if(phase==2 && !vs_ui_image_ready(detailID))return;
  requireTerrain(packets.size()==1,"Terrain replay received duplicate ground patches");
  const auto key="remote:"+detailID.asString();
  if(phase==1 || phase==3) requireTerrain(packets[0].image==resources.defaultWorldImage(),"Pending terrain did not bind the reference default image");
  else requireTerrain(packets[0].image==resources.image(key) && packets[0].image!=resources.defaultWorldImage(),"Terrain did not replace the default with its published detail image");
  if(!renderer) renderer=std::make_unique<VSWorldRenderer>(device,context);
  if(!target) {
   TextureDesc desc{};desc.Name="Terrain production packet pixel target";desc.Type=RESOURCE_DIM_TEX_2D;
   desc.Width=desc.Height=64;desc.Format=TEX_FORMAT_RGBA8_UNORM;desc.BindFlags=BIND_RENDER_TARGET;
   device->CreateTexture(desc,nullptr,&target);requireTerrain(bool(target),"Terrain target allocation failed");
  }
  auto* output=target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
  context->SetRenderTargets(1,&output,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  const float clear[4]={1,0,1,1};context->ClearRenderTarget(output,clear,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  renderer->draw(output,64,64,VSWorldScene::cameraView(eye,{0,0,-1},{0,1,0},64,64,64),packets);
  auto desc=target->GetDesc();desc.Name="Terrain regression readback";desc.Usage=USAGE_STAGING;
  desc.BindFlags=BIND_NONE;desc.CPUAccessFlags=CPU_ACCESS_READ;
  RefCntAutoPtr<ITexture> staging;device->CreateTexture(desc,nullptr,&staging);
  requireTerrain(bool(staging),"Terrain readback allocation failed");
  context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  CopyTextureAttribs copy{};copy.pSrcTexture=target;copy.pDstTexture=staging;
  copy.SrcTextureTransitionMode=copy.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
  context->CopyTexture(copy);context->WaitForIdle();
  MappedTextureSubresource mapped{};context->MapTextureSubresource(staging,0,0,MAP_READ,MAP_FLAG_DO_NOT_WAIT,nullptr,mapped);
  requireTerrain(mapped.pData!=nullptr,"Terrain readback mapping failed");
  std::vector<U8> pixels(64*64*4);
  for(unsigned y=0;y<64;++y)std::memcpy(pixels.data()+y*64*4,static_cast<const U8*>(mapped.pData)+y*mapped.Stride,64*4);
  context->UnmapTextureSubresource(staging,0,0);
  const std::array<U8,4> expected=phase==2?std::array<U8,4>{32,160,64,255}:std::array<U8,4>{127,127,127,255};
  const auto stem=phase==1?"pending":phase==2?"published":"retired";
  const auto relative=std::string("terrain/")+stem+".rgba";
  const auto file=std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"))/relative;
  std::filesystem::create_directories(file.parent_path());std::ofstream out(file,std::ios::binary);
  out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());requireTerrain(out.good(),"Terrain readback artifact write failed");out.close();
  for(std::size_t i=0;i<pixels.size();++i)requireTerrain(std::abs(int(pixels[i])-int(expected[i%4]))<=1,"Actual terrain GPU pixels differ from expected ground");
  evidence[stem]["file"]=relative;evidence[stem]["draw_packets"]=S32(packets.size());evidence[stem]["pixels_verified"]=true;
  if(phase==1) {
   LLPointer<LLImageRaw> raw=new LLImageRaw(128,128,4);raw->clear(32,160,64,255);
   LLPointer<LLImageJ2C> encoded=new LLImageJ2C;encoded->setReversible(true);
   const auto fixture=std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"))/"terrain/detail.j2c";
   const auto temporary=fixture.string()+".pending";
   requireTerrain(encoded->encode(raw,0.f) && encoded->save(temporary),"Terrain network image fixture write failed");
   std::filesystem::rename(temporary,fixture);phase=2;
  } else if(phase==2) {resources.erase(key);phase=3;} else {complete=true;}
  renderer->retire();
  context->SetRenderTargets(1,&windowTarget,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  Viewport viewport{0.f,0.f,float(width),float(height)};context->SetViewports(1,&viewport,width,height);
 }
};
}
bool vs_terrain_replay_tick(VSNativeSession& session,LLSD& report) {
 if(!std::getenv("VS_VULKAN_REPLAY_TERRAIN"))return true;
 static std::shared_ptr<TerrainReplay> state;
 auto& owner=*gViewerWindow->nativeContext();
 if(!state) {
  state=std::make_shared<TerrainReplay>();state->phase=1;
  LLSD handshake;handshake["terrain_textures"]=LLSD::emptyArray();
  for(unsigned i=0;i<4;++i){handshake["terrain_textures"].append(detailID);handshake["terrain_starts"].append(0.);handshake["terrain_ranges"].append(1.);}
  owner.world().configureRegion(handshake);
  std::array<U8,64> bytes{};LLBitPack bits(bytes.data(),U32(bytes.size()));
  U16 stride=264,range=1;U8 size=16,type='L',quant=0x82,end=97,endCoefficients=2;F32 offset=-.5f;U16 id=0;
  bits.bitPack(reinterpret_cast<U8*>(&stride),16);bits.bitPack(&size,8);bits.bitPack(&type,8);bits.bitPack(&quant,8);
  bits.bitPack(reinterpret_cast<U8*>(&offset),32);bits.bitPack(reinterpret_cast<U8*>(&range),16);bits.bitPack(reinterpret_cast<U8*>(&id),10);
  bits.bitPack(&endCoefficients,2);bits.bitPack(&end,8);const auto count=bits.flushBitPack();
  gMessageSystem->newMessage("LayerData");gMessageSystem->nextBlock("LayerID");gMessageSystem->addU8("Type",type);
  gMessageSystem->nextBlock("LayerData");gMessageSystem->addBinaryData("Data",bytes.data(),count);
  requireTerrain(gMessageSystem->sendReliable(session.host())>0,"Terrain protocol fixture transport failed");
  owner.setSceneObserver([state=state,&owner](auto* d,auto* c,auto* target,unsigned w,unsigned h){state->capture(owner,d,c,target,w,h);});
  return false;
 }
 if(!state->complete)return false;
 report["terrain_publication"]=state->evidence;owner.setSceneObserver({});state.reset();
 // The caller invokes this once, before requesting its connected frame capture.
 report["terrain_publication"]["complete"]=true;
 return true;
}
