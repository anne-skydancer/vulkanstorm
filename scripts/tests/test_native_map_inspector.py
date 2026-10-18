"""Actual shared Compass draw topology and account-owned rotation regression."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[2]
def body(source,name):
    start=source.index(name);end=source.index('{',start)+1;depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
class NativeMapInspectorTests(unittest.TestCase):
    def compile(self,code):
        compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory()as d:
            cpp=Path(d)/'compass.cpp';exe=Path(d)/'compass.exe';cpp.write_text(code)
            built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_actual_compass_full_and_compact_vertex_color_and_hover_parity(self):
        source=(ROOT/'indra/newview/fsfloateravataralign.cpp').read_text()
        adapter=body(source,'class VSCompassDraw')+';'
        line=body((ROOT/'indra/llrender/llrender2dutils.cpp').read_text(),'void LLRender2D::nativeLine(')
        fixture=r'''
#include <cassert>
#include <cmath>
#include <array>
#include <vector>
#include <string>
#include <cstdio>
using F32=float;using S32=int;using U32=unsigned;
constexpr bool TRUE=true,FALSE=false;constexpr int VX=0,VY=1,VZ=2;constexpr float F_PI=3.14159265358979323846f,F_PI_BY_TWO=F_PI/2,DEG_TO_RAD=F_PI/180,RAD_TO_DEG=180/F_PI;
int ll_round(float f){return int(std::floor(f+.5f));}
template<class...T>std::string llformat(const char*f,T...v){char b[100];std::snprintf(b,sizeof b,f,v...);return b;}
struct LLColor4{float mV[4];LLColor4(float r=0,float g=0,float b=0,float a=1):mV{r,g,b,a}{}static LLColor4 white;};LLColor4 LLColor4::white{1,1,1,1};
struct LLVector3{float mV[3]={1,0,0};float normalize(){float n=std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);if(n)for(auto&v:mV)v/=n;return n;}};
struct Agent{LLVector3 getAtAxis(){return {};}}gAgent;
struct LLRect{int mLeft=0,mTop=0,mRight=0,mBottom=0;void set(int l,int t,int r,int b){mLeft=l;mTop=t;mRight=r;mBottom=b;}};
using Vertex=std::array<float,6>;using Triangle=std::array<Vertex,3>;
std::vector<Triangle>expected,actual;std::vector<std::array<float,9>>circlesExpected,circlesActual;
float native_line_width=1;
struct LLRender{enum{TRIANGLES,LINES};};struct LLTexUnit{enum{TT_TEXTURE};};
struct LLRender2D{static bool native;static LLColor4 tint;static bool isNativeUI(){return native;}static void setNativeColor(LLColor4 c){tint=c;}static const LLColor4&nativeColor(){return tint;}static void nativeTriangle(Triangle t){actual.push_back(t);}static void nativeLine(const Vertex&,const Vertex&);};bool LLRender2D::native=false;LLColor4 LLRender2D::tint;
void referenceLine(Vertex a,Vertex b){float dx=b[0]-a[0],dy=b[1]-a[1],length=std::sqrt(dx*dx+dy*dy);if(!length)return;float nx=-dy/length*native_line_width/2,ny=dx/length*native_line_width/2;Vertex p=a,q=a,r=b,s=b;p[0]+=nx;p[1]+=ny;q[0]-=nx;q[1]-=ny;r[0]+=nx;r[1]+=ny;s[0]-=nx;s[1]-=ny;expected.push_back({p,r,q});expected.push_back({q,r,s});}
struct GL{int calls=0,mode=0;std::vector<Vertex> vertices;LLColor4 color;GL*getTexUnit(int){++calls;return this;}void unbind(int){++calls;}void color4f(float r,float g,float b,float a){++calls;color={r,g,b,a};}void begin(int m){++calls;mode=m;vertices.clear();}void vertex2f(float x,float y){++calls;vertices.push_back({x,y,color.mV[0],color.mV[1],color.mV[2],color.mV[3]});}void end(){++calls;if(mode==LLRender::TRIANGLES)for(size_t i=0;i<vertices.size();i+=3)expected.push_back({vertices[i],vertices[i+1],vertices[i+2]});else for(size_t i=0;i<vertices.size();i+=2)referenceLine(vertices[i],vertices[i+1]);}}gGL;
void gl_circle_2d(float x,float y,float r,int steps,bool filled){auto c=LLRender2D::native?LLRender2D::tint:gGL.color;(LLRender2D::native?circlesActual:circlesExpected).push_back({x,y,r,float(steps),float(filled),c.mV[0],c.mV[1],c.mV[2],c.mV[3]});}
void gl_rect_2d(LLRect,bool){}
struct LLFontGL{enum{HCENTER,BOTTOM,BOLD,DROP_SHADOW,TOP,NORMAL,NO_SHADOW,LEFT,VCENTER,RIGHT};static LLFontGL*getFontSansSerifSmall(){static LLFontGL f;return &f;}int getWidth(const std::string&){return 32;}template<class...T>void renderUTF8(T...) {}};
struct FSAvatarAlignBase{
struct CompassLayout{int R=50;float cx=70,cy=80;bool mini_compact=false,show_labels=true;int bearing_y=10,sq_half=70;std::string toggle_label="Mini";} layout;
int mCompassCX=0,mCompassCY=0,mCompassR=0,mHoverOctant=-1;bool mHoverToggle=false;LLRect mToggleBtnRect;
CompassLayout buildCompassLayout(){return layout;}void drawCompass();};
'''+line+'\n'+adapter+'\n'+body(source,'void FSAvatarAlignBase::drawCompass()')+r'''
int main(){for(bool compact:{false,true})for(int hover=-2;hover<8;++hover)for(float width:{1.f,2.f}){
 FSAvatarAlignBase control;control.layout.mini_compact=compact;control.layout.show_labels=!compact;control.mHoverOctant=hover;native_line_width=width;
 expected.clear();actual.clear();circlesExpected.clear();circlesActual.clear();LLRender2D::native=false;control.drawCompass();int oldCalls=gGL.calls;
 LLRender2D::native=true;control.drawCompass();assert(gGL.calls==oldCalls&&actual.size()==expected.size()&&actual.size()>20);assert(circlesActual==circlesExpected);
 for(size_t t=0;t<actual.size();++t)for(int v=0;v<3;++v)for(int c=0;c<6;++c)assert(std::abs(actual[t][v][c]-expected[t][v][c])<.00001f);
}}
'''
        self.compile(fixture)
    def test_actual_uuid_face_permission_range_epoch_and_native_rotation_consumer(self):
        source=(ROOT/'indra/newview/fsfloateravataralign.cpp').read_text()
        fixture=r'''
#include <cassert>
#include <cmath>
#include <functional>
#include <memory>
#define VS_NATIVE_VULKAN 1
using F32=float;using LLUUID=int;constexpr int VZ=2;LLUUID gAgentID=1;
struct LLVector3d{double x=0,y=0,z=0;};
LLVector3d operator-(LLVector3d a,LLVector3d b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
double dist_vec(LLVector3d a,LLVector3d b){auto p=a-b;return std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);}
struct LLVector3{float mV[3];LLVector3(LLVector3d p):mV{float(p.x),float(p.y),float(p.z)}{}float normalize(){float n=std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);if(n)for(auto&v:mV)v/=n;return n;}};
struct Agent{LLVector3d getPositionGlobal(){return {};}void resetAxes(LLVector3){assert(false);}}gAgent;
void send_agent_update(bool,bool){assert(false);}
struct VSNativeSession{bool ready=true;int rotations=0;LLVector3 last{{0,0,0}};static std::shared_ptr<VSNativeSession>active();bool regionReady(){return ready;}void faceBody(LLVector3 p){++rotations;last=p;}};
std::shared_ptr<VSNativeSession>owner=std::make_shared<VSNativeSession>();std::shared_ptr<VSNativeSession>VSNativeSession::active(){return owner;}
bool precise=true;LLVector3d target{3,4,8};bool vs_avatar_precise_location(LLUUID id,LLVector3d&p){p=target;return id==2&&precise;}
struct FSAvatarAlignBase{static constexpr float MAX_FACE_DISTANCE=20;bool valid=true;std::function<bool()>mAccountCurrent=[this]{return valid;};void*mTargetAvatar=nullptr;bool isAvatarInRange(const LLUUID&)const;void faceAvatar(const LLUUID&);void applyRotation(const LLVector3&);void snapAvatarBody(LLVector3){assert(false);}void snapRemoteAvatarBody(void*){assert(false);}};
'''
        # Only UUID.null checks are adapted by this tiny UUID stand-in.
        fixture=fixture.replace('using F32=float;using LLUUID=int;', 'using F32=float;struct LLUUID{int id;LLUUID(int v=0):id(v){}bool isNull()const{return !id;}bool operator==(const LLUUID&o)const{return id==o.id;}};')
        for method in ['bool FSAvatarAlignBase::isAvatarInRange(const LLUUID& id)', 'void FSAvatarAlignBase::faceAvatar(const LLUUID& id)', 'void FSAvatarAlignBase::applyRotation(const LLVector3& direction)']:
            fixture+=body(source,method)+'\n'
        fixture+=r'''
int main(){FSAvatarAlignBase panel;assert(panel.isAvatarInRange(2));panel.faceAvatar(2);assert(owner->rotations==1&&std::abs(owner->last.mV[0]-.6f)<.0001f&&std::abs(owner->last.mV[1]-.8f)<.0001f&&owner->last.mV[2]==0);
 panel.valid=false;panel.faceAvatar(2);assert(owner->rotations==1);panel.valid=true;
 precise=false;panel.faceAvatar(2);assert(owner->rotations==1);precise=true;
 target={30,0,0};assert(!panel.isAvatarInRange(2));panel.faceAvatar(2);assert(owner->rotations==1);
 target={0,0,2};panel.faceAvatar(2);assert(owner->rotations==1);
 target={2,0,0};owner->ready=false;panel.faceAvatar(2);assert(owner->rotations==1);
 owner->ready=true;assert(!panel.isAvatarInRange(1)&&!panel.isAvatarInRange(0));panel.faceAvatar(2);assert(owner->rotations==2);
}
'''
        self.compile(fixture)
if __name__=='__main__':unittest.main()
