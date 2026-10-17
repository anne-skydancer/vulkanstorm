"""Compile the production coarse decoder/merge and prove snapshot publication semantics."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]

class SpatialRootsTest(unittest.TestCase):
    def test_coarse_snapshot_decode_and_precise_merge(self):
        compiler=shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler,"A C++ compiler is required")
        installed=ROOT/"indra/newview/vscoarselocations.h"
        header=installed if installed.exists() else Path(__file__).with_name("vscoarselocations.h")
        fixture=r'''
#include "vscoarselocations.h"
#include <cassert>
int main(){
    std::vector<VSCoarseInput<int>> rows={{1,20,30,4},{2,40,50,255},{3,60,70,0},{4,80,90,25}};
    VSCoarseSnapshot<int> snapshot;
    assert(vs_decode_coarse(rows,0,3,512,1024,{1000,2000,0},false,snapshot));
    assert(snapshot.avatars.size()==2 && snapshot.prey && (*snapshot.prey)[0]==1160);
    assert(snapshot.avatars[0].id==2 && snapshot.avatars[0].position[0]==1080 && snapshot.avatars[0].position[1]==2200);
    assert(!snapshot.avatars[0].altitudeKnown && !snapshot.avatars[1].altitudeKnown);
    assert(vs_decode_coarse(rows,0,3,512,1024,{1000,2000,0},true,snapshot) && snapshot.avatars.size()==3);
    auto prior=snapshot.avatars;
    rows[0].x=256;assert(!vs_decode_coarse(rows,0,3,512,1024,{0,0,0},true,snapshot));assert(snapshot.avatars.size()==prior.size());rows[0].x=20;
    assert(!vs_decode_coarse(rows,4,-1,512,1024,{0,0,0},true,snapshot));
    assert(!vs_decode_coarse(rows,-2,-1,512,1024,{0,0,0},true,snapshot));
    assert(!vs_decode_coarse(rows,-1,-1,511,1024,{0,0,0},true,snapshot));
    std::vector<VSCoarseSample<int>> precise={{2,{1081,2201,1500},true}};
    auto merged=vs_merge_coarse(precise,snapshot.avatars,{1081,2201,1500},2000);
    assert(merged.size()==3 && merged[0].id==2 && merged[0].position[2]==1500);
    assert(merged[1].id==3 && merged[1].position[2]==-1);
    auto high=vs_merge_coarse(std::vector<VSCoarseSample<int>>{},snapshot.avatars,{1080,2200,1500},100);
    assert(high.empty()); // Unknown coarse height must not be invented as nearby.
    rows[1].id=0;assert(vs_decode_coarse(rows,0,3,512,1024,{1000,2000,0},false,snapshot));
    assert(snapshot.avatars.size()==2); // Anonymous legacy dots retained for minimap.
    assert(vs_merge_coarse(std::vector<VSCoarseSample<int>>{},snapshot.avatars,{1080,2200,0},2000).size()==1);
    std::vector<VSCoarseInput<int>> empty;assert(vs_decode_coarse(empty,-1,-1,256,256,{0,0,0},false,snapshot));assert(snapshot.avatars.empty()&&!snapshot.prey);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/"spatial.cpp";binary=Path(directory)/"spatial.exe"
            source.write_text(fixture,encoding="utf-8")
            built=subprocess.run([compiler,"-std=c++17","-I",str(header.parent),str(source),"-o",str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_actual_session_admission_and_snapshot_retirement(self):
        compiler=shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler,"A C++ compiler is required")
        installed=ROOT/"indra/newview/vscoarselocations.h"
        header=installed if installed.exists() else Path(__file__).with_name("vscoarselocations.h")
        methods_path=ROOT/"indra/newview/vsnativecircuits.cpp" if installed.exists() else Path(__file__).with_name("spatial_session_methods.cpp")
        methods_source=methods_path.read_text(encoding="utf-8-sig")
        methods=[]
        for signature in ("std::optional<VSNativeSession::RegionSnapshot> VSNativeSession::regionSnapshot(", "std::vector<VSNativeSession::RegionSnapshot> VSNativeSession::regionSnapshots(", "std::vector<VSCoarseSample<LLUUID>> VSNativeSession::coarseAvatarLocations()", "bool VSNativeSession::receiveCoarse("):
            begin=methods_source.index(signature);start=methods_source.index("{",begin);depth=0
            for index in range(start,len(methods_source)):
                if methods_source[index]=="{":depth+=1
                elif methods_source[index]=="}":
                    depth-=1
                    if depth==0:
                        methods.append(methods_source[begin:index+1]);break
        fixture=Path(__file__).with_name("native_spatial_roots_fixture.cpp").read_text(encoding="utf-8-sig").replace("METHODS","\n".join(methods))
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/"session.cpp";binary=Path(directory)/"session.exe"
            source.write_text(fixture,encoding="utf-8")
            built=subprocess.run([compiler,"-std=c++17","-I",str(header.parent),str(source),"-o",str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

if __name__=="__main__":unittest.main()
