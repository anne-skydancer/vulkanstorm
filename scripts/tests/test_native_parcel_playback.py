"""Compile actual native parcel transport/controller bodies without audio or GUI."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'indra/newview/vsnativesession.cpp'


def balanced(source, start):
    begin = source.index('{', start)
    depth = 1
    index = begin + 1
    while depth:
        depth += (source[index] == '{') - (source[index] == '}')
        index += 1
    return source[start:index]


def method(source, declaration):
    return balanced(source, source.index(declaration))


class NativeParcelPlaybackTests(unittest.TestCase):
    def test_actual_controller_and_authenticated_metadata(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required for native controller qualification')
        source = SOURCE.read_text(encoding='utf-8')
        definitions = '\n'.join(method(source, name) for name in (
            'bool VSNativeSession::admit(', 'void VSNativeSession::requestParcel()',
            'void VSNativeSession::clearParcelPlayback()', 'void VSNativeSession::updateParcelPlayback()', 'bool VSNativeSession::mediaEnabled()',
            'bool VSNativeSession::playbackCurrent(', 'bool VSNativeSession::setMusicEnabled(',
            'bool VSNativeSession::setMediaEnabled(', 'void VSNativeSession::rejectParcelPlayback(',
            'void VSNativeSession::setMediaPaused(', 'bool VSNativeSession::updateParcelMediaMetadata(',
            'void VSNativeSession::applyParcelMediaState(', 'void VSNativeSession::parcelMediaCommand('))
        deliver = source[source.index('bool VSNativeSession::deliver('):]
        parcel = balanced(deliver, deliver.index('if (name == "ParcelProperties"'))
        parcel += '\n' + balanced(deliver, deliver.index('else if (name == "ParcelMediaUpdate"'))
        # Keep the actual account/admission guards; neighbour handshake dispatch
        # is qualified by the scene/session replay, outside this media component.
        prelude = deliver[deliver.index('    if (generation !='):deliver.index('    if (name=="EnableSimulator")')]
        definitions += '\nbool VSNativeSession::deliver(const std::string& name,const LLSD& body,const LLHost& sender,U64 generation){\n' + prelude + parcel + '\nreturn true;}'
        declaration = re.search(r'const\s+std::set<std::string>\s+worldMessages\s*=\s*\{.*?\};', source, re.S)
        self.assertIsNotNone(declaration, 'The real world-message admission set must be present')
        world_messages = declaration.group(0)
        fixture = STUBS + world_messages + '\n' + definitions + CHECKS
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'native_parcel_playback.cpp'
            executable = Path(directory) / 'native_parcel_playback.exe'
            cpp.write_text(fixture, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_filter_choices_follow_actual_xui_indices(self):
        source = SOURCE.read_text(encoding='utf-8')
        catalog = ET.parse(ROOT / 'indra/newview/skins/default/xui/en/notifications.xml').getroot()
        for name in ('AudioAlertSingle', 'MediaAlertSingle'):
            notification = next(n for n in catalog.findall('notification') if n.get('name') == name)
            choices = {b.get('name'): int(b.get('index')) for b in notification.findall('./form/button')}
            self.assertEqual(choices['BlacklistDomain'], 2)
            self.assertEqual(choices['WhitelistDomain'], 3)
        self.assertIn('resolve(option == 2 ? 4 : option)', source)
        for name in ('AudioAlert2', 'MediaAlert2'):
            notification = next(n for n in catalog.findall('notification') if n.get('name') == name)
            choices = {b.get('name'): int(b.get('index')) for b in notification.findall('./form/button')}
            self.assertEqual(choices['RememberDomain'], 1)
            self.assertEqual(choices['RememberURL'], 2)
        self.assertIn('option == 1 || option == 2 ? (allow ? 3 : 4)', source)
        self.assertIn('row["domain"] = rememberURL ? url : domain', source)

    def test_real_message_schema_and_mime_routing(self):
        source = SOURCE.read_text(encoding='utf-8')
        template = (ROOT / 'scripts/messages/message_template.msg').read_text(encoding='utf-8')
        for name in ('ParcelPropertiesRequest', 'ParcelProperties', 'ParcelMediaUpdate', 'ParcelMediaCommandMessage'):
            self.assertRegex(template, r'\b' + name + r'\s+(?:High|Medium|Low)\b')
        self.assertIn('msg->getString("MediaData", "MediaType", type)', source)
        parcel = (ROOT / 'indra/llinventory/llparcel.cpp').read_text(encoding='utf-8')
        self.assertIn('msg->getString("MediaData", "MediaType", buffer)', parcel)
        self.assertIn('params.mime_type = mMediaType', source)
        browser = (ROOT / 'indra/newview/llfloaterwebcontent.cpp').read_text(encoding='utf-8')
        self.assertIn('mWebBrowser->navigateTo(p.url, p.mime_type)', browser)
        self.assertIn('mWebBrowser->setHomePageUrl(p.url, p.mime_type)', browser)
        filters = method(source, 'bool VSNativeSession::requestParcelPlayback(')
        self.assertIn('owner->playbackCurrent(', filters)
        for prompt in ('AudioAlertSingle', 'MediaAlertSingle', 'AudioAlert2', 'MediaAlert2', 'EnableMediaFilter', 'ParcelCanPlayMedia'):
            self.assertIn('"' + prompt + '"', filters)
        reset = method(source, 'void VSNativeSession::reset()')
        self.assertLess(reset.index('clearParcelPlayback();'), reset.index('++mGeneration;'))
        self.assertIn('LLViewerParcelMedia::deleteSingleton()', reset)
        self.assertIn('clearParcelPlayback();', method(source, 'void VSNativeSession::requestLogout('))
        change = method(source, 'bool VSNativeSession::changeRegion(')
        self.assertLess(change.index('clearParcelPlayback();'), change.index('++mRegionEpoch;'))

    def test_actual_nearby_panel_stays_with_current_cpu_parcel(self):
        panel = (ROOT / 'indra/newview/llpanelnearbymedia.cpp').read_text()
        for declaration in ('void LLPanelNearByMedia::refreshList()', 'void LLPanelNearByMedia::updateControls()',
                            'void LLPanelNearByMedia::onClickSelectedMediaPlay()', 'void LLPanelNearByMedia::onClickSelectedMediaPause()',
                            'void LLPanelNearByMedia::onClickSelectedMediaMute()', 'void LLPanelNearByMedia::onCommitSelectedMediaVolume()',
                            'bool LLPanelNearByMedia::setDisabled(', 'std::string LLPanelNearByMedia::getSelectedUrl()'):
            native = method(panel, declaration).split('#if VS_NATIVE_VULKAN', 1)[1].split('#endif', 1)[0]
            self.assertIn('VSNativeSession::active()', native)
            self.assertIn('return', native)
            for forbidden in ('LLViewerParcelMgr', 'LLViewerMediaFocus', 'getPriorityList', 'getProximity', 'getSomeObject'):
                self.assertNotIn(forbidden, native)
        refresh = method(panel, 'void LLPanelNearByMedia::refreshList()').split('#endif', 1)[0]
        self.assertIn('PARCEL_MEDIA_LIST_ITEM_UUID', refresh)
        self.assertIn('owner->mediaURL()', refresh)
        self.assertIn('owner->parcelName()', refresh)
        pause = method((ROOT / 'indra/newview/llviewermedia.cpp').read_text(), 'void LLViewerMedia::setAllMediaPaused(')
        self.assertIn('owner->setMediaPaused(val)', pause)
        self.assertNotIn('owner->setMediaEnabled(!val)', pause)


STUBS = r"""
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>
using U64=uint64_t;using U32=uint32_t;using S32=int32_t;using F32=float;
constexpr int VX=0,VY=1,PARCEL_RESULT_SUCCESS=0;
#define LL_WARNS(...) std::cerr
#define LL_ENDL '\n'
struct LLSD {
 enum Kind {Undefined,Integer,String,Map};Kind kind=Undefined;S32 number=0;std::string text;std::map<std::string,LLSD> values;
 LLSD()=default;LLSD(S32 x):kind(Integer),number(x){}LLSD(const char* x):kind(String),text(x){}LLSD(const std::string& x):kind(String),text(x){}
 LLSD& operator[](const char* key){kind=Map;return values[key];}
 const LLSD& operator[](const char* key)const{static const LLSD empty;auto i=values.find(key);return i==values.end()?empty:i->second;}
 int asUUID()const{return number;}S32 asInteger()const{return number;}std::string asString()const{return text;}
 bool isBoolean()const{return kind==Integer;}bool asBoolean()const{return number!=0;}
 bool isInteger()const{return kind==Integer;}bool isString()const{return kind==String;}bool isMap()const{return kind==Map;}
 bool has(const char* key)const{return values.count(key);}
};
struct LLHost {int id=1;bool isOk()const{return id!=0;}bool operator==(const LLHost& rhs)const{return id==rhs.id;}
 bool operator<(const LLHost& rhs)const{return id<rhs.id;}};
