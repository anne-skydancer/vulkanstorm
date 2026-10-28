"""Execute the actual Win32 DPI case with distinct outer/client extents."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]


class DpiClientExtentTest(unittest.TestCase):
    def test_drawable_extent_is_published_after_resize(self):
        source=(ROOT/'indra/llwindow/llwindowwin32.cpp').read_text()
        case=source[source.index('        case WM_DPICHANGED:'):source.index('        case WM_DISPLAYCHANGE:')]
        fixture=r'''
#include <cassert>
#include <cstdint>
#include <functional>
#include <vector>
using S32=int;using F32=float;
struct RECT {int left,top,right,bottom;};using LPRECT=RECT*;
RECT actual{};bool queried=false;bool readable=true;int notified=0,w=0,h=0;float scale=0;
std::vector<std::function<void()>> pending;
struct Callbacks {void handleDPIChanged(void*,float s,int x,int y){++notified;scale=s;w=x;h=y;}} cb;
struct WindowThread {void pingWindowTimeout(const char*){}} windowThread; // Firestorm 7.2.5: WM_DPICHANGED pings the window watchdog
struct Window {Callbacks* mCallbacks=&cb;WindowThread* mWindowThread=&windowThread;} instance;
void SetWindowPos(int,int,int,int,int width,int height,int){assert(!queried);actual={0,0,width-16,height-39};}
bool GetClientRect(int,RECT* out){queried=true;*out=actual;return readable;}
#define LL_PROFILE_ZONE_NAMED_CATEGORY_WIN32(x)
#define WM_DPICHANGED 1
#define HWND_TOP 0
#define SWP_NOZORDER 1
#define SWP_NOACTIVATE 2
#define USER_DEFAULT_SCREEN_DPI 96
#define LOWORD(x) ((x)&65535)
#define WINDOW_IMP_POST(expression) pending.emplace_back([=]{expression;})
int dispatch(int message,unsigned w_param,std::intptr_t l_param){
  int h_wnd=1;Window* window_imp=&instance;
  switch(message){
''' + case + r'''
  }return -1;
}
int main(){
 RECT proposed{100,100,1380,1124};
 assert(dispatch(1,144,reinterpret_cast<std::intptr_t>(&proposed))==0);
 assert(notified==0&&pending.size()==1);pending.back()();pending.clear();
 assert(scale==1.5f&&w==1264&&h==985); // Never publish outer 1280x1024.
 queried=false;readable=false;
 assert(dispatch(1,96,reinterpret_cast<std::intptr_t>(&proposed))==0);
 assert(pending.empty()); // A destroyed/unreadable client must not invent an extent.
}
'''
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler,'A C++ compiler is required for the actual handler fixture')
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'dpi.cpp';path.write_text(fixture)
            executable=Path(directory)/'dpi-test.exe'
            built=subprocess.run([compiler,'-std=c++17',str(path),'-o',str(executable)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            result=subprocess.run([str(executable)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__=='__main__':unittest.main()
