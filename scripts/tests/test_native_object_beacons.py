"""Differential original pipeline predicates/geometry and true source consumers."""
from pathlib import Path
import os,unittest
from test_native_material_selection import body,run
ROOT=Path(os.environ.get('VS_BEACON_ROOT',Path(__file__).resolve().parents[2]));OVERLAY=Path(os.environ['VS_BEACON_PROPOSED'])if 'VS_BEACON_PROPOSED'in os.environ else None
def source(path):
 p=OVERLAY/path if OVERLAY else ROOT/path
 return(p if p.exists()else ROOT/path).read_text()
class NativeObjectBeaconTests(unittest.TestCase):
 def test_categories_differential_against_actual_original_pipeline_all_source_flags(self):
  pipeline=source('indra/newview/pipeline.cpp');names=['renderScriptedTouchBeacons','renderScriptedBeacons','renderPhysicalBeacons','renderMOAPBeacons','renderParticleBeacons','renderSoundHighlights'];original='\n'.join('void '+name+'(LLDrawable*drawablep)'+body(pipeline,'void '+name+'(')for name in names)
  policy=source('indra/newview/vsobjectbeaconpolicy.h').replace('#pragma once','')
  run(self,r'''#include <cassert>
#include <array>
#include <vector>
using S32=int;using U8=unsigned char;
struct LLColor4{float c[4];LLColor4(float x,float y,float z,float w):c{x,y,z,w}{}};
struct LLFace{};struct TE{bool media=false;bool hasMedia()const{return media;}};
struct LLViewerObject{unsigned flags;bool isAvatar()const{return flags&1;}bool getParent()const{return flags&2;}bool flagScripted()const{return flags&4;}bool flagHandleTouch()const{return flags&8;}bool flagUsePhysics()const{return flags&16;}bool hasMedia()const{return flags&32;}bool isParticleSource()const{return flags&64;}bool isAudioSource()const{return flags&128;}int getPositionAgent()const{return 0;}U8 getNumTEs()const{return 1;}TE getTEref(int)const{return TE{hasMedia()};}};
struct LLDrawable{LLViewerObject object;LLFace face;auto getVObj(){return &object;}int getNumFaces(){return 1;}LLFace*getFace(int){return &face;}};
struct Pipeline{bool sRenderBeacons=true,sRenderHighlight=true;std::vector<LLFace*>mHighlightFaces;}gPipeline;
struct LLPipeline{static constexpr int DebugBeaconLineWidth=1;};struct ObjectList{std::vector<LLColor4>beacons;void addDebugBeacon(int,const char*,LLColor4 c,LLColor4,int){beacons.push_back(c);}}gObjectList;
ORIGINAL
POLICY
int main(){for(unsigned flags=0;flags<256;++flags)for(unsigned settings=0;settings<64;++settings){VSObjectBeaconFlags selected{bool(settings&1),bool(settings&2),bool(settings&4),bool(settings&8),bool(settings&16),bool(settings&32)};LLDrawable drawable{{flags},{}};const auto actual=vs_object_beacon_categories(drawable.object,selected);for(unsigned category=0;category<5;++category){gPipeline.mHighlightFaces.clear();gObjectList.beacons.clear();switch(category){case 0:if(selected.touch)renderScriptedTouchBeacons(&drawable);else if(selected.scripted)renderScriptedBeacons(&drawable);break;case 1:if(selected.physical)renderPhysicalBeacons(&drawable);break;case 2:if(selected.media)renderMOAPBeacons(&drawable);break;case 3:if(selected.particles)renderParticleBeacons(&drawable);break;case 4:if(selected.sound)renderSoundHighlights(&drawable);break;}assert(actual[category]==!gPipeline.mHighlightFaces.empty());if(category<4)assert(actual[category]==!gObjectList.beacons.empty());else assert(gObjectList.beacons.empty());}}}
'''.replace('ORIGINAL',original).replace('POLICY',policy))
 def test_box_vertices_equal_original_for_rectangular_and_translated_bounds(self):
  cpp=source('indra/newview/llspatialpartition.cpp');original=body(cpp,'void drawBoxOutline(const LLVector3& pos, const LLVector3& size)');helper=source('indra/newview/vsdebugbeacongeometry.h');helper=helper[helper.index('// Original LLSpatialPartition::drawBoxOutline'):]
  run(self,r'''#include <cassert>
#include <array>
#include <vector>
#include <cmath>
using F32=float;
struct LLVector3{float mV[3];LLVector3(float x,float y,float z):mV{x,y,z}{}bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1])&&std::isfinite(mV[2]);}LLVector3 scaledVec(LLVector3 b)const{return{mV[0]*b.mV[0],mV[1]*b.mV[1],mV[2]*b.mV[2]};}LLVector3 operator+(LLVector3 b)const{return{mV[0]+b.mV[0],mV[1]+b.mV[1],mV[2]+b.mV[2]};}LLVector3 operator-(LLVector3 b)const{return{mV[0]-b.mV[0],mV[1]-b.mV[1],mV[2]-b.mV[2]};}};
struct LLRender{enum{LINES};};struct Sink{std::vector<std::array<float,3>>v;void begin(int){}void end(){}void vertex3fv(const float*p){v.push_back({p[0],p[1],p[2]});}}gGL;
void reference(const LLVector3&pos,const LLVector3&size)ORIGINAL
HELPER
int main(){for(float x:{-12.f,0.f,33.f})for(float y:{0.f,3.f,14.f}){LLVector3 p{x,y,-8},size{2,5,7};gGL.v.clear();reference(p,size);Sink native;vs_debug_box_outline(native,p,size);assert(native.v==gGL.v&&native.v.size()==24);}LLVector3 nan{NAN,0,0},size{2,4,7};gGL.v.clear();reference(nan,size);Sink native;vs_debug_box_outline(native,nan,size);assert(native.v==gGL.v&&native.v.empty());}
'''.replace('ORIGINAL',original).replace('HELPER',helper))
 def test_actual_sources_and_constant_shader_do_not_reuse_surface_or_vertex_colors(self):
  scene=source('indra/newview/vsworldscene.cpp');actual=body(scene,'void VSWorldScene::appendObjectBeacons(');
  # Rows are keyed by the submitted object generation and admitted only through packetObject,
  # which requires the packet's actual object generation and face revision to be current.
  self.assertIn('std::make_pair(identity,packet.objectGeneration)',actual);self.assertIn('const auto token=packetObject(packet);',actual);self.assertIn('if (!token || !current(*token)) continue;',actual)
  admitted=body(scene,'VSWorldScene::packetObject(const VSWorldRenderer::Packet& packet)const');self.assertIn('token->generation!=packet.objectGeneration',admitted);self.assertIn('!current(FaceHandle{*token,packet.faceIndex,packet.faceGeneration})',admitted)
  self.assertIn('packet.constantColor=true',actual);self.assertIn('packet.color={119.f/255.f,0,0,.5f}',actual);self.assertIn('packet.surface.reset()',actual);self.assertIn('if (!rigged)',actual)
  shader=source('indra/newview/vsworldrenderer.cpp');self.assertIn('if (p.constantColor) constants.surfaceFlags[0]=-2;',shader);self.assertLess(shader.index('if (surfaceFlags.x<-1.5)'),shader.index('vec4 c=sampleColor*tint*faceColor;'))
  presentation=source('indra/newview/vsbeaconpresentation.cpp');self.assertIn('gAudiop->sourcePositions()',presentation);self.assertIn('region->getHeight()',presentation);self.assertIn('boxMeshes.erase(it)',presentation);self.assertIn('source.hud?LLVector3(center.data())',presentation)
if __name__=='__main__':unittest.main()
