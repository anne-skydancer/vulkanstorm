"""Exercise production mesh admission ordering against upstream behavior."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "indra/newview/llmeshrepository.cpp").read_text()
start = source.index("            const size_t admission_count =")
end = source.index("            for (size_t index =", start)
selection = source[start:end]
start = source.index("            const unsigned lane = remaining[1]")
worker = source[start:source.index("\n", start)]
fixture = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <memory>
#include <vector>
using std::size_t;
template<class T> T llmax(T a, T b) { return std::max(a,b); }
struct Request {
 int score, refreshed, checks=0;
 void checkScore() { score=refreshed; ++checks; }
 int getScore() const { return score; }
};
using Queue=std::vector<std::shared_ptr<Request>>;
size_t select(Queue& mPendingRequests, int push_count) {
""" + selection + r"""
 return admission_count;
}
unsigned workerLane(std::array<size_t,4> remaining) {
""" + worker + r"""
 return lane;
}
int main() {
 Queue q;
 for (int score : {1,90,20,100,50}) q.push_back(std::make_shared<Request>(Request{0,score}));
 const auto original=q;
 assert(select(q,0)==0 && q==original);
 assert(select(q,-1)==0 && q==original);
 for(auto& r:q) assert(r->checks==0);
 assert(select(q,2)==2);
 assert(q[0]->score==100 && q[1]->score==90);
 for(auto& r:q) assert(r->checks==1);
 q.erase(q.begin(),q.begin()+2);
 assert(q.size()==3);
 assert(select(q,1)==1 && q[0]->score==50);
 q=original;
 assert(select(q,5)==5 && q==original);
 assert(select(q,80)==5 && q==original);
 q.clear(); assert(select(q,80)==0);
 std::array<size_t,4> remaining{2,2,2,0};
 for(unsigned expected : {1,1,2,2,0,0}) {
  auto lane=workerLane(remaining); assert(lane==expected); --remaining[lane];
 }
 assert(workerLane(remaining)==4);
}
"""
with tempfile.TemporaryDirectory(prefix="mesh-priority-") as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(fixture)
    (path / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\nproject(mesh_priority LANGUAGES CXX)\n"
        "add_executable(mesh_priority test.cpp)\ntarget_compile_features(mesh_priority PRIVATE cxx_std_17)\n")
    subprocess.run(["cmake", "-S", str(path), "-B", str(path / "build")], check=True)
    subprocess.run(["cmake", "--build", str(path / "build"), "--config", "Debug"], check=True)
    executable = path / "build" / ("Debug/mesh_priority.exe" if sys.platform == "win32" else "mesh_priority")
    subprocess.run([str(executable)], check=True)
