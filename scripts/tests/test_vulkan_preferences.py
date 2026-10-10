"""Compile production preference/widget paths with no OpenGL renderer available."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class NativePreferencesTest(unittest.TestCase):
    def compile_and_run(self, source):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'preferences.cpp'
            exe = Path(directory) / 'preferences.exe'
            cpp.write_text(source)
            build = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_translucent_swatch_draws_without_opengl(self):
        source = (ROOT / 'indra/newview/llcolorswatch.cpp').read_text()
        draw = source[source.index('void LLColorSwatchCtrl::draw()'):source.index('void LLColorSwatchCtrl::setEnabled(')]
        # No gGL is declared: any direct legacy render-state access is a failure.
        self.compile_and_run(r'''
#include <cassert>
using F32=float;
struct LLColor4 {float alpha=1;static LLColor4 white,grey,black;bool isOpaque(){return alpha==1;}};
LLColor4 LLColor4::white,LLColor4::grey,LLColor4::black;
LLColor4 operator%(LLColor4 c,float a){c.alpha*=a;return c;}
struct LLRect {int mLeft=0,mBottom=0;LLRect(int,int,int,int){};int getHeight(){return 30;}int getWidth(){return 20;}void stretch(int){}};
int checks=0,fills=0,images=0,children=0,depth=0;
void gl_rect_2d(LLRect,LLColor4,bool fill){if(fill)++fills;}
void gl_rect_2d_checkerboard(LLRect,float){++checks;}
void gl_draw_x(LLRect,LLColor4){}
struct LLUI {static void pushMatrix(){++depth;}static void popMatrix(){--depth;}};
struct Image {float alpha=0;void draw(LLRect,LLColor4 c){++images;alpha=c.alpha;}void draw(int,int,int,int,LLColor4){++images;}};
struct Pointer {Image* p=nullptr;bool notNull(){return p;}Image* operator->(){return p;}};
struct Border {void setKeyboardFocusHighlight(bool){}};
struct Caption {void setEnabled(bool){}};
struct Color {LLColor4 get(){return {};}};
struct LLUICtrl {void draw(){++children;}};
struct LLColorSwatchCtrl:LLUICtrl {
 enum{TT_ACTIVE};bool mValid=true,active=false;int mLabelHeight=5;
 LLColor4 mColor;Color mBorderColor;Pointer mAlphaGradientImage,mFallbackImage;
 Border border;Caption caption;Border* mBorder=&border;Caption* mCaption=&caption;
 int getTransparencyType(){return active?TT_ACTIVE:1;}float getCurrentTransparency(){return .5f;}
 bool hasFocus(){return true;}LLRect getRect(){return {0,30,20,0};}
 bool getEnabled(){return true;}bool isInEnabledChain(){return true;}void draw();
};
''' + draw + r'''
int main(){
 LLColorSwatchCtrl swatch;Image image;swatch.mColor.alpha=.4f;swatch.mAlphaGradientImage.p=&image;
 for(int i=0;i<100;++i){swatch.draw();assert(depth==0);}
 assert(checks==100&&fills==100&&images==100&&children==100);
 assert(image.alpha>.199f&&image.alpha<.201f);
 swatch.active=true;swatch.draw();assert(image.alpha==.4f);
 swatch.mColor.alpha=1;swatch.draw();assert(checks==101&&images==101);
 swatch.mValid=false;swatch.draw();swatch.mFallbackImage.p=&image;swatch.draw();
 assert(depth==0&&children==104&&images==102);
}
''')

    def test_shader_choices_only_use_legacy_capabilities_in_legacy_sessions(self):
        source = (ROOT / 'indra/newview/llfloaterpreference.cpp').read_text()
        post = source[source.index('bool LLPanelPreferenceGraphics::postBuild()'):]
        probe = post[post.index('    // Shader availability'):post.index('#if !LL_DARWIN')]
        self.compile_and_run(r'''
#include <cassert>
#include <map>
#include <string>
struct Shader {bool isComplete(){return false;}};
Shader gFXAAProgram[1],gSMAAEdgeDetectProgram[1];
struct Window{bool native=true;bool isNativeVulkan(){return native;}}window;
Window* gViewerWindow=&window;
struct LLComboBox{bool enabled=true;int removals=0;void remove(const char*){++removals;}void setEnabled(bool value){enabled=value;}};
struct Graphics {
 std::map<std::string,LLComboBox> controls;
 template<class T>T* getChild(const char* name){return &controls[name];}
 void probe(){
''' + probe + r'''
 }
};
int main(){Graphics native;native.probe();
 assert(native.controls["fsaa"].enabled&&native.controls["fsaa"].removals==0);
 window.native=false;Graphics legacy;legacy.probe();
 assert(!legacy.controls["fsaa"].enabled&&legacy.controls["fsaa"].removals==2);
 assert(!legacy.controls["fsaa quality"].enabled);
}
''')

    def test_native_refresh_preserves_saved_controls(self):
        source = (ROOT / 'indra/newview/llfloaterpreference.cpp').read_text()
        refresh = source[source.index('void LLFloaterPreference::refreshEnabledState()'):]
        refresh = refresh[:refresh.index('#endif') + len('#endif')] + '\n}\n'
        self.compile_and_run(r'''
#include <cassert>
#include <map>
#include <string>
#define VS_NATIVE_VULKAN 1
struct Control {bool enabled=true;int saved=7;void setEnabled(bool v){enabled=v;}};
struct LLButton:Control{};
struct VSNativeSession{static bool active(){return true;}};
enum {STATE_STARTED=1};struct LLStartUp{static int getStartupState(){return 0;}};
struct LLLoginInstance{static LLLoginInstance* getInstance(){static LLLoginInstance i;return &i;}bool authSuccess(){return false;}};
struct LLFloaterPreference {
 std::map<std::string,LLButton> controls;
 Control* getChildView(const char* n){return &controls[n];}
 template<class T>T* getChild(const char* n){return &controls[n];}
 void refreshEnabledState();
};
''' + refresh + r'''
int main(){LLFloaterPreference prefs;
 for(const char* n:{"DrawDistance","fsaa","fsaa quality","TransparentWater","camera_fov","QualityPerformanceSelection","RenderPostProcess","render_backend"})prefs.controls[n];
 for(int i=0;i<100;++i)prefs.refreshEnabledState();
 for(const char* n:{"DrawDistance","fsaa","fsaa quality","TransparentWater","camera_fov","QualityPerformanceSelection","RenderPostProcess","render_backend"}){
 assert(prefs.controls[n].enabled);assert(prefs.controls[n].saved==7);}
 assert(!prefs.controls["Defaults"].enabled&&!prefs.controls["Wireframe"].enabled);
}
''')

if __name__ == '__main__':
    unittest.main()
