"""Windows native-code microbenchmark of the production particle dispatcher.

Uses real OpenGL and the host test's minimal viewer objects. CPU comparison is
ordinary billboard expansion (legacy approximate reciprocal square root) plus
planar uploads, not a complete viewer frame. Excludes drawing, simulation, face
sorting, color conversion and viewer buffer bookkeeping from CPU measurements.
A matching point draw consumes all generated attributes after each geometry batch
on both paths; draw submission is outside the geometry CPU timer.
Never use its timings as an in-world FPS claim. --opengl selects packaged Mesa.
"""
import argparse
import ast
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--opengl', default='opengl32.dll')
args = parser.parse_args()

# Share the fixture as data without executing the host test's build/main routine.
tree = ast.parse((ROOT/'scripts/tests/test_particle_compute.py').read_text())
fixture = next(ast.literal_eval(n.value) for n in tree.body
               if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'fixture' for t in n.targets))
a = fixture.index('constexpr U32 GL_CURRENT_PROGRAM')
b = fixture.index('struct LLVector3', a)
fixture = fixture[:a] + 'bool flushed=false, setting=true;\n' + fixture[b:]
fixture = fixture.replace('return U32(i)*1024;', 'return verts*(i==0?0:i==1?16:i==2?40:i==3?44:32);')

api = r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <chrono>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <xmmintrin.h>
constexpr unsigned GL_CURRENT_PROGRAM=0x8b8d,GL_SHADER_STORAGE_BUFFER_BINDING=0x90d3,
 GL_SHADER_STORAGE_BUFFER_START=0x90d4,GL_SHADER_STORAGE_BUFFER_SIZE=0x90d5,
 GL_SHADER_STORAGE_BUFFER=0x90d2,GL_COMPUTE_SHADER=0x91b9,GL_COMPILE_STATUS=0x8b81,
 GL_LINK_STATUS=0x8b82,GL_STREAM_DRAW=0x88e0,GL_NO_ERROR=0,
 GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT=1,GL_BUFFER_UPDATE_BARRIER_BIT=0x200,
 GL_SHADER_STORAGE_BARRIER_BIT=0x2000,GL_COPY_READ_BUFFER=0x8f36,GL_COPY_WRITE_BUFFER=0x8f37,
 GL_COPY_READ_BUFFER_BINDING=0x8f36,GL_COPY_WRITE_BUFFER_BINDING=0x8f37;
