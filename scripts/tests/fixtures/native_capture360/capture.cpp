#include <cassert>
#include <cmath>
#include <memory>
#include <map>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <sstream>
using S32=int;using U32=unsigned;using F32=float;constexpr float F_PI_BY_TWO=1.57079632679f,RAD_TO_DEG=57.295779513f;
#define LL_WARNS(x) std::ostringstream()
#define LL_ENDL ""
struct LLVector3 {float x=0,y=0,z=0;LLVector3()=default;LLVector3(float a,float b,float c):x(a),y(b),z(c){}bool operator==(const LLVector3&)const=default;};
struct LLViewerCamera {LLVector3 origin{9,8,7},at{0,1,0},left{-1,0,0},up{0,0,1};float aspect=1.7f,view=.7f;int height=800;static LLViewerCamera* getInstance(){static LLViewerCamera v;return &v;}auto getOrigin(){return origin;}auto getAtAxis(){return at;}auto getLeftAxis(){return left;}auto getUpAxis(){return up;}auto getAspect(){return aspect;}auto getView(){return view;}auto getViewHeightInPixels(){return height;}float getYaw(){return .4f;}void setOrigin(LLVector3 v){origin=v;}void setAxes(LLVector3 a,LLVector3 l,LLVector3 u){at=a;left=l;up=u;}void setAspect(float v){aspect=v;}void setView(float v){view=v;}void setViewHeightInPixels(int v){height=v;}void updateNativeFrustum(){}void lookDir(LLVector3 a,LLVector3 u){at=a;up=u;left={u.y*a.z-u.z*a.y,u.z*a.x-u.x*a.z,u.x*a.y-u.y*a.x};}};
struct LLPipeline {enum {RENDER_TYPE_AVATAR=1,RENDER_TYPE_PARTICLES=2};static inline int mask=3,sUseOcclusion=2;static inline bool sRenderAttachedLights=true;static bool hasRenderTypeControl(int k){return(mask&k)!=0;}static void toggleRenderTypeControl(int k){mask^=k;}};
struct Settings {std::map<std::string,int> values;bool getBOOL(std::string k){return values[k];}unsigned getU32(std::string k){return values[k];}void setBOOL(std::string k,bool v){values[k]=v;}}gSavedSettings;
struct LLEnvironment {bool paused=false;static auto& instance(){static LLEnvironment e;return e;}bool isCloudScrollPaused(){return paused;}void pauseCloudScroll(){paused=true;}void resumeCloudScroll(){paused=false;}};
struct LLCharacter {static inline std::vector<LLCharacter*> sInstances;std::shared_ptr<int> requestPause(){return std::make_shared<int>(1);}};
struct LLViewerPartSim {static inline bool enabled=true;static auto* getInstance(){static LLViewerPartSim s;return &s;}static int getMaxPartCount(){return enabled?100:0;}void enable(bool v){enabled=v;}};
struct LLAppViewer {int pauses=0,resumes=0;static auto* instance(){static LLAppViewer a;return &a;}void pauseMainloopTimeout(){++pauses;}void resumeMainloopTimeout(){++resumes;}void pingMainloopTimeout(const char*){}};
struct LLViewerStats {struct Recording {int stops=0,resumes=0;void stop(){++stops;}void resume(){++resumes;}}recording;static auto& instance(){static LLViewerStats s;return s;}auto& getRecording(){return recording;}};
struct LLImageRaw {LLImageRaw(int,int,int){}};
template<class T>struct LLPointer {std::shared_ptr<T> p;LLPointer& operator=(T* v){p.reset(v);return *this;}operator T*()const{return p.get();}};
struct Window {int captures=0,fail=0;std::vector<std::pair<LLVector3,LLVector3>> frames;bool simpleSnapshot(LLImageRaw*,int width,int height,int){++captures;assert(width==64&&height==64);assert(LLViewerCamera::getInstance()->aspect==1&&LLViewerCamera::getInstance()->view==F_PI_BY_TWO);assert(gSavedSettings.getBOOL("FreezeTime")&&!LLViewerPartSim::enabled);frames.push_back({LLViewerCamera::getInstance()->at,LLViewerCamera::getInstance()->up});return captures!=fail;}}window;auto* gViewerWindow=&window;
struct LLSD {std::string& operator[](const char*){return value;}std::string value;};struct LLNotificationsUtil {static inline unsigned errors=0;static void add(const char*,const LLSD&){++errors;}};
struct Control {bool enabled=true;void setEnabled(bool v){enabled=v;}};struct Browser {std::string uri;void navigateTo(std::string v){uri=v;}};
struct LLFloater360Capture {std::function<bool()> mCurrent=[]{return true;};bool mCapturing=false,mWorldFrozen=false,mOldFreezeTime=false,mOldParticles=false,mOldCloudsPaused=false;int mSourceImageSize=64;float mInitialHeadingDeg=0;Control capture,save;Control *mCaptureBtn=&capture,*mSaveLocalBtn=&save;Browser browser;Browser* mWebBrowser=&browser;std::string mEqrGenHTML="eqr.html";std::vector<std::shared_ptr<int>> mAvatarPauseHandles;LLPointer<LLImageRaw> mRawImages[6];std::vector<std::string> encoded;bool header=true,footer=true;void setSourceImageSize(){}std::string makeFullPathToJS(std::string v){return v;}bool writeDataURLHeader(std::string){return header;}bool writeDataURLFooter(std::string){return footer;}bool encodeAndSave(LLPointer<LLImageRaw>,std::string,std::string prefix){encoded.push_back(prefix);return true;}std::string getHTMLBaseFolder(){return "base/";}void capture360Images();void freezeWorld(bool);};
CAPTURE
FREEZE
int main(){
 LLCharacter character;LLCharacter::sInstances={&character};gSavedSettings.values={{"360CaptureHideAvatars",1},{"FreezeTime",1},{"360CaptureNumRenderPasses",2}};LLEnvironment::instance().paused=true;
 const auto old=*LLViewerCamera::getInstance();LLFloater360Capture floater;floater.capture360Images();assert(window.captures==6&&floater.encoded==std::vector<std::string>({"posx","posz","posy","negx","negz","negy"}));
 // Frozen original LLFloater360Capture directions/up axes, unchanged cube-to-EQR convention.
 const LLVector3 directions[6]={{1,0,0},{0,1,0},{0,0,1},{-1,0,0},{0,-1,0},{0,0,-1}},ups[6]={{0,0,1},{0,0,1},{0,-1,0},{0,0,1},{0,0,1},{0,1,0}};
 for(int i=0;i<6;++i)assert(window.frames[i].first==directions[i]&&window.frames[i].second==ups[i]);
 auto restored=[&]{auto* c=LLViewerCamera::getInstance();assert(c->origin==old.origin&&c->at==old.at&&c->left==old.left&&c->up==old.up&&c->aspect==old.aspect&&c->view==old.view&&c->height==old.height);assert(LLPipeline::mask==3&&LLPipeline::sUseOcclusion==2&&LLPipeline::sRenderAttachedLights);assert(gSavedSettings.getBOOL("FreezeTime")&&LLEnvironment::instance().paused&&LLViewerPartSim::enabled);assert(LLAppViewer::instance()->pauses==LLAppViewer::instance()->resumes);assert(LLViewerStats::instance().recording.stops==LLViewerStats::instance().recording.resumes);};restored();assert(floater.save.enabled&&!floater.browser.uri.empty()&&floater.mAvatarPauseHandles.empty());
 window={};window.fail=3;LLFloater360Capture partial;partial.capture360Images();restored();assert(partial.encoded.size()==2&&!partial.save.enabled&&partial.browser.uri.empty()&&LLNotificationsUtil::errors==1);
 window={};LLFloater360Capture retired;retired.mCurrent=[]{return false;};retired.capture360Images();assert(window.captures==0);
 window={};LLPipeline::mask=0;LLViewerPartSim::enabled=false;gSavedSettings.setBOOL("FreezeTime",false);LLEnvironment::instance().paused=false;LLFloater360Capture hidden;hidden.capture360Images();assert(LLPipeline::mask==0&&!LLViewerPartSim::enabled&&!gSavedSettings.getBOOL("FreezeTime")&&!LLEnvironment::instance().paused);
}
