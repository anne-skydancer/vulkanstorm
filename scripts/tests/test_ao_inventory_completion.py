"""Execute the real AO set scheduling after an asynchronous category fetch."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT=Path(__file__).resolve().parents[2]


class AOInventoryCompletionTest(unittest.TestCase):
    def test_existing_incomplete_set_is_revisited_after_fetch(self):
        source=(ROOT/'indra/newview/aoengine.cpp').read_text()
        update=method(source,'void AOEngine::update()')
        begin=update.index('std::vector<std::pair<LLUUID, AOSet*>> setsToWrite;')
        end=update.index('allComplete = false;',begin)+len('allComplete = false;')
        fixture=r'''
#include <cassert>
#include <sstream>
#include <string>
#include <vector>
#define LL_DEBUGS(...) std::ostringstream()
#define LL_WARNS(...) std::ostringstream()
#define LL_ENDL "\n"
using LLUUID=int;
struct Row {int id;std::string name;int getUUID()const{return id;}const std::string& getName()const{return name;}};
struct AOSet {int id;bool complete=false;std::string name;explicit AOSet(int value):id(value){}
 void setName(const std::string& value){name=value;}bool getComplete()const{return complete;}};
struct LLStringUtil {static void getTokens(const std::string& value,std::vector<std::string>& output,const char*){output.push_back(value);}};
struct Engine {
 std::vector<AOSet*> mSets;
 ~Engine(){for(auto* set:mSets)delete set;}
 AOSet* getSetByName(const std::string& name){for(auto* set:mSets)if(set->name==name)return set;return nullptr;}
 std::vector<AOSet*> schedule(std::vector<Row*>& input){auto* categories=&input;bool allComplete=true;
 SCHEDULE
 }
 std::vector<AOSet*> result;for(const auto& entry:setsToWrite)result.push_back(entry.second);return result;}
};
int main(){Engine engine;Row row{42,"Connected AO"};std::vector<Row*> rows{&row};
 auto first=engine.schedule(rows);assert(first.size()==1&&engine.mSets.size()==1);
 auto* set=first.front();assert(!set->getComplete());
 // The category fetch completed between timer ticks. The same incomplete
 // owner must enter the actual state-reading pass, without creating a duplicate.
 auto second=engine.schedule(rows);assert(second.size()==1&&second.front()==set&&engine.mSets.size()==1);
 set->complete=true;assert(engine.schedule(rows).empty()&&engine.mSets.size()==1);
}
'''.replace('SCHEDULE',update[begin:end])
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'ao_completion.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)


if __name__=='__main__':unittest.main()
