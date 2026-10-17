// Diagnostic-only terrain pending/replacement regression. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsr3terrainreplay.h"
#include "lllocalgltfmaterials.h"
#include "llgltfmateriallist.h"
#include "llviewercontrol.h"
#include "llimagepng.h"
#include "llsdutil.h"
#include <map>
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
constexpr U8 colors[4][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,255,255}};
struct TerrainReplay {
 unsigned phase=0;bool complete=false;LLSD evidence;
 std::map<std::string,LLSD> saved;
 std::array<LLPointer<LLLocalGLTFMaterial>,4> assets;
 std::unique_ptr<VSWorldRenderer> renderer;
 Diligent::RefCntAutoPtr<Diligent::ITexture> target;
 void save(const std::string& name) {saved[name]=gSavedSettings.getControl(name)->getValue();}
 ~TerrainReplay() {
  for(const auto& [name,value]:saved) gSavedSettings.getControl(name)->setValue(value);
  for(auto& asset:assets) if(asset) {gGLTFMaterialList.removeMaterial(asset->getWorldID());asset=nullptr;}
 }
 void begin(const std::filesystem::path& root) {
  for(const char* name:{"LocalTerrainPaintEnabled","RenderTerrainPBRScale","RenderTerrainPBRDetail",
      "RenderTerrainPBRPlanarSampleCount","RenderTerrainPBRTriplanarBlendFactor","TerrainPaintResolution","TerrainPaintBitDepth"}) save(name);
  gSavedSettings.setBOOL("LocalTerrainPaintEnabled",true);
  gSavedSettings.setS32("RenderTerrainPBRDetail",0);gSavedSettings.setS32("RenderTerrainPBRPlanarSampleCount",3);
  gSavedSettings.setF32("RenderTerrainPBRScale",8);gSavedSettings.setF32("RenderTerrainPBRTriplanarBlendFactor",8);
  std::filesystem::create_directories(root/"r3/terrain");
  for(unsigned layer=0;layer<4;++layer) {
   const auto prefix="LocalTerrainTransform"+std::to_string(layer+1);
   for(const auto& suffix:{"ScaleU","ScaleV","OffsetU","OffsetV","Rotation"}) {
    const auto name=prefix+suffix;save(name);gSavedSettings.setF32(name,suffix[0]=='S'?1.f:0.f);
   }
   const auto setting="LocalTerrainAsset"+std::to_string(layer+1);save(setting);
   const auto pngPath=root/"r3/terrain"/(std::to_string(layer)+".png");
   LLPointer<LLImageRaw> raw=new LLImageRaw(16,16,4);raw->clear(colors[layer][0],colors[layer][1],colors[layer][2],255);
   LLPointer<LLImagePNG> png=new LLImagePNG;
   requireTerrain(png->encode(raw,0) && png->save(pngPath.string()),"R3 terrain fixture PNG write failed");
   const auto file=root/"r3/terrain"/(std::to_string(layer)+".gltf");
   std::ofstream out(file);out<<"{\"asset\":{\"version\":\"2.0\"},\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],\"textures\":[{\"source\":0}],\"images\":[{\"uri\":\""<<layer<<".png\"}]}";
   out.close();requireTerrain(out.good(),"R3 terrain fixture material write failed");
   assets[layer]=new LLLocalGLTFMaterial(file.string(),0);
   requireTerrain(assets[layer]->updateSelf() && assets[layer]->isLoaded(),"R3 terrain local material did not load");
   gSavedSettings.setString(setting,assets[layer]->getWorldID().asString());
  }
 }
 void capture(VSVulkanContext& owner,Diligent::IRenderDevice* device,Diligent::IDeviceContext* context,
              Diligent::ITextureView* windowTarget,unsigned width,unsigned height) {
  using namespace Diligent;
  if(complete) return;
  auto& world=owner.world();auto& resources=owner.resources();
  const LLVector3 eye(8,8,10);
  auto packets=world.packets(eye,64,64,64);
  packets.erase(std::remove_if(packets.begin(),packets.end(),[](const auto& p){
   return !p.terrain || p.terrainKey!=0 || p.regionOffset[0]!=0 || p.regionOffset[1]!=0;
  }),packets.end());
  if(packets.empty() || !packets[0].terrainMaterial) return;
  requireTerrain(packets.size()==1,"R3 terrain production patch missing or duplicated");
  const auto& material=*packets[0].terrainMaterial;
  requireTerrain(material.paint && material.detail==0 && material.planar==3 && material.sharpness==8,
   "R3 terrain settings did not reach production material");
  for(unsigned layer=0;layer<4;++layer) {
   requireTerrain(material.images[layer][0] && material.layers[layer].pbr,"R3 terrain lost loaded local PBR channel");
   requireTerrain(std::abs(material.layers[layer].uv[0].matrix[0]-.125f)<1e-6f,"R3 terrain PBR scale was inert");
  }
  packets[0].fullbright=true; // Isolate R3 material inputs from later R5 lighting.
  if(!renderer) renderer=std::make_unique<VSWorldRenderer>(device,context);
  if(!target) {
   TextureDesc desc{};desc.Name="R3 terrain material pixel target";desc.Type=RESOURCE_DIM_TEX_2D;
   desc.Width=desc.Height=64;desc.Format=TEX_FORMAT_RGBA8_UNORM;desc.BindFlags=BIND_RENDER_TARGET;
   device->CreateTexture(desc,nullptr,&target);requireTerrain(bool(target),"Terrain target allocation failed");
  }
  auto* output=target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
  context->SetRenderTargets(1,&output,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  const float clear[4]={1,0,1,1};context->ClearRenderTarget(output,clear,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  renderer->draw(output,64,64,VSWorldScene::cameraView(eye,{0,0,-1},{0,1,0},64,64,64),packets);
  auto desc=target->GetDesc();desc.Name="R3 terrain material readback";desc.Usage=USAGE_STAGING;
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

  const auto root=std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"));
  const auto name="r3/terrain/paint-layer-"+std::to_string(phase)+".rgba";
  std::ofstream out(root/name,std::ios::binary);out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
  requireTerrain(out.good(),"R3 terrain material readback archive failed");out.close();
  for(std::size_t offset=0;offset<pixels.size();++offset)
   requireTerrain(std::abs(int(pixels[offset])-int(colors[phase][offset%4]))<=1,"R3 actual terrain PBR pixels differ from selected paint layer");
  LLSD row;row["file"]=name;row["layer"]=S32(phase);row["pixels_verified"]=true;
  evidence["layers"].append(row);
  if(++phase<4) {
   LLPointer<LLImageRaw> raw=new LLImageRaw(1,1,4);raw->clear(phase==1?255:0,phase==2?255:0,phase==3?255:0,255);
   requireTerrain(bool(resources.publish("terrain:local-paint",*raw)),"R3 terrain paint replacement refused");
  } else {
   // Exercise real local terrain transform and scale controllers on the same mesh.
   const auto mesh=packets[0].mesh;
   gSavedSettings.setF32("RenderTerrainPBRScale",4);gSavedSettings.setF32("LocalTerrainTransform1OffsetU",.25f);
   auto changed=world.packets(eye,64,64,64);
   bool checked=false;
   for(const auto& packet:changed) if(packet.terrain && packet.terrainKey==0 && packet.regionOffset[0]==0 && packet.regionOffset[1]==0) {
    requireTerrain(packet.mesh==mesh && packet.terrainMaterial &&
     std::abs(packet.terrainMaterial->layers[0].uv[0].matrix[0]-.25f)<1e-6f &&
     std::abs(packet.terrainMaterial->layers[0].uv[0].offset[0]-.25f)<1e-6f,
     "R3 terrain live scale/transform did not update without geometry replacement");checked=true;
   }
   requireTerrain(checked,"R3 terrain live transform witness missing");
   evidence["live_scale_transform"]=true;
   gSavedSettings.setU32("TerrainPaintResolution",64);gSavedSettings.setU32("TerrainPaintBitDepth",1);
   requireTerrain(world.createTerrainPaintMap(),"R3 native local paint bake failed");
   const auto paint=resources.rawImage("terrain:local-paint");
   requireTerrain(paint && paint->getWidth()==64 && paint->getHeight()==64,"R3 terrain paint resolution was inert");
   for(S32 offset=0;offset<paint->getDataSize();++offset)
    requireTerrain(paint->getData()[offset]==0 || paint->getData()[offset]==255,"R3 terrain paint bit-depth quantization was inert");
   evidence["paint_bake"]=true;evidence["paint_bit_depth"]=1;
   world.deleteTerrainPaintMap();
   requireTerrain(!gSavedSettings.getBOOL("LocalTerrainPaintEnabled") && !resources.image("terrain:local-paint"),"R3 terrain paint deletion failed");
   evidence["paint_deleted"]=true;complete=true;
  }
  renderer->retire();context->SetRenderTargets(1,&windowTarget,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  Viewport viewport{0.f,0.f,float(width),float(height)};context->SetViewports(1,&viewport,width,height);
 }
};
std::shared_ptr<TerrainReplay> state;
}
bool vs_r3_terrain_replay_tick(LLSD& report) {
 if(!std::getenv("VS_VULKAN_REPLAY_LOCAL_IMAGES") || report["r3_terrain"]["complete"].asBoolean()) return true;
 auto& owner=*gViewerWindow->nativeContext();
 if(!state) {
  state=std::make_shared<TerrainReplay>();
  state->begin(std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY")));
  owner.setSceneObserver([held=state,&owner](auto* d,auto* c,auto* target,unsigned w,unsigned h){held->capture(owner,d,c,target,w,h);});
  return false;
 }
 if(!state->complete) return false;
 report["r3_terrain"]=state->evidence;report["r3_terrain"]["complete"]=true;
 owner.setSceneObserver({});state.reset();return true;
}
void vs_r3_terrain_replay_cleanup() {
 if(state && gViewerWindow && gViewerWindow->nativeContext()) gViewerWindow->nativeContext()->setSceneObserver({});
 state.reset();
}