#define API(ret,name,...) using name##Type=ret(APIENTRY*)(__VA_ARGS__); name##Type name=nullptr;
API(void,glGetIntegerv,unsigned,int*)
API(void,glGetIntegeri_v,unsigned,unsigned,int*)
API(void,glGetInteger64i_v,unsigned,unsigned,int64_t*)
API(void,glUseProgram,unsigned)
API(void,glBindBufferRange,unsigned,unsigned,unsigned,ptrdiff_t,ptrdiff_t)
API(void,glBindBufferBase,unsigned,unsigned,unsigned)
API(void,glBindBuffer,unsigned,unsigned)
API(unsigned,glCreateShader,unsigned)
API(void,glShaderSource,unsigned,int,const char*const*,const int*)
API(void,glCompileShader,unsigned)
API(void,glGetShaderiv,unsigned,unsigned,int*)
API(void,glGetShaderInfoLog,unsigned,int,int*,char*)
API(void,glDeleteShader,unsigned)
API(unsigned,glCreateProgram)
API(void,glAttachShader,unsigned,unsigned)
API(void,glLinkProgram,unsigned)
API(void,glGetProgramiv,unsigned,unsigned,int*)
API(void,glGetProgramInfoLog,unsigned,int,int*,char*)
API(void,glDeleteProgram,unsigned)
API(void,glGenBuffers,int,unsigned*)
API(void,glDeleteBuffers,int,const unsigned*)
API(int,glGetUniformLocation,unsigned,const char*)
API(void,glBufferData,unsigned,ptrdiff_t,const void*,unsigned)
API(void,glBufferSubData,unsigned,ptrdiff_t,ptrdiff_t,const void*)
API(void,glGetBufferSubData,unsigned,ptrdiff_t,ptrdiff_t,void*)
API(unsigned,glGetError)
API(void,glUniform1ui,int,unsigned)
API(void,glUniform4ui,int,unsigned,unsigned,unsigned,unsigned)
API(void,glUniform3fv,int,int,const float*)
API(void,glDispatchCompute,unsigned,unsigned,unsigned)
API(void,glMemoryBarrier,unsigned)
API(void,glCopyBufferSubData,unsigned,unsigned,ptrdiff_t,ptrdiff_t,ptrdiff_t)
API(void,glGenQueries,int,unsigned*)
API(void,glDeleteQueries,int,const unsigned*)
API(void,glQueryCounter,unsigned,unsigned)
API(void,glGetQueryObjectui64v,unsigned,unsigned,uint64_t*)
API(void,glGenVertexArrays,int,unsigned*)
API(void,glDeleteVertexArrays,int,const unsigned*)
API(void,glBindVertexArray,unsigned)
API(void,glEnableVertexAttribArray,unsigned)
API(void,glVertexAttribPointer,unsigned,int,unsigned,unsigned char,int,const void*)
API(void,glDrawArrays,unsigned,int,int)
API(void,glViewport,int,int,int,int)
API(void,glFinish)
API(const char*,glGetString,unsigned)
#undef API
struct Context {
 HMODULE dll; HWND window; HDC dc; HGLRC context;
 using Proc=PROC(WINAPI*)(LPCSTR); Proc proc;
 using Make=BOOL(WINAPI*)(HDC,HGLRC); Make make;
 using Delete=BOOL(WINAPI*)(HGLRC); Delete remove;
 template<class T>T get(const char* name){
  auto p=proc(name);
  if(!p || uintptr_t(p)<=3 || uintptr_t(p)==uintptr_t(-1)) p=GetProcAddress(dll,name);
  if(!p) throw std::runtime_error(name);
  return reinterpret_cast<T>(p);
 }
 Context(const wchar_t* library){
  auto path=std::filesystem::path(library).make_preferred();
  dll=path.is_absolute()
      ?LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)
      :LoadLibraryW(library);
  if(!dll) throw std::runtime_error("OpenGL DLL load failed: "+std::to_string(GetLastError()));
  proc=reinterpret_cast<Proc>(GetProcAddress(dll,"wglGetProcAddress"));
  make=reinterpret_cast<Make>(GetProcAddress(dll,"wglMakeCurrent"));
  remove=reinterpret_cast<Delete>(GetProcAddress(dll,"wglDeleteContext"));
  auto create=reinterpret_cast<HGLRC(WINAPI*)(HDC)>(GetProcAddress(dll,"wglCreateContext"));
  window=CreateWindowExW(0,L"STATIC",L"Particle benchmark",0,0,0,32,32,nullptr,nullptr,nullptr,nullptr);
  dc=GetDC(window);PIXELFORMATDESCRIPTOR p={};p.nSize=sizeof(p);p.nVersion=1;
  p.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;p.cColorBits=24;
  if(!SetPixelFormat(dc,ChoosePixelFormat(dc,&p),&p)) throw std::runtime_error("Pixel format");
  context=create(dc);if(!context || !make(dc,context)) throw std::runtime_error("WGL context");
  auto core=get<HGLRC(WINAPI*)(HDC,HGLRC,const int*)>("wglCreateContextAttribsARB");
  int attrs[]={0x2091,4,0x2092,3,0x9126,1,0};auto next=core(dc,nullptr,attrs);
  if(!next || !make(dc,next)) throw std::runtime_error("4.3 Core context");
  remove(context);context=next;
  // LOAD_APIS
 }
 ~Context(){make(nullptr,nullptr);remove(context);ReleaseDC(window,dc);DestroyWindow(window);FreeLibrary(dll);}
};
'''
names = re.findall(r'^API\([^,]+,(\w+)', api, re.M)
api = api.replace('// LOAD_APIS', '\n'.join(f'{n}=get<{n}Type>("{n}");' for n in names))

main = r'''
GLuint makeDrawProgram(){
 const char* vertex=R"(#version 430 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
layout(location=3) in vec4 glow;
layout(location=4) in vec2 uv;
out vec4 shade;
void main(){gl_Position=vec4(position.xy*.01-vec2(.5),0,1);
shade=color+glow*.1+vec4(normal*.01,0)+vec4(uv*.01,0,0);}
)";
 const char* fragment=R"(#version 430 core
