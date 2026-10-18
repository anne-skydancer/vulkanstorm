"""Actual parcel overlay crossing kernel versus the unchanged reference controller."""
from pathlib import Path
import os,re,unittest
from test_native_material_selection import body,run
ROOT=Path(os.environ.get('VS_PARCEL_BOUNDS_ROOT',Path(__file__).resolve().parents[2]));OVERLAY=Path(os.environ['VS_PARCEL_BOUNDS_PROPOSED'])if 'VS_PARCEL_BOUNDS_PROPOSED'in os.environ else None
def source(path):
 p=OVERLAY/path if OVERLAY else ROOT/path
 return(p if p.exists()else ROOT/path).read_text()
class NativeParcelBoundsTests(unittest.TestCase):
 def test_actual_kernel_matches_original_square_and_handles_rectangular_regions(self):
  actual=source('indra/newview/vsparcelbounds.h');actual=re.sub(r'^#.*$','',actual,flags=re.M)
  original=body(source('indra/newview/llviewerparceloverlay.cpp'),'bool LLViewerParcelOverlay::encroachesOnNearbyParcel(')
  run(self,r'''
#include <cassert>
#include <vector>
#include <algorithm>
#include <random>
using S32=int;using U32=unsigned;using U8=unsigned char;using F32=float;
constexpr int VX=0,VY=1;constexpr float PARCEL_GRID_STEP_METERS=4,REGION_WIDTH_METERS=256;constexpr U8 PARCEL_WEST_LINE=64,PARCEL_SOUTH_LINE=128;
template<class T>T llclamp(T v,T a,T b){return std::clamp(v,a,b);}
struct LLVector3{float mV[3];};struct LLBBox{LLVector3 a,b;LLVector3 getMinAgent()const{return a;}LLVector3 getMaxAgent()const{return b;}};
ACTUAL
struct Reference{int mParcelGridsPerEdge=64;std::vector<U8>mOwnership=std::vector<U8>(4096);bool crosses(const std::vector<LLBBox>&boxes)const ORIGINAL};
int main(){Reference ref;std::mt19937 random(4771);for(auto&v:ref.mOwnership)v=U8(random());auto cell=[&](int r,int c){return ref.mOwnership.at(r*64+c);};
 for(int i=0;i<4000;++i){float x=float(random()%272)-8,y=float(random()%272)-8,w=float(random()%30),h=float(random()%30);if(x+w==256||y+h==256)continue;std::vector<LLBBox>boxes{{{x,y,0},{x+w,y+h,1}}};assert(ref.crosses(boxes)==vs_parcel_bounds_cross(boxes,256,256,cell));}
 assert(!vs_parcel_bounds_cross({},256,256,cell));
 std::vector<U8>wide(128*64,0);auto rectangular=[&](int r,int c){assert(r>=0&&r<64&&c>=0&&c<128);return wide.at(r*128+c);};
 std::vector<LLBBox>box{{{300,20,0},{310,24,1}}};assert(!vs_parcel_bounds_cross(box,512,256,rectangular));wide[5*128+77]=PARCEL_WEST_LINE;assert(vs_parcel_bounds_cross(box,512,256,rectangular));wide.assign(wide.size(),0);box={{{510,2,0},{513,3,1}}};assert(vs_parcel_bounds_cross(box,512,256,rectangular));
 box={{{508,252,0},{512,256,0}}};assert(!vs_parcel_bounds_cross(box,512,256,rectangular));
 box={{{0,0,0},{4,4,0}}};wide[128]=PARCEL_SOUTH_LINE;assert(vs_parcel_bounds_cross(box,512,256,rectangular));
}
'''.replace('ACTUAL',actual).replace('ORIGINAL',original))
 def test_selection_uses_actual_root_and_child_identity_and_current_region(self):
  actual=body(source('indra/newview/vseditselection.cpp'),'bool VSEditSelection::objectsCrossParcel()')
  self.assertIn('if (!mCurrent()) return false;',actual);self.assertIn('mRoots',actual);self.assertIn('object->getChildren()',actual);self.assertIn('object->task().getHost()',actual);self.assertIn('region->objectsCrossParcel({bounding})',actual);self.assertLess(actual.index('LLBBox bounding;'),actual.index('for (const auto* node'))
if __name__=='__main__':unittest.main()
