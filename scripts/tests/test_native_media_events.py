"""Compare shared media navigation/bounce policy with the frozen GL controller."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MediaEvents(unittest.TestCase):
    def test_original_navigation_redirect_download_and_bounce_rules(self):
        source = (ROOT / 'indra/newview/vsmediaevent.h').read_text()
        source = '\n'.join(line for line in source.splitlines() if not line.startswith(('#include', '#pragma')))
        reference = Path(__file__).with_name('media_event_gl_reference.cpp').read_text()
        states = sorted(set(re.findall(r'LLViewerMediaImpl::(MEDIANAVSTATE_\w+)', source)))
        events = sorted(set(re.findall(r'LLViewerMediaObserver::(MEDIA_EVENT_\w+)', source)))
        fixture = r'''
#include <cassert>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#define LL_DEBUGS(x) std::ostringstream()
#define LL_INFOS(x) std::ostringstream()
#define LL_WARNS(x) std::ostringstream()
#define LL_ENDL ""
struct LLViewerMediaObserver{enum EMediaEvent{EVENTS,OTHER};};
struct LLPluginClassMedia {
 std::string location="location",uri="navigate";int responses=0;
 std::string getLocation()const{return location;}std::string getNavigateURI()const{return uri;}
 void sendPickFileResponse(const std::vector<std::string>& value){assert(value.empty());++responses;}
};
struct LLViewerMediaImpl {
 enum State{STATES,OTHER};State state=OTHER;bool failed=false;std::string current;
 std::vector<std::string> navigations;State getNavState()const{return state;}
 void setMediaFailed(bool v){failed=v;}std::string getCurrentMediaURL()const{return current;}
 void navigateTo(const std::string& url,const std::string& type,bool rediscover,bool server){
  assert(type.empty()&&!rediscover&&server);navigations.push_back(url);
 }
};
using viewer_media_t=std::shared_ptr<LLViewerMediaImpl>;
struct LLMediaEntry{
 std::string current,home;std::set<std::string> whitelist;
 std::string getCurrentURL()const{return current;}std::string getHomeURL()const{return home;}
 bool checkCandidateUrl(const std::string& url)const{return whitelist.contains(url);}
};
namespace LLNotificationsUtil{std::vector<std::string> notices;void add(const std::string& v){notices.push_back(v);}}
struct LLVOVolume{
 std::vector<std::string> navigations;
 void mediaEvent(LLViewerMediaImpl*,LLPluginClassMedia*,LLViewerMediaObserver::EMediaEvent);
 void mediaNavigated(LLViewerMediaImpl*,LLPluginClassMedia*,const std::string& url){navigations.push_back(url);}
};
REFERENCE
SHARED
int main(){
 for(int state=0;state<=int(LLViewerMediaImpl::OTHER);++state)
 for(int event=0;event<=int(LLViewerMediaObserver::OTHER);++event){
  LLViewerMediaImpl impl;impl.state=LLViewerMediaImpl::State(state);LLPluginClassMedia plugin;LLVOVolume object;
  LLNotificationsUtil::notices.clear();object.mediaEvent(&impl,&plugin,LLViewerMediaObserver::EMediaEvent(event));
  const auto notices=LLNotificationsUtil::notices;const auto responses=plugin.responses;
  LLNotificationsUtil::notices.clear();plugin.responses=0;std::vector<std::string> navigations;
  vs_media_event(&impl,&plugin,LLViewerMediaObserver::EMediaEvent(event),[&](auto*,auto*,const std::string& url){navigations.push_back(url);});
  assert(navigations==object.navigations&&notices==LLNotificationsUtil::notices&&responses==plugin.responses);
 }
 const std::vector<std::string> urls={"","example.org","https://home","http://bad",":"};
 for(const auto& current:urls)for(const auto& home:urls)
 for(int mask=0;mask<8;++mask)for(int present=0;present<4;++present){
  LLMediaEntry entry;entry.current=current;entry.home=home;
  if(mask&1)entry.whitelist.insert("https://example.org");
  if(mask&2)entry.whitelist.insert("https://home");
  if(mask&4)entry.whitelist.insert(":");
  auto expected=std::make_shared<LLViewerMediaImpl>();auto actual=std::make_shared<LLViewerMediaImpl>();
  expected->current=actual->current="https://home";
  reference_bounce(present&1?&entry:nullptr,present&2?expected:nullptr);
  vs_media_bounce_back(present&1?&entry:nullptr,present&2?actual:nullptr);
  assert(expected->failed==actual->failed&&expected->navigations==actual->navigations);
 }
}
'''.replace('EVENTS', ','.join(events)).replace('STATES', ','.join(states)).replace('REFERENCE', reference).replace('SHARED', source)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / 'events.cpp'
            exe = Path(folder) / 'events.exe'
            cpp.write_text(fixture)
            result = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