in vec4 shade;out vec4 outputColor;void main(){outputColor=shade;}
)";
 GLuint result=glCreateProgram();
 for(auto stage:{std::pair<unsigned,const char*>{0x8b31,vertex},{0x8b30,fragment}}){
  GLuint shader=glCreateShader(stage.first);glShaderSource(shader,1,&stage.second,nullptr);glCompileShader(shader);
  int ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);if(!ok)throw std::runtime_error("Draw shader compile failed");
  glAttachShader(result,shader);glDeleteShader(shader);
 }
 glLinkProgram(result);int ok=0;glGetProgramiv(result,GL_LINK_STATUS,&ok);
 if(!ok)throw std::runtime_error("Draw shader link failed");return result;
}
void consume(const std::vector<LLVertexBuffer>& buffers){
 for(const auto& b:buffers){
  glBindBuffer(0x8892,b.handle);
  for(unsigned i=0;i<5;++i){
   glVertexAttribPointer(i,i==0||i==1?3:i==4?2:4,i==2||i==3?0x1401:0x1406,i==2||i==3,
    i==0||i==1?16:i==4?8:4,reinterpret_cast<const void*>(size_t(b.getOffset(i))));
  }
  glDrawArrays(0,0,b.verts);
 }
}
// Only ordinary billboards are compared here. Keep this independent from the
// compute kernel: an accidentally missing dispatch must not look like a win.
void cpuGeometry(const std::vector<LLFace*>& faces,LLVertexBuffer& buffer,std::vector<unsigned char>& bytes){
 for(size_t i=0;i<faces.size();++i){
  const auto& p=*static_cast<LLVOPartGroup*>(faces[i]->object)->group.mParticles[faces[i]->index];
  float x=p.mPosAgent.mV[0]-2,y=p.mPosAgent.mV[1]-3,z=p.mPosAgent.mV[2]-4;
  float inv=_mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x*x+y*y)));
  float rx=y*inv,ry=-x*inv;
  float ux=ry*z,uy=-rx*z,uz=rx*y-ry*x;
  inv=_mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(ux*ux+uy*uy+uz*uz)));
  ux*=inv;uy*=inv;uz*=inv;
  rx*=p.mScale.mV[0]*.5f;ry*=p.mScale.mV[0]*.5f;
  ux*=p.mScale.mV[1]*.5f;uy*=p.mScale.mV[1]*.5f;uz*=p.mScale.mV[1]*.5f;
  for(unsigned c=0;c<4;++c){
   float h=c<2?-1.f:1.f,v=c&1?-1.f:1.f;
   float pos[]={p.mPosAgent.mV[0]+v*ux+h*rx,p.mPosAgent.mV[1]+v*uy+h*ry,p.mPosAgent.mV[2]+v*uz,0};
   float normal[]={-1,0,0,0};auto vertex=i*4+c;
   std::memcpy(bytes.data()+vertex*16,pos,16);
   std::memcpy(bytes.data()+buffer.getOffset(1)+vertex*16,normal,16);
   std::memcpy(bytes.data()+buffer.getOffset(2)+vertex*4,p.mColor.mV,4);
   std::memcpy(bytes.data()+buffer.getOffset(3)+vertex*4,p.mGlow.mV,4);
  }
 }
 glBindBuffer(0x8892,buffer.handle);
 // Match contiguous dirty ranges and the viewer's 64 KiB upload granularity.
 for(auto range: {std::pair<unsigned,unsigned>{0,buffer.verts*32},
                 {buffer.getOffset(2),buffer.verts*8}})
  for(unsigned i=0;i<range.second;i+=65536)
   glBufferSubData(0x8892,range.first+i,std::min(65536u,range.second-i),bytes.data()+range.first+i);
}
double percentile(std::vector<double> v,double fraction){std::sort(v.begin(),v.end());return v[size_t((v.size()-1)*fraction)];}
int wmain(int argc,wchar_t** argv){
 try {
  if(argc!=3) return 2;
  Context context(argv[2]);
  shader_file=std::filesystem::path(argv[1]).string();
  std::cout<<"GL: "<<glGetString(0x1f02)<<" | "<<glGetString(0x1f01)<<"\n";
  if(!LLParticleCompute::initGL()) throw std::runtime_error("Required shader failed");
  GLuint draw_program=makeDrawProgram(),vao=0;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
  for(unsigned i=0;i<5;++i)glEnableVertexAttribArray(i);glViewport(0,0,32,32);glUseProgram(draw_program);
  std::cout<<"particles,groups,mode,cpu_p50_us,cpu_p95_us,complete_p50_us\n";
  for(auto shape:{std::pair<unsigned,unsigned>{1,1},{8192,1},{8192,8},{8192,64},{8192,512}}){
   const auto total=shape.first,groups=shape.second,n=total/groups;
   LLVOPartGroup object;std::vector<LLViewerPart> parts(total);std::vector<LLFace> storage(total);
   std::vector<std::vector<LLFace*>> faces(groups);
   std::vector<LLVertexBuffer> buffers(groups);std::vector<std::vector<unsigned char>> output(groups);
   for(unsigned i=0;i<total;++i){
    parts[i].mPosAgent={{float(i%101)+10,float(i%67)+20,float(i%29)+30}};
    object.group.mParticles.push_back(&parts[i]);storage[i]={&object,int(i)};faces[i/n].push_back(&storage[i]);
   }
   for(unsigned g=0;g<groups;++g){
    auto& b=buffers[g];b.verts=n*4;b.bytes=b.verts*48;glGenBuffers(1,&b.handle);glBindBuffer(0x8892,b.handle);
    output[g].resize(b.verts*48);glBufferData(0x8892,output[g].size(),output[g].data(),GL_STREAM_DRAW);
    for(unsigned v=0;v<b.verts;++v){float uv[]={float(v%4>=2),float(!(v%2))};std::memcpy(output[g].data()+b.getOffset(4)+v*8,uv,8);}
    glBufferSubData(0x8892,b.getOffset(4),b.verts*8,output[g].data()+b.getOffset(4));
   }
   // Verify actual dispatcher -> scratch -> destination copies before timing.
   for(unsigned g=0;g<groups;++g){
    cpuGeometry(faces[g],buffers[g],output[g]);
    if(!LLParticleCompute::generate(buffers[g],faces[g])) throw std::runtime_error("Queue failed");
   }
   if(!LLParticleCompute::flush()) throw std::runtime_error("Validation flush failed");
   for(unsigned g=0;g<groups;++g){
    std::vector<unsigned char> actual(output[g].size());glBindBuffer(0x8892,buffers[g].handle);
    glGetBufferSubData(0x8892,0,actual.size(),actual.data());
    for(unsigned v=0;v<buffers[g].verts;++v){
     for(unsigned component=0;component<4;++component){
      float a,b;std::memcpy(&a,actual.data()+v*16+component*4,4);
      std::memcpy(&b,output[g].data()+v*16+component*4,4);
      if(std::abs(a-b)>.003f) throw std::runtime_error("Position copy mismatch");
     }
     for(int type:{1,2,3}){
      unsigned stride=type==1?16:4,offset=buffers[g].getOffset(type)+v*stride;
      if(type==1){
       for(unsigned c=0;c<4;++c){float a,b;std::memcpy(&a,actual.data()+offset+c*4,4);std::memcpy(&b,output[g].data()+offset+c*4,4);
        if(a!=b) throw std::runtime_error("Normal copy mismatch");}
      }else if(std::memcmp(actual.data()+offset,output[g].data()+offset,stride)) throw std::runtime_error("Packed attribute copy mismatch");
     }
     float uv[2];std::memcpy(uv,actual.data()+buffers[g].getOffset(4)+v*8,8);
     if(uv[0]!=float(v%4>=2) || uv[1]!=float(!(v%2))) throw std::runtime_error("UV copy mismatch");
    }
   }
   std::array<std::vector<double>,2> cpu,total_time;
   for(int frame=-30;frame<180;++frame){
    // Alternate order to reduce driver warmup and clock-drift bias.
    for(int pass=0;pass<2;++pass){int mode=(frame+30+pass)%2;
     for(auto& p:parts)p.mPosAgent.mV[0]+=.0001f;
     glFinish();auto start=std::chrono::steady_clock::now();
     for(unsigned g=0;g<groups;++g){
      if(mode){if(!LLParticleCompute::generate(buffers[g],faces[g]))throw std::runtime_error("Dispatch failed");}
      else cpuGeometry(faces[g],buffers[g],output[g]);
     }
     if(mode && !LLParticleCompute::flush()) throw std::runtime_error("Batch flush failed");
     auto submitted=std::chrono::steady_clock::now();consume(buffers);glFinish();auto finished=std::chrono::steady_clock::now();
     if(glGetError()) throw std::runtime_error("GL error");
     if(frame>=0){
      cpu[mode].push_back(std::chrono::duration<double,std::micro>(submitted-start).count());
      total_time[mode].push_back(std::chrono::duration<double,std::micro>(finished-start).count());
     }
    }
   }
   for(int mode=0;mode<2;++mode)
    std::cout<<total<<','<<groups<<','<<(mode?"compute":"cpu-reference")<<','<<std::fixed<<std::setprecision(2)
     <<percentile(cpu[mode],.5)<<','<<percentile(cpu[mode],.95)<<','<<percentile(total_time[mode],.5)<<'\n';
   // Separate steady queued throughput from forced synchronization latency.
   // Query results are read only after the whole phase, outside CPU timing.
   for(int mode=0;mode<2;++mode){
    constexpr int frames=120;unsigned queries[frames*2];glGenQueries(frames*2,queries);
    std::vector<double> submitted_times,gpu_times;
    glFinish();auto phase=std::chrono::steady_clock::now();
    for(int frame=0;frame<frames;++frame){
     for(auto& p:parts)p.mPosAgent.mV[0]+=.0001f;
     glQueryCounter(queries[frame*2],0x8e28);
     auto begin=std::chrono::steady_clock::now();
     for(unsigned g=0;g<groups;++g){
      if(mode){if(!LLParticleCompute::generate(buffers[g],faces[g]))throw std::runtime_error("Queue failed");}
      else cpuGeometry(faces[g],buffers[g],output[g]);
     }
     if(mode && !LLParticleCompute::flush())throw std::runtime_error("Stream flush failed");
     auto end=std::chrono::steady_clock::now();consume(buffers);glQueryCounter(queries[frame*2+1],0x8e28);
     submitted_times.push_back(std::chrono::duration<double,std::micro>(end-begin).count());
    }
    glFinish();double elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-phase).count()/frames;
    for(int f=0;f<frames;++f){uint64_t a,b;glGetQueryObjectui64v(queries[f*2],0x8866,&a);glGetQueryObjectui64v(queries[f*2+1],0x8866,&b);gpu_times.push_back(double(b-a)/1000.);}
    std::cout<<"STREAM,"<<total<<','<<groups<<','<<(mode?"compute":"cpu-reference")<<','
     <<percentile(submitted_times,.5)<<','<<percentile(submitted_times,.95)<<','<<percentile(gpu_times,.5)<<','<<elapsed<<'\n';
    glDeleteQueries(frames*2,queries);
   }
   for(auto& b:buffers) glDeleteBuffers(1,&b.handle);
  }
  LLParticleCompute::destroyGL();glDeleteVertexArrays(1,&vao);glDeleteProgram(draw_program);return 0;
 } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
'''
source = (ROOT/'indra/newview/llparticlecompute.cpp').read_text()
source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
# Native driver declarations precede the small viewer fixture.
code = '#include <cstdint>\n#include <cstddef>\n#include <filesystem>\n' + api + fixture + source + main
(ROOT/'.tmp').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='particle-bench-', dir=ROOT/'.tmp') as folder:
    folder = Path(folder)
    (folder/'benchmark.cpp').write_text(code)
    (folder/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(particle_bench LANGUAGES CXX)\nadd_executable(particle_bench benchmark.cpp)\ntarget_compile_features(particle_bench PRIVATE cxx_std_17)\ntarget_link_libraries(particle_bench PRIVATE user32 gdi32)\n')
    subprocess.run(['cmake', '-S', str(folder), '-B', str(folder/'build')], check=True)
    subprocess.run(['cmake', '--build', str(folder/'build'), '--config', 'Release'], check=True)
    subprocess.run([str(folder/'build/Release/particle_bench.exe'),
                    str(ROOT/'indra/newview/app_settings/shaders/class1/objects/particleGeometryC.glsl'),
                    args.opengl], check=True)
