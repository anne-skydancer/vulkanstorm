"""Accepted outfits survive incomplete/older AIS replies without weakening avatar guards."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method

class AcceptedOutfitRecovery(unittest.TestCase):
 def test_older_reply_and_incomplete_fetch_keep_accepted_outfit(self):
  s=(ROOT/'indra/newview/vsnativeappearance.cpp').read_text(encoding='utf-8')
  helper=method(s,'bool authoritativeOutfit(')
  guard=method((ROOT/'indra/newview/vsworldavatar.cpp').read_text(encoding='utf-8'),'bool VSWorldAvatar::wearables(')
  guard=guard[guard.index('    if (!avatar.self'):guard.index('    std::map<LLWearableType::EType')]
  code=r'''
  #include <cassert>
  #include <vector>
  using S32=int;
  struct LLUUID{int value=0;bool operator!=(const LLUUID& b)const{return value!=b.value;}};
  struct LLViewerInventoryCategory{enum{VERSION_UNKNOWN=-1,FETCH_NONE=0};int version=17,desc=8,fetching=2;
   int getVersion()const{return version;}void setVersion(int n){version=n;}void setDescendentCount(int n){desc=n;}void setFetching(int n){fetching=n;}};
  struct LLInventoryModelBackgroundFetch{int starts=0,forced=0;LLUUID last;
   static LLInventoryModelBackgroundFetch& instance(){static LLInventoryModelBackgroundFetch f;return f;}
   void start(LLUUID id,bool recursive){assert(recursive);++starts;last=id;}
   void scheduleFolderFetch(LLUUID id,bool force){assert(force);++forced;last=id;}};
  struct Owner{LLUUID acceptedCOF{100};int acceptedCOFVersion=17;METHOD};
  struct LLWearableData{enum{MAX_CLOTHING_LAYERS=60,MAX_BODY_PARTS=4};};
  struct Avatar{bool self=true;int pendingCOFVersion=17;};
  bool originalAdmission(Avatar& avatar,const std::vector<int>& wearables,int cofVersion){GUARD return true;}
  int main(){
   Owner state;LLViewerInventoryCategory category;auto& fetch=LLInventoryModelBackgroundFetch::instance();
   std::vector<int> accepted{1,2,3,4,5,7,10,11};Avatar avatar;
   assert(!originalAdmission(avatar,accepted,14)); // exact pre-existing avatar guard
   category.version=14;
   assert(!state.authoritativeOutfit({100},&category,true));
   assert(fetch.forced==1&&fetch.last.value==100&&category.version==-1&&category.desc==-1&&category.fetching==0);
   assert(state.acceptedCOFVersion==17&&accepted.size()==8&&avatar.pendingCOFVersion==17);
   // An incomplete fetch retains the entire accepted outfit, not login fallback.
   assert(!state.authoritativeOutfit({100},&category,false));assert(fetch.starts==1&&accepted.size()==8);
   // The authoritative response unlocks ordinary publication with the guard intact.
   category.version=17;category.desc=8;
   assert(state.authoritativeOutfit({100},&category,true));assert(originalAdmission(avatar,accepted,17));
   category.version=18;assert(state.authoritativeOutfit({100},&category,true));assert(originalAdmission(avatar,accepted,18));
   state.acceptedCOFVersion=-1;assert(state.authoritativeOutfit({100},nullptr,false)); // initial fallback remains valid
  }
  '''.replace('METHOD',helper).replace('GUARD',guard)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'recovery.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code,encoding='utf-8')
   run=subprocess.run([shutil.which('clang++')or shutil.which('g++'),'-std=c++17','-Wall','-Werror',str(cpp),'-o',str(exe)],capture_output=True,text=True);assert not run.returncode,run.stdout+run.stderr
   run=subprocess.run([str(exe)],capture_output=True,text=True);assert not run.returncode,run.stdout+run.stderr


if __name__=='__main__':unittest.main()
