"""Compare native texture pressure with the actual shared OpenGL reference calculation."""
from pathlib import Path
import shutil,subprocess,tempfile
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'indra/newview/llviewertexture.cpp').read_text()
start=source.index('    // This is a divisor used to determine how much VRAM')
end=source.index('    // set to max discard bias if the window',start)
reference=source[start:end]
controller=method((ROOT/'indra/newview/vsuiresources.cpp').read_text(),'void VSUIResources::updateTextureMemory(')
fixture=r'''
#include "vstexturememory.h"
#include <cassert>
#include <map>
#include <string>
#include <sstream>
#include <type_traits>
#include <vector>
using F32=float;using F64=double;using U32=unsigned;
using S32Megabytes=double;
template<class A,class B> auto llmax(A a,B b){using C=std::common_type_t<A,B>;return std::max(C(a),C(b));}
template<class A,class B> auto llmin(A a,B b){using C=std::common_type_t<A,B>;return std::min(C(a),C(b));}
auto ll_round(double v){return std::round(v);}
std::map<std::string,double> gSavedSettings;
template<class T> struct LLCachedControl {
 std::string name;LLCachedControl(std::map<std::string,double>&,const char* s):name(s){}
 LLCachedControl(std::map<std::string,double>&,const char* s,T fallback):name(s){gSavedSettings.try_emplace(s,fallback);}
 operator T()const{return T(gSavedSettings.at(name));}T operator()()const{return T(*this);}
};
VSTextureMemory::Input input;
struct Image {static double getTextureBytesAllocated(){return double(input.gpuBytes);}};
using LLImageGL=Image;
struct LLVertexBuffer {static double getBytesAllocated(){return double(input.geometryBytes);}};
struct {F32 mVRAM=8192;}gGLManager;
struct Timer {double start=0;double getElapsedTimeF32(){return input.now-start;}void reset(){start=input.now;}}sEvaluationTimer;
F32 sDesiredDiscardBias=0,sFreeVRAMMegabytes=0,gFrameIntervalSeconds=0;
constexpr F32 MIN_VRAM_BUDGET=768,MEMORY_CHECK_WAIT_TIME=1;
bool isSystemMemoryLow(){return input.systemLow;}
F32 getSystemMemoryBudgetFactor(){return F32(input.systemFactor);}
double getFreeSystemMemory(){return input.freeSystemMiB;}
// Firestorm 7.2.5: the budget factor moved to LLMemory, and recovery became
// isSystemMemoryRecovered(); off Windows that is the former free-memory test
// (RenderMinFreeMainMemoryThreshold plus a 100 MiB margin).
struct LLMemory {static F32 getSystemMemoryBudgetFactor(){return ::getSystemMemoryBudgetFactor();}};
bool isSystemMemoryRecovered(){gSavedSettings.try_emplace("RenderMinFreeMainMemoryThreshold",512);return getFreeSystemMemory()>gSavedSettings.at("RenderMinFreeMainMemoryThreshold")+100;}
struct List:std::vector<int>{void updateImageDecodePriority(int&,bool){}}gTextureList;
std::ostringstream logOutput;
#define LL_WARNS(...) logOutput
#define LL_DEBUGS(...) logOutput
#define LL_ENDL std::endl
void reference(){
REFERENCE
 sDesiredDiscardBias=std::clamp(sDesiredDiscardBias,1.f,4.f);
}
struct VSUIResources {
 struct Stats {std::uint64_t residentGPUImageBytes=0,remoteImageBudgetBytes=512*1024*1024,remoteImagePayloadBytes=0,remoteImageAllocationRefusals=0;};
 struct Renderer {Stats stats;Stats statistics(){return stats;}};
 struct Impl {Renderer renderer;VSTextureMemory::Policy textureMemory;std::uint64_t lastRemoteRefusals=0;
  bool remotePressure=false;double remotePressureReleaseBytes=0;} storage;
 Impl* mImpl=&storage;
 void updateTextureMemory(const VSTextureMemory::Settings&,VSTextureMemory::Input);
};
CONTROLLER
int main(){
 gSavedSettings={{"RenderTextureVRAMDivisor",2},{"RenderMaxVRAMBudget",2048},{"FSLimitTextureVRAMUsage",0}};
 VSTextureMemory::Settings settings;settings.detectedMiB=8192;settings.manualMiB=2048;
 VSTextureMemory::Policy policy;
 for(unsigned frame=0;frame<1000;++frame){
  input.now=frame*.02;input.seconds=.02;input.freeSystemMiB=16000;input.systemFactor=1.8;
  input.gpuBytes=std::uint64_t((frame>=50&&frame<400?2000:200)*1024ull*1024ull);
  input.geometryBytes=40*1024*1024;input.systemLow=frame>=500&&frame<700;
  if(frame==200){settings.manual=true;gSavedSettings["FSLimitTextureVRAMUsage"]=1;}
  if(frame==300){settings.manualMiB=1024;gSavedSettings["RenderMaxVRAMBudget"]=1024;}
  gFrameIntervalSeconds=F32(input.seconds);reference();policy.update(settings,input);
  assert(std::abs(policy.freeMiB-sFreeVRAMMegabytes)<.01);
  assert(std::abs(policy.bias-sDesiredDiscardBias)<.0001);
 }
 assert(policy.dimension(1024,5)==1024);
 policy.bias=4;assert(policy.dimension(1024,5)==256);
 policy.bias=1.5;assert(policy.dimension(1024,5)==1024);
 input.background=true;input.minimized=true;input.now=30;policy.update(settings,input);assert(policy.bias==4);
 input.background=false;input.now=31;policy.update(settings,input);assert(policy.bias<4);
 VSUIResources resources;auto& stats=resources.storage.renderer.stats;
 stats.remoteImagePayloadBytes=340*1024*1024;stats.remoteImageAllocationRefusals=1;
 input={};input.now=1;input.seconds=.02;input.freeSystemMiB=16000;
 resources.updateTextureMemory(settings,input);assert(resources.storage.remotePressure);
 for(unsigned frame=0;frame<500;++frame){input.now+=.02;resources.updateTextureMemory(settings,input);}
 assert(resources.storage.remotePressure&&resources.storage.textureMemory.bias>1.9);
 stats.remoteImagePayloadBytes=200*1024*1024;resources.updateTextureMemory(settings,input);
 assert(!resources.storage.remotePressure);
 stats.remoteImagePayloadBytes=0;resources.updateTextureMemory(settings,input);assert(!resources.storage.remotePressure);
 settings.manual=false;settings.detectedMiB=8192;settings.overrideMiB=4096;
 resources.updateTextureMemory(settings,input);assert(resources.storage.textureMemory.budgetMiB==2048);

}
'''.replace('REFERENCE',reference).replace('CONTROLLER',controller)
with tempfile.TemporaryDirectory() as temporary:
 p=Path(temporary)/'memory.cpp';p.write_text(fixture);exe=Path(temporary)/'memory.exe'
 command=[shutil.which('clang++') or shutil.which('g++'),'-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT/'indra/newview'),str(p),'-o',str(exe)]
 result=subprocess.run(command,capture_output=True,text=True);print(result.stdout+result.stderr)
 if result.returncode:raise SystemExit(result.returncode)
 result=subprocess.run([str(exe)],capture_output=True,text=True);print(result.stdout+result.stderr)
 if not result.returncode:print('PASS: 1000 reference frames, pressure admission/recovery, override and background policies')
 raise SystemExit(result.returncode)

