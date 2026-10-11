"""Exercise production queued-request and callback account lifetime guards."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_event_notifier_account import function


class ExperienceAccountLifetimeTest(unittest.TestCase):
    def test_independent_capability_grants_populate_available_account_tabs(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/newview/llfloaterexperiences.cpp').read_text()
        methods = '\n'.join(function(source, signature) for signature in (
            'void LLFloaterExperiences::refreshContents()',
            'void LLFloaterExperiences::updateInfo(',
        ))
        fixture = r'''
#include <cassert>
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace boost {using std::bind;}using std::placeholders::_1;using std::placeholders::_2;
struct LLSD {};struct LLPanelExperiences {};template<class T>struct LLHandle {};
struct Agent {std::map<std::string,std::string> grants;std::string getRegionCapability(std::string cap){return grants[cap];}}gAgent;
struct LLFloaterExperiences {using NameMap_t=std::map<std::string,std::string>;
 using Callback_t=std::function<void(LLPanelExperiences*,const LLSD&)>;std::vector<std::string> fetched;
 template<class T>LLHandle<T>getDerivedHandle(){return {};}
 void setupRecentTabs(){}void checkPurchaseInfo(LLPanelExperiences*,const LLSD&)const{}
 void retrieveExperienceList(std::string cap,LLHandle<LLFloaterExperiences>,NameMap_t,std::string="",Callback_t={}){assert(!cap.empty());fetched.push_back(cap);}
 void refreshContents();void updateInfo(std::string,std::string);};
METHODS
int main(){gAgent.grants={{"GetAdminExperiences","admin"},{"GetCreatorExperiences","creator"},{"AgentExperiences","owned"}};
 LLFloaterExperiences viewer;viewer.refreshContents();assert((viewer.fetched==std::vector<std::string>{"admin","creator","owned"}));
 viewer.fetched.clear();gAgent.grants.clear();gAgent.grants["GetExperiences"]="allowed";viewer.refreshContents();assert((viewer.fetched==std::vector<std::string>{"allowed"}));}
'''.replace('METHODS', methods)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'experience_tabs.cpp'
            path.write_text(fixture)
            executable = Path(directory) / 'experience_tabs.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_retired_queue_does_not_use_new_cap_and_retired_results_do_not_dispatch(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/llmessage/llexperiencecache.cpp').read_text()
        methods = '\n'.join(function(source, signature) for signature in (
            'LLExperienceCache::ExperienceGetFn_t LLExperienceCache::guardAccountCallback',
            'void LLExperienceCache::enqueueAccountCoprocedure',
        ))
        header = (root / 'indra/newview/vsnativeim.h').read_text()
        callback = function(header, 'auto vs_native_im_account_callback(Callback callback)')
        fixture = r'''
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
struct LLSD {};struct LLUUID {};
namespace LLCoreHttpUtil {struct HttpCoroutineAdapter {using ptr_t=std::shared_ptr<HttpCoroutineAdapter>;};}
using Procedure=std::function<void(LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t&,const LLUUID&)>;
struct LLCoprocedureManager {std::vector<Procedure> queue;static LLCoprocedureManager& instance(){static LLCoprocedureManager manager;return manager;}
 void enqueueCoprocedure(const std::string&,const std::string&,Procedure p){queue.push_back(p);}
 void run(){auto pending=std::move(queue);queue.clear();LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t adapter;for(auto& p:pending)p(adapter,{});}};
int account=1;
std::function<bool()> vs_native_im_guard(){int epoch=account;return [epoch]{return account==epoch;};}
template<class Callback> CALLBACK
struct LLExperienceCache {using ExperienceGetFn_t=std::function<void(const LLSD&)>;
 std::function<std::function<bool()>()> mAccountGuardQuery;
 ExperienceGetFn_t guardAccountCallback(ExperienceGetFn_t);
 void enqueueAccountCoprocedure(const std::string&,const std::string&,Procedure);};
METHODS
int main(){LLExperienceCache cache;cache.mAccountGuardQuery=vs_native_im_guard;
 int requests=0,results=0,ui=0;std::string cap="old account cap";
 auto oldResult=cache.guardAccountCallback([&](const LLSD&){++results;});
 cache.enqueueAccountCoprocedure("ExpCache","UpdateExperience",[&](auto&,const auto&){++requests;assert(cap=="old account cap");});
 auto oldUI=vs_native_im_account_callback([&](const LLSD&){++ui;return true;});
 assert(oldUI(LLSD{}));assert(ui==1);++account;cap="new account cap";
 LLCoprocedureManager::instance().run();oldResult({});assert(requests==0&&results==0&&!oldUI(LLSD{})&&ui==1);
 auto newResult=cache.guardAccountCallback([&](const LLSD&){++results;});
 cache.enqueueAccountCoprocedure("ExpCache","UpdateExperience",[&](auto&,const auto&){++requests;assert(cap=="new account cap");newResult({});});
 LLCoprocedureManager::instance().run();assert(requests==1&&results==1);
 auto voidUI=vs_native_im_account_callback([&](int n){ui+=n;});voidUI(2);assert(ui==3);++account;voidUI(2);assert(ui==3);
 // An ordinary OpenGL caller without an account policy remains functional.
 LLExperienceCache baseline;auto ordinary=baseline.guardAccountCallback([&](const LLSD&){++results;});
 baseline.enqueueAccountCoprocedure("ExpCache","Search",[&](auto&,const auto&){ordinary({});});
 LLCoprocedureManager::instance().run();assert(results==2);
}
'''.replace('METHODS', methods).replace('CALLBACK', callback)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'experience_account.cpp'
            path.write_text(fixture)
            executable = Path(directory) / 'experience_account.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