const std::set<std::string> transport={"PacketAck"};
const std::set<std::string> session={"ParcelProperties","ParcelMediaUpdate","ParcelMediaCommandMessage"};
namespace LLStringUtil {void trim(std::string& value){auto a=value.find_first_not_of(' ');auto b=value.find_last_not_of(' ');value=a==std::string::npos?"":value.substr(a,b-a+1);}}
double now(){return 100.;}
bool webURL(const std::string& value){return value.rfind("http://",0)==0||value.rfind("https://",0)==0;}
struct Settings {std::map<std::string,int> values;bool getBOOL(const char* key)const{auto i=values.find(key);return i!=values.end()&&i->second;}int getS32(const char* key)const{auto i=values.find(key);return i==values.end()?0:i->second;}}gSavedSettings;
struct Vector {F32 mV[3]={128,64,20};bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1])&&std::isfinite(mV[2]);}};
template<class T>T llclamp(T v,T low,T high){return std::clamp(v,low,high);}template<class T>T llmin(T a,T b){return std::min(a,b);}
struct LLViewerAudio {static inline int stops=0;static bool instanceExists(){return true;}static LLViewerAudio& instance(){static LLViewerAudio a;return a;}void stopInternetStreamWithAutoFade(){++stops;}};
struct LLPluginClassMediaOwner {static constexpr int MEDIA_LOADING=1;};
struct Plugin {bool running=false,timeBased=true,loop=false;int status=1,starts=0,pauses=0,seeks=0;float lastSeek=0;
 bool isPluginRunning(){return running;}bool pluginSupportsMediaTime(){return timeBased;}int getStatus(){return status;}
 void setLoop(bool v){loop=v;}void start(){++starts;}void pause(){++pauses;}void seek(float v){++seeks;lastSeek=v;}
};
struct LLMediaCtrl {Plugin* plugin=nullptr;bool decoupled=false;int width=0,height=0;
 Plugin* getMediaPlugin(){return plugin;}void setDecoupleTextureSize(bool v){decoupled=v;}void setTextureSize(int w,int h){width=w;height=h;}
};
struct Window {int closes=0;LLMediaCtrl browser;void closeFloater(){++closes;}template<class T>T* findChild(const char*,bool){return &browser;}};
constexpr int PARCEL_MEDIA_COMMAND_STOP=0,PARCEL_MEDIA_COMMAND_UNLOAD=1,PARCEL_MEDIA_COMMAND_PAUSE=2,PARCEL_MEDIA_COMMAND_TIME=3,PARCEL_MEDIA_COMMAND_PLAY=4,PARCEL_MEDIA_COMMAND_LOOP=5;
struct Handle {Window* pointer=nullptr;Window* get()const{return pointer;}bool isDead()const{return !pointer;}void markDead(){pointer=nullptr;}};
struct LLMessageSystem {std::string name,block;std::map<std::string,F32> floats;std::map<std::string,S32> ints;int agent=0,session=0,sends=0;LLHost destination;
 void newMessage(const char* v){name=v;}void nextBlock(const char* v){block=v;}
 void addUUID(const char* key,int v){if(std::string(key)=="AgentID")agent=v;else session=v;}
 void addS32(const char* key,S32 v){ints[key]=v;}void addF32(const char* key,F32 v){floats[key]=v;}void addBOOL(const char* key,bool v){ints[key]=v;}
 int sendReliable(LLHost v){destination=v;++sends;return 1;}
} message,*gMessageSystem=&message;
void* gAudiop=reinterpret_cast<void*>(1);
struct VSNativeSession {
 enum class Phase{Login,Connecting,Connected,Logout,Disconnected};Phase mPhase=Phase::Connected;
 LLHost mHost;bool mChangingRegion=false;Vector mPosition;U32 mWidth=256,mHeight=256;
 struct Circuit {bool acknowledged=false,handshake=false;};std::map<LLHost,Circuit> mCircuits;
 U64 mGeneration=7,mRegionEpoch=3,mParcelRevision=1,mMusicRequest=1,mMediaRequest=1,mSent=0,mRejected=0,mExpired=0;
 struct UUID {int id=0;UUID& operator=(int value){id=value;return *this;}void setNull(){id=0;}}mParcelOwner;
 U32 mParcelFlags=0;bool mParcelPermissionsKnown=false,mParcelSeeAvatars=true;
 S32 mParcelCellX=-1,mParcelCellY=-1;double mParcelQueryTime=0;
 S32 mParcelSequence=11,mParcelID=-1;int mAgent=23,mSession=24;
 bool mMusicRequested=false,mMediaRequested=false,mMusicUserStopped=false,mMediaUserStopped=false,mMediaPresented=false;
 bool mMediaLoop=false,mMediaAutoScale=true,mMediaPaused=false,mMediaStateDirty=false;S32 mMediaWidth=0,mMediaHeight=0;F32 mMediaPendingSeek=-1.f;
 std::string mMusicURL,mMediaURL,mParcelName,mMediaType;Handle mParcelMediaWindow;
 int musicRequests=0,mediaRequests=0;
 bool admit(const std::string&,const LLHost&);void requestParcel();void clearParcelPlayback();void updateParcelPlayback();bool mediaEnabled()const;
 bool playbackCurrent(bool,U64,U64,U64,U64,const std::string&)const;
 bool setMusicEnabled(bool);bool setMediaEnabled(bool);void rejectParcelPlayback(bool);
 void setMediaPaused(bool);bool updateParcelMediaMetadata(const LLSD&);void applyParcelMediaState();void parcelMediaCommand(U32,U32,F32);
 bool requestParcelPlayback(bool music){if(music)++musicRequests;else++mediaRequests;return mPhase==Phase::Connected&&!mChangingRegion;}
 bool deliver(const std::string&,const LLSD&,const LLHost&,U64);
};
"""

CHECKS = r"""
LLSD parcel(){LLSD p;p["parcel_flags"]=123;p["see_avatars"]=0;p["result"]=0;p["sequence"]=11;p["id"]=31;p["name"]="CPU parcel";p["music"]="http://127.0.0.1/music";p["media"]="http://127.0.0.1/video";p["type"]="video/mp4";return p;}
int main(){
 VSNativeSession owner;auto p=parcel();
 assert(!owner.deliver("ParcelProperties",p,LLHost{2},7));assert(owner.mMusicURL.empty());
 assert(!owner.deliver("ParcelProperties",p,LLHost{1},6));assert(owner.mExpired==1);
 auto bad=p;bad["sequence"]=-50000;assert(!owner.deliver("ParcelProperties",bad,LLHost{1},7));
 bad=p;bad["sequence"]=10;assert(!owner.deliver("ParcelProperties",bad,LLHost{1},7));
 bad=p;bad["result"]=-1;assert(!owner.deliver("ParcelProperties",bad,LLHost{1},7));
 bad=p;bad["music"]="file:///private";assert(!owner.deliver("ParcelProperties",bad,LLHost{1},7));
 bad=p;bad["id"]="31";assert(!owner.deliver("ParcelProperties",bad,LLHost{1},7));
 assert(owner.deliver("ParcelProperties",p,LLHost{1},7));assert(owner.mMusicURL==p["music"].asString());assert(owner.mMediaType=="video/mp4");
 assert(owner.mParcelPermissionsKnown&&owner.mParcelFlags==123&&!owner.mParcelSeeAvatars);
 // Metadata does not itself bypass explicit enable settings or fetch a URL.
 assert(owner.musicRequests==0&&owner.mediaRequests==0);
 assert(!owner.setMusicEnabled(true));assert(!owner.mMusicRequested);
 gSavedSettings.values["AudioStreamingMusic"]=1;assert(owner.setMusicEnabled(true));assert(owner.musicRequests==1);
 owner.rejectParcelPlayback(true);assert(!owner.mMusicRequested&&!owner.mMusicUserStopped);
 assert(owner.setMusicEnabled(true));
 auto revision=owner.mParcelRevision, request=owner.mMusicRequest;
 assert(owner.playbackCurrent(true,7,3,revision,request,owner.mMusicURL));
 assert(!owner.playbackCurrent(true,8,3,revision,request,owner.mMusicURL));
 assert(!owner.playbackCurrent(true,7,4,revision,request,owner.mMusicURL));
 assert(!owner.playbackCurrent(true,7,3,revision+1,request,owner.mMusicURL));
 assert(!owner.playbackCurrent(true,7,3,revision,request+1,owner.mMusicURL));
 assert(!owner.playbackCurrent(true,7,3,revision,request,"http://127.0.0.1/retired"));
 owner.setMusicEnabled(false);assert(owner.mMusicUserStopped);assert(!owner.playbackCurrent(true,7,3,revision,request,owner.mMusicURL));
 gSavedSettings.values["AudioStreamingMedia"]=1;assert(owner.setMediaEnabled(true));Window window;owner.mParcelMediaWindow.pointer=&window;
 assert(owner.mediaEnabled());
 gSavedSettings.values["AudioStreamingMedia"]=0;owner.updateParcelPlayback();assert(window.closes==1&&!owner.mediaEnabled());
 gSavedSettings.values["AudioStreamingMedia"]=1;assert(owner.setMediaEnabled(true));owner.mMediaPresented=false;
 owner.updateParcelPlayback();assert(owner.mMediaRequested); // consent can remain pending without a window
 owner.mMediaPresented=true;owner.updateParcelPlayback();assert(!owner.mMediaRequested&&owner.mMediaUserStopped);
 assert(owner.setMediaEnabled(true));owner.mParcelMediaWindow.pointer=&window;
 // A changed parcel retires both pending consent and the 2D media window.
 bad=p;bad["id"]=32;bad["music"]="http://127.0.0.1/next";
 assert(owner.deliver("ParcelProperties",bad,LLHost{1},7));assert(window.closes==2);assert(!owner.mediaEnabled());
 owner.clearParcelPlayback();assert(owner.mParcelID==-1&&owner.mMusicURL.empty()&&owner.mMediaURL.empty());
 assert(!owner.playbackCurrent(true,7,3,revision,request,p["music"].asString()));
 owner.requestParcel();assert(message.name=="ParcelPropertiesRequest");assert(message.agent==23&&message.session==24);
 assert(message.ints["SequenceID"]==12&&!message.ints["SnapSelection"]);assert(message.destination.id==1);
 assert(message.floats["West"]==128&&message.floats["East"]>128&&message.floats["South"]==64);
 // Metadata is bounded and commands remain pending until the actual plugin is ready.
 owner.mParcelMediaWindow.pointer=&window;owner.mMediaRequested=true;owner.mMediaUserStopped=false;
 Plugin plugin;window.browser.plugin=&plugin;LLSD metadata;metadata["loop"]=1;metadata["autoscale"]=0;metadata["width"]=640;metadata["height"]=360;
 assert(owner.updateParcelMediaMetadata(metadata));owner.applyParcelMediaState();assert(plugin.starts==0&&owner.mMediaStateDirty);
 auto invalid=metadata;invalid["width"]=4097;assert(!owner.updateParcelMediaMetadata(invalid));assert(owner.mMediaWidth==640);
 gSavedSettings.values["PermAllowScriptedMedia"]=1;
 owner.parcelMediaCommand(1u<<PARCEL_MEDIA_COMMAND_TIME,PARCEL_MEDIA_COMMAND_TIME,12.5f);assert(owner.mMediaPendingSeek==12.5f&&plugin.seeks==0);
 plugin.running=true;owner.applyParcelMediaState();assert(plugin.loop&&window.browser.decoupled&&window.browser.width==640&&plugin.starts==1);
 assert(plugin.seeks==0&&owner.mMediaPendingSeek==12.5f);plugin.status=2;owner.applyParcelMediaState();assert(plugin.seeks==1&&plugin.lastSeek==12.5f);
 owner.setMediaPaused(true);assert(owner.mMediaPaused&&!owner.mediaEnabled()&&plugin.pauses==1&&window.closes==2);
 owner.setMediaPaused(false);assert(owner.mediaEnabled()&&plugin.starts==2&&window.closes==2);
 auto starts=plugin.starts;assert(owner.updateParcelMediaMetadata(metadata));owner.applyParcelMediaState();assert(plugin.starts==starts);
 owner.parcelMediaCommand(1u<<PARCEL_MEDIA_COMMAND_TIME,PARCEL_MEDIA_COMMAND_TIME,std::numeric_limits<float>::infinity());assert(owner.mMediaPendingSeek<0);
 plugin.loop=false;owner.mMediaLoop=false;owner.parcelMediaCommand(1u<<PARCEL_MEDIA_COMMAND_LOOP,PARCEL_MEDIA_COMMAND_LOOP,0);assert(plugin.loop);
 owner.parcelMediaCommand(0,PARCEL_MEDIA_COMMAND_TIME,80);assert(plugin.lastSeek==12.5f);
 owner.setMediaEnabled(false);assert(owner.mMediaPendingSeek<0&&!owner.mMediaPaused);
 auto music=owner.musicRequests;owner.parcelMediaCommand(1u<<PARCEL_MEDIA_COMMAND_PLAY,PARCEL_MEDIA_COMMAND_PLAY,0);assert(!owner.mMediaRequested&&owner.musicRequests==music);
 owner.mMediaUserStopped=false;owner.mMediaRequested=true;owner.mParcelMediaWindow.pointer=&window;
 gSavedSettings.values["PermAllowScriptedMedia"]=0;owner.parcelMediaCommand(1u<<PARCEL_MEDIA_COMMAND_TIME,PARCEL_MEDIA_COMMAND_TIME,99);assert(plugin.lastSeek==12.5f);
 owner.clearParcelPlayback();assert(owner.mMediaPendingSeek<0&&!owner.mMediaLoop&&!owner.mMediaPaused&&owner.mMediaWidth==0);
 // Identical source updates must preserve pause without restart or repeated consent.
 p["sequence"]=owner.mParcelSequence;assert(owner.deliver("ParcelProperties",p,LLHost{1},7));
 assert(owner.setMediaEnabled(true));owner.mParcelMediaWindow.pointer=&window;owner.setMediaPaused(true);
 LLSD update;update["media"]=owner.mMediaURL;update["type"]=owner.mMediaType;
 starts=plugin.starts;auto requests=owner.mediaRequests;
 assert(owner.deliver("ParcelMediaUpdate",update,LLHost{1},7));assert(owner.mMediaPaused&&owner.mMediaRequested);
 assert(plugin.starts==starts&&owner.mediaRequests==requests);
 update["media"]="http://127.0.0.1/replaced";assert(owner.deliver("ParcelMediaUpdate",update,LLHost{1},7));
 assert(owner.mMediaRequested&&owner.mMediaPaused&&!owner.mMediaUserStopped);
 owner.mParcelMediaWindow.pointer=&window;p["id"]=99;
 assert(owner.deliver("ParcelProperties",p,LLHost{1},7));assert(owner.mMediaRequested&&owner.mMediaPaused&&!owner.mMediaUserStopped);
 owner.setMediaEnabled(false);update["media"]="http://127.0.0.1/explicit-stop";
 assert(owner.deliver("ParcelMediaUpdate",update,LLHost{1},7));assert(owner.mMediaUserStopped&&!owner.mMediaRequested);
 owner.mChangingRegion=true;int sends=message.sends;owner.requestParcel();assert(message.sends==sends);
 assert(!owner.playbackCurrent(true,7,3,owner.mParcelRevision,owner.mMusicRequest,owner.mMusicURL));
}
"""

if __name__ == '__main__':
    unittest.main()
