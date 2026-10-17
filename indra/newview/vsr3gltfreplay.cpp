// Actual native imported-image lifecycle qualification. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsr3gltfreplay.h"
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#include "vsworldscene.h"
#include "vsuiresources.h"
#include "llviewerwindow.h"
#include "llviewercontrol.h"
#include "llwindow.h"
#include "llprimitive.h"
#include "llvolumemessage.h"
#include "llregionhandle.h"
#include "llframetimer.h"
#include "llsdutil.h"
#include "vsmaterial.h"
#include "message.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <stdexcept>
namespace {
const LLUUID objectID("b3800000-0000-0000-0000-000000000001");
constexpr unsigned localID=993;
constexpr unsigned colors[5][4][4]={
 {{17,34,51,255},{68,85,102,255},{119,136,153,255},{170,187,204,255}},
 {{128,128,255,255},{136,120,250,255},{120,136,250,255},{128,128,255,255}},
 {{11,77,155,255},{22,88,166,255},{33,99,177,255},{44,111,188,255}},
 {{51,17,34,255},{102,68,85,255},{153,119,136,255},{204,170,187,255}},
 {{210,210,210,255},{180,180,180,255},{150,150,150,255},{120,120,120,255}}};
void require(bool value,const char* message){if(!value) throw std::runtime_error(message);}
struct State {
 unsigned step=0,settleFrame=0;double deadline=0;
 F32 minimizedTime=gSavedSettings.getF32("TextureDiscardMinimizedTime");
 U32 limit=gSavedSettings.getU32("RenderMaxTextureResolution");
 bool full=gSavedSettings.getBOOL("TextureLoadFullRes"),compression=gSavedSettings.getBOOL("RenderCompressTextures");
 bool anisotropy=gSavedSettings.getBOOL("RenderAnisotropic");
 LLSD evidence;
 std::vector<VSUIRenderer::Image> retained;
 std::vector<std::vector<U8>> retainedPixels;
 std::vector<std::weak_ptr<const VSUIRenderer::Texture>> images;
 ~State(){if(gViewerWindow && gViewerWindow->getWindow())gViewerWindow->getWindow()->restore();
  gSavedSettings.setF32("TextureDiscardMinimizedTime",minimizedTime);gSavedSettings.setU32("RenderMaxTextureResolution",limit);gSavedSettings.setBOOL("TextureLoadFullRes",full);
  gSavedSettings.setBOOL("RenderCompressTextures",compression);gSavedSettings.setBOOL("RenderAnisotropic",anisotropy);}
};
std::unique_ptr<State> state;
void createObject(VSNativeSession& session){
 LLMessageSystem* msg=gMessageSystem;msg->newMessage("ObjectUpdate");msg->nextBlock("RegionData");
 msg->addU64("RegionHandle",to_region_handle(256,512));msg->addU16("TimeDilation",65535);
 msg->nextBlock("ObjectData");msg->addU32("ID",localID);msg->addU8("State",0);msg->addUUID("FullID",objectID);
 msg->addU32("CRC",0);msg->addU8("PCode",LL_PCODE_VOLUME);msg->addU8("Material",0);msg->addU8("ClickAction",0);
 msg->addVector3("Scale",LLVector3(1,1,1));
 const auto position=session.position()+LLVector3(0,2,0);
 const F32 pose[15]={position.mV[0],position.mV[1],position.mV[2],0,0,0,0,0,0,0,0,0,0,0,0};
 msg->addBinaryData("ObjectData",pose,sizeof(pose));msg->addU32("ParentID",0);msg->addU32("UpdateFlags",0);
 LLVolumeParams params;LLVolumeMessage::packVolumeParams(&params,msg);
 LLPrimitive material;material.setNumTEs(6);material.packTEMessage(msg);
 for(const char* field:{"TextureAnim","NameValue","Data","Text","MediaURL","PSBlock","ExtraParams"}) msg->addBinaryData(field,nullptr,0);
 const U8 white[4]={255,255,255,255};msg->addBinaryData("TextColor",white,4);
 msg->addUUID("Sound",LLUUID::null);msg->addUUID("OwnerID",LLUUID::null);msg->addF32("Gain",0);
 msg->addU8("Flags",0);msg->addF32("Radius",0);msg->addU8("JointType",0);
 msg->addVector3("JointPivot",LLVector3::zero);msg->addVector3("JointAxisOrAnchor",LLVector3::zero);
 require(msg->sendReliable(session.host())>0,"R3 imported-image object transport failed");
}
LLSD observe(State& s,VSUIResources& resources,const VSWorldRenderer::Packet& packet,const std::string& phase,unsigned width,unsigned variant){
 require(packet.surface && packet.surface->pbr && packet.surface->doubleSided,"R3 imported PBR material flags lost");
 const auto& surface=*packet.surface;
 require(surface.alpha==VSWorldContracts::AlphaMode::Mask && std::abs(surface.cutoff-.4f)<1e-5f &&
  std::abs(surface.metallic-.35f)<1e-5f && std::abs(surface.roughness-.65f)<1e-5f &&
  std::abs(surface.normalScale-.5f)<1e-5f && std::abs(surface.occlusionStrength-.6f)<1e-5f && surface.independentOcclusion,
  "R3 imported material factors lost");
 require(std::abs(surface.base[0]-.25f)<1e-5f && std::abs(surface.base[1]-.5f)<1e-5f &&
  std::abs(surface.base[2]-.75f)<1e-5f && std::abs(surface.base[3]-.8f)<1e-5f &&
  std::abs(surface.emissive[0]-.1f)<1e-5f && std::abs(surface.emissive[1]-.2f)<1e-5f &&
  std::abs(surface.emissive[2]-.3f)<1e-5f,"R3 imported linear color factors lost");
 require(surface.uvSet[1]==1 && surface.uvSet[4]==1 && surface.samplers[0].u==VSWorldContracts::AddressMode::Mirror &&
  surface.samplers[0].v==VSWorldContracts::AddressMode::Clamp && surface.samplers[0].minification==VSWorldContracts::Filter::Nearest &&
  surface.samplers[0].magnification==VSWorldContracts::Filter::Nearest && surface.samplers[0].mipmaps && surface.samplers[0].trilinear,
  "R3 imported sampler or UV selection lost");
 require(surface.samplers[0].anisotropy==(gSavedSettings.getBOOL("RenderAnisotropic")?16u:1u),"R3 imported anisotropy preference inert");
 LLSD result;result["material_contracts"]=true;result["width"]=S32(width);result["height"]=S32(width/2);result["variant"]=S32(variant);
 std::array<VSUIRenderer::Image,5> images{packet.image,packet.channels[0],packet.channels[1],packet.channels[2],packet.channels[3]};
 const auto root=std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"));
 for(unsigned channel=0;channel<images.size();++channel){
  const auto& image=images[channel];const auto extent=VSUIRenderer::imageExtent(image);
  require(image && extent[0]==width && extent[1]==width/2,"R3 imported image extent mismatch");
  const auto pixels=resources.readbackImage(image);const auto& cpu=VSUIRenderer::imagePixels(image);
  require(pixels==cpu,"R3 imported image GPU bytes differ from publication");
  for(unsigned y=0;y<extent[1];++y) for(unsigned x=0;x<width;++x) for(unsigned c=0;c<4;++c){
   const unsigned quadrant=(y>=extent[1]/2?2:0)+(x>=width/2?1:0);
   require(pixels[(std::size_t(y)*width+x)*4+c]==colors[channel][(quadrant+variant)%4][c],"R3 imported image independent pixel mismatch");
  }
  const auto relative=std::filesystem::path("r3/gltf-images")/(phase+"-"+std::to_string(channel)+".rgba");
  std::ofstream output(root/relative,std::ios::binary);output.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
  require(output.good(),"R3 imported image readback not saved");
  LLSD row;row["channel"]=S32(channel);row["file"]=relative.generic_string();row["generation"]=ll_sd_from_U64(VSUIRenderer::generation(image));result["images"].append(row);
  s.images.push_back(image);
  if(phase=="initial"){s.retained.push_back(image);s.retainedPixels.push_back(pixels);}
 }
 for(unsigned i=0;i<s.retained.size();++i) require(VSUIRenderer::imagePixels(s.retained[i])==s.retainedPixels[i],"R3 imported replacement mutated queued generation");
 return result;
}
}
bool vs_r3_gltf_images(LLSD& report){
 if(!std::getenv("VS_VULKAN_REPLAY_LOCAL_IMAGES") || report.has("r3_gltf_images")) return true;
 const auto session=VSNativeSession::active();require(bool(session),"R3 imported image session missing");
 auto& world=gViewerWindow->nativeContext()->world();auto& resources=gViewerWindow->nativeContext()->resources();
 const auto folder=std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"))/"r3/gltf-images";
 if(!state){state=std::make_unique<State>();state->deadline=LLFrameTimer::getTotalSeconds()+30;
  gSavedSettings.setBOOL("RenderCompressTextures",false);gSavedSettings.setBOOL("TextureLoadFullRes",false);
  gSavedSettings.setU32("RenderMaxTextureResolution",512);gSavedSettings.setBOOL("RenderAnisotropic",false);
  createObject(*session);return false;}
 auto& s=*state;require(LLFrameTimer::getTotalSeconds()<s.deadline,"R3 imported image publication timed out");
 if(s.step==0){const auto object=world.object(objectID);if(!object)return false;
  require(world.loadGLTF(*object,(folder/"embedded.gltf").string()),"R3 embedded glTF scene load failed");++s.step;return false;}
 if(s.step==8){if(world.object(objectID))return false;
  for(const auto& image:s.images)if(!image.expired())return false;
  s.evidence["object_retired"]=true;s.evidence["immutable_generations"]=true;report["r3_gltf_images"]=s.evidence;return true;}
 if(s.step==5) {
  world.packets(session->position(),128,768,768);
  if(!s.settleFrame)s.settleFrame=LLFrameTimer::getFrameCount();
  if(LLFrameTimer::getFrameCount()-s.settleFrame<3)return false;
  require(VSVulkanContext::headlessReplay(),"Imported minimized replay requires headless isolation");
  gSavedSettings.setF32("TextureDiscardMinimizedTime",1.2f);
  gViewerWindow->getWindow()->minimize();++s.step;return false;
 }
 if(s.step==6 && resources.textureMemoryPolicy().bias<3.9)return false;
 const unsigned viewport=s.step==6?768:4096;
 const auto packets=world.packets(session->position(),128,viewport,viewport);
 const VSWorldRenderer::Packet* selected=nullptr;
 for(const auto& packet:packets)if(std::equal(packet.objectIdentity.begin(),packet.objectIdentity.end(),objectID.mData) && packet.gltfNode>=0)selected=&packet;
 if(!selected || !selected->surface)return false;
 const unsigned width=s.step==2?1024:s.step==6?VSUIRenderer::imageExtent(selected->image)[0]:512;
 if(s.step==6 && (!width || width>=512))return false;
 for(const auto& image:std::array<VSUIRenderer::Image,5>{selected->image,selected->channels[0],selected->channels[1],selected->channels[2],selected->channels[3]})
  if(!image || VSUIRenderer::imageExtent(image)[0]!=width)return false;
 if(s.step==4 && VSUIRenderer::imagePixels(selected->image)[0]!=colors[0][1][0])return false;
 const std::string phase=s.step==1?"initial":s.step==2?"full-resolution":s.step==3?"limited-again":s.step==4?"replacement":s.step==6?"minimized":"restored";
 s.evidence[phase]=observe(s,resources,*selected,phase,width,s.step>=4?1:0);
 if(s.step==6){s.evidence[phase]["bias"]=resources.textureMemoryPolicy().bias;gViewerWindow->getWindow()->restore();}
 if(s.step==7)s.evidence[phase]["restored_window"]=!gViewerWindow->getWindow()->getMinimized();
 if(s.step==1)gSavedSettings.setBOOL("TextureLoadFullRes",true);
 else if(s.step==2){gSavedSettings.setBOOL("TextureLoadFullRes",false);gSavedSettings.setBOOL("RenderAnisotropic",true);}
 else if(s.step==3)require(world.loadGLTF(*world.object(objectID),(folder/"replacement.gltf").string()),"R3 embedded glTF replacement failed");
 else if(s.step==4){s.retained.clear();s.retainedPixels.clear();}
 else if(s.step==7){LLMessageSystem* msg=gMessageSystem;msg->newMessage("KillObject");msg->nextBlock("ObjectData");msg->addU32("ID",localID);require(msg->sendReliable(session->host())>0,"R3 imported-image retirement transport failed");}
 ++s.step;s.deadline=LLFrameTimer::getTotalSeconds()+30;return false;
}
void vs_r3_gltf_retired(LLSD& report){if(!state)return;for(const auto& image:state->images)require(image.expired(),"R3 imported image survived account retirement");report["r3_gltf_images"]["account_retired"]=true;}
void vs_r3_gltf_cleanup(){state.reset();}
