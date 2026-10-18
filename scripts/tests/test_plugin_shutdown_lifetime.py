"""Exercise the production plugin-shell lifetime against a real asynchronous DSO."""
from pathlib import Path
import os
import shutil
import signal
import subprocess
import tempfile
import unittest


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class PluginShutdownLifetimeTest(unittest.TestCase):
    def test_goodbye_retains_code_until_process_exit_while_worker_finishes(self):
        root = Path(__file__).resolve().parents[2]
        if not (root/'indra').exists():  # Allows off-tree review before integration.
            root = Path(__file__).resolve().parents[2].parent
        source = (root/'indra/llplugin/llpluginprocesschild.cpp').read_text()
        header = (root/'indra/llplugin/llpluginprocesschild.h').read_text()
        complete_field = next(line.strip() for line in header.splitlines() if 'bool mPluginShutdownComplete' in line)
        branch = source.split('        case STATE_UNLOADED:', 1)[1].split('        case STATE_ERROR:', 1)[0]
        destructor = function(source, 'LLPluginProcessChild::~LLPluginProcessChild()')
        destructor = destructor[destructor.index('{'):]
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        worker = r'''
#ifdef _WIN32
#include <windows.h>
static HANDLE wakeEvent,doneEvent;
static DWORD WINAPI work(void*){WaitForSingleObject(wakeEvent,INFINITE);SetEvent(doneEvent);return 0;}
extern "C" __declspec(dllexport) void startWorker(HANDLE wake,HANDLE done){wakeEvent=wake;doneEvent=done;CreateThread(nullptr,0,work,nullptr,0,nullptr);}
#else
#include <pthread.h>
#include <unistd.h>
static int wakeFD,doneFD;
static void* work(void*){char byte;if(read(wakeFD,&byte,1)==1)write(doneFD,&byte,1);return nullptr;}
extern "C" void startWorker(int wake,int done){wakeFD=wake;doneFD=done;pthread_t worker;pthread_create(&worker,nullptr,work,nullptr);}
#endif
'''
        fixture = r'''
#include <cstdlib>
#include <cassert>
int cleanupRequests=0,expectedCleanup=0;bool protocolComplete=false;
static void verifyCleanup(){assert(cleanupRequests==expectedCleanup);}
#ifdef _WIN32
#include <windows.h>
struct LLPluginInstance{HMODULE module;~LLPluginInstance(){FreeLibrary(module);}};
#else
#include <dlfcn.h>
#include <poll.h>
#include <sys/resource.h>
#include <unistd.h>
struct LLPluginInstance{void*module;~LLPluginInstance(){dlclose(module);}};
#endif
struct LLPluginMessage{LLPluginMessage(const char*,const char*){}};
struct LLPluginProcessChild {
 enum State{STATE_UNLOADED,STATE_DONE};State state=STATE_UNLOADED;LLPluginInstance*mInstance;
 COMPLETE_FIELD
 ~LLPluginProcessChild() DESTRUCTOR
 void killSockets(){}void setState(State next){state=next;}
 void sendMessageToPlugin(LLPluginMessage){++cleanupRequests;if(protocolComplete)std::abort();}
 void idle(){switch(state){case STATE_UNLOADED: BRANCH default:break;}}
};
int main(int argc,char**argv){
 assert(argc==2||argc==3);std::atexit(verifyCleanup);
#ifdef _WIN32
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 HMODULE module=LoadLibraryA(argv[1]);assert(module);
 auto start=reinterpret_cast<void(*)(HANDLE,HANDLE)>(GetProcAddress(module,"startWorker"));assert(start);
 HANDLE wake=CreateEventW(nullptr,TRUE,FALSE,nullptr),done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 if(argc==2)start(wake,done);
#else
 rlimit limit{0,0};setrlimit(RLIMIT_CORE,&limit);
 void*module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(module);
 auto start=reinterpret_cast<void(*)(int,int)>(dlsym(module,"startWorker"));assert(start);
 int wake[2],done[2];assert(pipe(wake)==0&&pipe(done)==0);if(argc==2)start(wake[0],done[1]);
#endif
 LLPluginProcessChild child;child.mInstance=new LLPluginInstance{module};
 if(argc==3){expectedCleanup=1;child.state=LLPluginProcessChild::STATE_DONE;return 0;}
 protocolComplete=true;child.idle();
 assert(child.state==LLPluginProcessChild::STATE_DONE);
#ifdef _WIN32
 SetEvent(wake);if(WaitForSingleObject(done,3000)!=WAIT_OBJECT_0)return 23;
 assert(GetModuleHandleA(argv[1])==module);CloseHandle(wake);CloseHandle(done);
#else
 char byte=1;assert(write(wake[1],&byte,1)==1);pollfd poller{done[0],POLLIN,0};
 if(poll(&poller,1,3000)!=1)return 23;assert(read(done[0],&byte,1)==1);
 // RTLD_NOLOAD proves the successful worker returned from still-owned code.
 assert(dlopen(argv[1],RTLD_NOW|RTLD_NOLOAD)!=nullptr);
 for(int fd:wake)close(fd);for(int fd:done)close(fd);
#endif
 // The actual shell destructor exits without releasing code underneath workers.
}
'''.replace('DESTRUCTOR', destructor).replace('COMPLETE_FIELD', complete_field)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            worker_source = directory/'worker.cpp'; worker_source.write_text(worker)
            library = directory/('worker.dll' if os.name == 'nt' else 'worker.so')
            shared_flags = ['-shared', '-static'] if os.name == 'nt' else ['-shared', '-fPIC', '-pthread']
            subprocess.run([compiler, *shared_flags, str(worker_source), '-o', str(library)], check=True, capture_output=True, timeout=120)
            # The historical branch is a negative control, isolated in its own
            # process: it must reproduce the actual execute-after-unload fault.
            historical = 'killSockets(); delete mInstance; mInstance = nullptr; setState(STATE_DONE); break;'
            for name, body in [('historical', historical), ('production', branch)]:
                path = directory/f'{name}.cpp'; path.write_text(fixture.replace('BRANCH', body))
                executable = directory/(name + ('.exe' if os.name == 'nt' else ''))
                flags = ['-static'] if os.name == 'nt' else ['-ldl', '-pthread']
                built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(executable), *flags], capture_output=True, text=True, timeout=120)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                kwargs = {'creationflags': subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {}
                run = subprocess.run([str(executable), str(library)], capture_output=True, timeout=15, **kwargs)
                if name == 'historical':
                    expected = 0xc0000005 if os.name == 'nt' else -signal.SIGSEGV
                    actual = run.returncode & 0xffffffff if os.name == 'nt' else run.returncode
                    self.assertEqual(actual, expected, run.stdout + run.stderr)
                else:
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    # STATE_DONE is also reached on error; only a completed
                    # protocol suppresses the destructor's cleanup request.
                    error = subprocess.run([str(executable), str(library), 'error'], capture_output=True, timeout=15, **kwargs)
                    self.assertEqual(error.returncode, 0, error.stdout + error.stderr)


if __name__ == '__main__':
    unittest.main()
