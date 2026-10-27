"""Compare native visibility admission with the reference avatar policy.

Exercise the actual production decision before skeleton transition handling;
this is policy evidence, not connected skeleton/mesh restoration evidence.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class AvatarVisibilityTest(unittest.TestCase):
    def test_reference_priority_and_both_animesh_exclusions(self):
        source = (ROOT/'indra/newview/vsworldavatar.cpp').read_text()
        production = method(source, 'VSWorldAvatar::Visibility VSWorldAvatar::updateVisibility(')
        production = production[:production.index('    if (state != avatar.visibility')]+'    return state;\n}'
        # The appearance policy itself lives on the private owner.
        policy = '\n'.join(method(source, signature) for signature in (
            'bool inMuteList() const', 'bool silhouette() const', 'bool buddy() const',
            'LLVOAvatar::VisualMuteSettings visualMute() const', 'bool complex() const',
            'Visibility overallAppearance() const'))
        fixture = r'''
#include <algorithm>
#include <cassert>
#include <map>
#include <limits>
#include <string>
using U64=unsigned long long;using U32=unsigned;using U8=unsigned char;using S32=int;using F32=float;using F64=double;
struct LLUUID{int value=0;bool notNull()const{return value!=0;}bool operator!=(LLUUID b)const{return value!=b.value;}};
const LLUUID IMG_DEFAULT_AVATAR{90},IMG_INVISIBLE{91};constexpr int BAKED_SKIRT=4;
namespace LLWearableType{constexpr int WT_SKIRT=4;}
struct Baked{int mTextureIndex=0;} baked;
struct Dictionary{std::map<int,Baked*> getBakedTextures(){return {{0,&baked},{BAKED_SKIRT,&baked}};}} dictionary;
struct LLAvatarAppearance{static Dictionary* getDictionary(){return &dictionary;}};
struct Texture{LLUUID id;LLUUID getID()const{return id;}};
struct Textures{Texture texture;Texture* getTE(U8){return &texture;}};
struct Saved{std::map<std::string,float> values;
 bool getBOOL(const char* k){return values[k]!=0;}int getS32(const char* k){return int(values[k]);}
 unsigned getU32(const char* k){return unsigned(values[k]);}float getF32(const char* k){return values[k];}}gSavedSettings;
// Cached controls observe the live setting, as LLCachedControl does after a change.
template<class T> struct LLCachedControl{Saved& saved;std::string name;
 LLCachedControl(Saved& s,const char* n,T=T()):saved(s),name(n){}
 operator T()const{return T(saved.values[name]);}T operator()()const{return *this;}};
struct LLFrameTimer{static inline F64 now=0;static F64 getTotalSeconds(){return now+=2;}};
struct LLVector3d{double x=0;};double dist_vec_squared(LLVector3d a,LLVector3d b){return (a.x-b.x)*(a.x-b.x);}
struct Agent{LLVector3d getPositionGlobal()const{return {};}}gAgent;
constexpr int RLV_BHVR_SETCAM_AVDIST=1,RLV_MODIFIER_SETCAM_AVDIST=2;
struct RlvActions{static inline bool active=false;static bool hasBehaviour(int){return active;}};
template<class T> struct RlvCachedBehaviourModifier{explicit RlvCachedBehaviourModifier(int){}T operator()()const{return 10;}};
struct LLAvatarTracker{bool buddy=false;static LLAvatarTracker& instance(){static LLAvatarTracker value;return value;}
 bool isBuddy(LLUUID){return buddy;}};
struct LLVOAvatar{enum{AV_RENDER_LIMIT_BY_COMPLEXITY=0,AV_RENDER_ONLY_SHOW_FRIENDS=2};
 enum VisualMuteSettings{AV_RENDER_NORMALLY=0,AV_ALWAYS_RENDER=1,AV_DO_NOT_RENDER=2};};
struct FSAvatarRenderPersistence{int setting=0;static FSAvatarRenderPersistence& instance(){static FSAvatarRenderPersistence value;return value;}
 LLVOAvatar::VisualMuteSettings getAvatarRenderSettings(LLUUID){return LLVOAvatar::VisualMuteSettings(setting);}};
struct LLMuteList{bool muted=false;static bool instanceExists(){return true;}
 static LLMuteList& instance(){static LLMuteList value;return value;}bool isMuted(LLUUID){return muted;}};
struct VSWorldAvatar{enum class Visibility{Normal,Jellydoll,Invisible};
 struct Impl{bool control=false,self=false,preview=false,animatedAttachments=false,tooSlowWithoutShadows=false;
  LLUUID id;Textures textures;U32 complexity=0;float attachmentArea=0;Visibility visibility=Visibility::Normal;
  mutable bool cachedMute=false,cachedSilhouette=false;mutable F64 muteUpdate=0,silhouetteUpdate=0;
  double distance=0;LLVector3d positionGlobal()const{return {distance};}
  int performanceUpdates=0;void updatePerformance(){++performanceUpdates;} // GPU-time tuning is outside this policy.
  bool isWearingWearableType(int){return false;}
POLICY
 };
 Impl value,*mImpl=&value;
 Visibility updateVisibility(U32,F32,bool);};
PRODUCTION
VSWorldAvatar::Visibility reference(bool self,bool control,bool buddy,bool muted,bool friendsOnly,int setting,
 int mode,unsigned maximum,float maxArea,unsigned cost,float area,unsigned exclude,bool animated){
 using V=VSWorldAvatar::Visibility;
 // LLVOAvatar::isVisible gates friends-only before getOverallAppearance.
 if(!self&&!control&&friendsOnly&&!buddy)return V::Invisible;
 if(self)return V::Normal;
 if(muted)return V::Invisible;
 if(setting==LLVOAvatar::AV_ALWAYS_RENDER)return V::Normal;
 if(setting==LLVOAvatar::AV_DO_NOT_RENDER)return V::Jellydoll;
 // LLVOAvatar::isTooComplex: friends/always, friends-only mode, exclusions,
 // then the coupled cost/area limit (zero cost limit disables area limit).
 if(buddy&&mode>LLVOAvatar::AV_RENDER_LIMIT_BY_COMPLEXITY)return V::Normal;
 if(mode==LLVOAvatar::AV_RENDER_ONLY_SHOW_FRIENDS&&!control)return V::Jellydoll;
 if((control&&exclude>=2)||(animated&&(exclude&1)))return V::Normal;
 return maximum&&(cost>maximum||(maxArea>0&&area>maxArea))?V::Jellydoll:V::Normal;
}
int main(){
 for(bool self:{false,true})for(bool control:{false,true})for(bool buddy:{false,true})
 for(bool muted:{false,true})for(bool friendsOnly:{false,true})for(int setting:{0,1,2})
 for(int mode:{0,1,2})for(unsigned maximum:{0u,100u,1000u})for(unsigned exclude:{0u,1u,2u,3u})
 for(bool animated:{false,true})for(float area:{0.f,99.f,100.f,101.f}){
  VSWorldAvatar avatar;avatar.value.self=self;avatar.value.control=control;
  LLAvatarTracker::instance().buddy=buddy;LLMuteList::instance().muted=muted;
  FSAvatarRenderPersistence::instance().setting=setting;
  gSavedSettings.values={{"RenderAvatarFriendsOnly",float(friendsOnly)},{"RenderAvatarComplexityMode",float(mode)},
   {"RenderAvatarMaxComplexity",float(maximum)},{"RenderAutoMuteSurfaceAreaLimit",100.f},{"FSImpostorAvatarExclude",float(exclude)}};
  assert(avatar.updateVisibility(100,area,animated)==reference(self,control,buddy,muted,friendsOnly,setting,mode,
   maximum,100,100,area,exclude,animated));
 }
 // Every defined visible baked texture adds the reference's texture charge;
 // an unworn skirt, default and invisible textures do not.
 VSWorldAvatar avatar;avatar.value.self=true;avatar.value.textures.texture.id={1};
 avatar.updateVisibility(100,0,false);assert(avatar.value.complexity==300);
 avatar.value.textures.texture.id=IMG_INVISIBLE;avatar.updateVisibility(100,0,false);assert(avatar.value.complexity==100);
 avatar.value.textures.texture.id={1};avatar.updateVisibility(std::numeric_limits<U32>::max()-100,0,false);
 assert(avatar.value.complexity==std::numeric_limits<U32>::max());
 // Preview owners are never policy-hidden; ordinary owners refresh performance first.
 VSWorldAvatar preview;preview.value.preview=true;LLMuteList::instance().muted=true;
 assert(preview.updateVisibility(0,0,false)==VSWorldAvatar::Visibility::Normal&&preview.value.performanceUpdates==0);
 LLMuteList::instance().muted=false;FSAvatarRenderPersistence::instance().setting=0;gSavedSettings.values.clear();
 // RLVa @setcam_avdist silhouettes distant avatars before other complexity rules.
 VSWorldAvatar distant;distant.value.distance=100;LLAvatarTracker::instance().buddy=false;
 assert(distant.updateVisibility(0,0,false)==VSWorldAvatar::Visibility::Normal&&distant.value.performanceUpdates==1);
 RlvActions::active=true;assert(distant.updateVisibility(0,0,false)==VSWorldAvatar::Visibility::Jellydoll);
 distant.value.distance=5;assert(distant.updateVisibility(0,0,false)==VSWorldAvatar::Visibility::Normal);
 distant.value.tooSlowWithoutShadows=true;assert(distant.updateVisibility(0,0,false)==VSWorldAvatar::Visibility::Jellydoll);
 RlvActions::active=false;
}
'''.replace('PRODUCTION', production).replace('POLICY', policy)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary)/'avatar_visibility.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout+ran.stderr)


if __name__ == '__main__':
    unittest.main()
