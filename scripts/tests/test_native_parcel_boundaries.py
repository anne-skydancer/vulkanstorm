from native_source_fixture import production_sources as compose,ROOT,HERE,method
"""Execute actual immutable visual-input ownership and shared collision expiration."""
from pathlib import Path
import re,shutil,subprocess,tempfile,unittest
class ParcelBoundaries(unittest.TestCase):
 def test_actual_geometry_publication_and_shared_expiration(self):
  changes=compose();source=changes['indra/newview/llviewerparcelmgr.cpp'];header=changes['indra/newview/llviewerparcelmgr.h']
  methods='\n'.join(method(source,s) for s in ['std::function<bool()> LLViewerParcelMgr::selectionGuard() const',
   'bool LLViewerParcelMgr::collisionVisible()', 'std::optional<LLViewerParcelMgr::NativeParcelBoundary> LLViewerParcelMgr::nativeSelectionBoundary() const',
   'std::optional<LLViewerParcelMgr::NativeParcelBoundary> LLViewerParcelMgr::nativeCollisionBoundary()'])
  descriptor=method(header,'    struct NativeParcelBoundary')+';'
  fixture=(HERE/'native_parcel_replies_fixture.cpp').read_text().split('int main(){')[0]
  fixture=fixture.replace('PREHASH','').replace('PRODUCTION',methods)
  # Boundary dependency doubles contain no rendering, GL surface or legacy region.
  fixture=fixture.replace('struct VSRegionContext {','''struct LLVector3d {double x=0,y=0,z=0;bool operator==(const LLVector3d& o)const{return x==o.x && y==o.y && z==o.z;}};
struct VSParcelBitmap {using Snapshot=std::shared_ptr<const int>;};
struct VSRegionContext {LLVector3d getPosGlobalFromRegion(LLVector3d v)const{return v;}''',1)
  fixture=fixture.replace('struct LLParcel {','struct LLParcel {bool pass=false;LLVector3d getAABBMin()const{return {};}LLVector3d getAABBMax()const{return {256,256,0};}bool getParcelFlag(int)const{return pass;}',1)
  settings='''struct Settings {bool render=true;int mode=1;bool getBOOL(const char*)const{return render;}}gSavedSettings;
template<class T>struct LLCachedControl {Settings&settings;LLCachedControl(Settings&s,const char*,T):settings(s){}operator T()const{return T(settings.mode);}};
struct Timer {float elapsed=0;float getElapsedTimeF32()const{return elapsed;}};
bool gDisconnected=false;
constexpr int PF_USE_PASS_LIST=1;
constexpr float PARCEL_COLLISION_DRAW_SECS_ON_PROXIMITY=1.f,PARCEL_BAN_LINES_DRAW_SECS_ON_COLLISION=10.f;
'''
  fixture=fixture.replace('struct LLViewerParcelMgr {',settings+'\nstruct LLViewerParcelMgr {\n'+descriptor+'''
 static constexpr int PARCEL_BAN_LINES_ON_COLLISION=1,PARCEL_BAN_LINES_ON_PROXIMITY=2,PARCEL_BAN_LINES_HIDE=0;
 bool mRenderSelection=true,mRenderCollision=true;
 Timer mCollisionTimer;int mCollisionBanned=2;
 LLVector3d mWestSouth{0,0,0},mEastNorth{256,256,0};
 VSParcelBitmap::Snapshot mNativeSelectionBitmap,mNativeCollisionBitmap=std::make_shared<const int>(1);
 std::optional<VSRegionContext>mNativeCollisionRegion=VSRegionContext{};
 LLParcel collision;LLParcel*mCollisionParcel=&collision;
 bool collisionVisible();
 std::optional<NativeParcelBoundary>nativeSelectionBoundary()const;
 std::optional<NativeParcelBoundary>nativeCollisionBoundary();
''',1)
  fixture+='''int main(){
 LLViewerParcelMgr owner;LLViewerParcelMgr::owner=&owner;
 auto selection=owner.nativeSelectionBoundary();assert(selection && !selection->bitmap && selection->current());
 owner.mWestSouth.x=4;assert(!selection->current());owner.mWestSouth.x=0;
 selection=owner.nativeSelectionBoundary();owner.mNativeSelectionBitmap=std::make_shared<const int>(3);assert(!selection->current());
 selection=owner.nativeSelectionBoundary();assert(selection->bitmap==owner.mNativeSelectionBitmap && selection->current());
 owner.mRenderSelection=false;assert(!selection->current() && !owner.nativeSelectionBoundary());owner.mRenderSelection=true;
 gSavedSettings.render=false;assert(!selection->current() && !owner.nativeSelectionBoundary());gSavedSettings.render=true;
 owner.mRequestResult=PARCEL_RESULT_NO_DATA;assert(!selection->current() && !owner.nativeSelectionBoundary());owner.mRequestResult=0;
 auto collision=owner.nativeCollisionBoundary();assert(collision && collision->collision && collision->current());
 owner.collision.pass=true;assert(!collision->current());collision=owner.nativeCollisionBoundary();assert(collision->usePass);
 owner.mCollisionTimer.elapsed=10.f;assert(collision->current());owner.mCollisionTimer.elapsed=10.1f;assert(!collision->current() && !owner.mRenderCollision);
 owner.mRenderCollision=true;owner.mCollisionTimer.elapsed=1.f;gSavedSettings.mode=2;collision=owner.nativeCollisionBoundary();assert(collision && collision->current());
 owner.mCollisionTimer.elapsed=1.1f;assert(!collision->current() && !owner.nativeCollisionBoundary());
 owner.mRenderCollision=true;owner.mCollisionTimer.elapsed=0;gSavedSettings.mode=0;assert(!owner.nativeCollisionBoundary());gSavedSettings.mode=1;
 collision=owner.nativeCollisionBoundary();selection=owner.nativeSelectionBoundary();assert(collision && selection);++epoch;assert(!collision->current() && !selection->current());
}'''
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'boundary.cpp';exe=Path(tmp)/'boundary.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17','-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
