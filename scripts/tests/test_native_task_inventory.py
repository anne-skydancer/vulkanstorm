"""Compile native task-inventory publication/retirement methods; no scene or device required."""
from pathlib import Path
import shutil
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).with_name("vstaskinventory.cpp")
if not SOURCE.is_file():
    SOURCE = ROOT / "indra/newview/vstaskinventory.cpp"


def body(source, signature):
    start = source.index(signature)
    start += re.search(r"\n[ \t]*\{", source[start:]).end() - 1
    depth = 0
    for index in range(start, len(source)):
        depth += (source[index] == "{") - (source[index] == "}")
        if depth == 0:
            return source[start:index + 1]
    raise AssertionError(signature)


class NativeTaskInventoryTest(unittest.TestCase):
    def test_atomic_publication_and_reentrant_retirement(self):
        source = SOURCE.read_text()
        fixture = r'''
#include <cassert>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
using U16=unsigned short;using U64=unsigned long long;using F64=double;
using LLUUID=std::string;
struct LLInventoryObject {using object_list_t=std::list<int>;};
struct VSWorldScene {struct ObjectHandle {LLUUID id;};};
struct LLFrameTimer {inline static double now=0;static double getTotalSeconds(){return now;}};
std::vector<U64> aborted;
struct Xfer {void abortRequestById(U64 id,int){aborted.push_back(id);}} xfer;
auto* gXferManager=&xfer;
struct VSTaskInventory {
 struct Result {VSWorldScene::ObjectHandle object;U16 serial=0;bool success=false;LLInventoryObject::object_list_t contents;};
 using Callback=std::function<void(const Result&)>;
 struct Impl {
  struct Request {
   VSWorldScene::ObjectHandle object;
   std::function<bool()> current;
   std::vector<Callback> callbacks;
   U64 ticket=0,transfer=0;U16 serial=0;bool receiving=false;F64 deadline=0;
  };
  std::map<LLUUID,Request> requests;bool active=true;
  Request* find(const LLUUID& id,U64 ticket) FIND
  void finish(const LLUUID& id,U64 ticket,bool success,U16 serial,LLInventoryObject::object_list_t contents={}) FINISH
 };
 std::shared_ptr<Impl> mImpl=std::make_shared<Impl>();
 void tick() TICK
 void reset() RESET
};
int main(){
 VSTaskInventory inventory;auto impl=inventory.mImpl;
 bool current=true;int deliveries=0;
 auto add=[&](U64 ticket,U64 transfer=0){
  VSTaskInventory::Impl::Request entry;entry.object.id="object";entry.ticket=ticket;entry.transfer=transfer;
  entry.current=[&]{return current;};entry.deadline=10;
  entry.callbacks.push_back([&](const auto& result){assert(result.object.id=="object");++deliveries;});
  impl->requests["object"]=entry;
 };
 add(1);impl->finish("object",2,true,2,{9});assert(deliveries==0&&impl->requests.size()==1);
 current=false;impl->finish("object",1,true,1,{9});assert(deliveries==0);
 inventory.tick();assert(impl->requests.empty());current=true;
 add(2);impl->requests["object"].callbacks.push_back([&](const auto& result){
  assert((result.success&&result.serial==65535&&result.contents==LLInventoryObject::object_list_t{1,2}));
  assert(impl->requests.empty());add(3);
 });
 impl->finish("object",2,true,65535,{1,2});assert(deliveries==1);assert(impl->requests.size()==1);assert(impl->requests.at("object").ticket==3);assert(impl->find("object",3));
 impl->finish("object",2,true,2);assert(deliveries==1);assert(impl->requests.size()==1);assert(impl->requests.at("object").ticket==3);assert(impl->find("object",3));
 add(4,44);LLFrameTimer::now=11;
 impl->requests["object"].callbacks.push_back([&](const auto& result){assert(!result.success);add(5,55);});
 inventory.tick();assert(deliveries==2&&impl->find("object",5));assert(aborted==std::vector<U64>{44});
 impl->requests["object"].callbacks.insert(impl->requests["object"].callbacks.begin(),[&](const auto&){inventory.reset();});
 impl->finish("object",5,true,5);assert(!impl->active&&impl->requests.empty()&&deliveries==2);
 inventory.reset();assert(aborted==std::vector<U64>{44});
 VSTaskInventory other;other.mImpl->requests["x"]={{"x"},[]{return true;},{},6,66,0,false,10};
 other.reset();assert(other.mImpl->requests.empty()&&aborted==std::vector<U64>({44,66}));
}
'''
        for token, signature in (
            ("FIND", "Request* find("), ("FINISH", "void finish("),
            ("TICK", "void VSTaskInventory::tick()"), ("RESET", "void VSTaskInventory::reset()"),
        ):
            fixture = fixture.replace(token, body(source, signature))
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler, "C++ compiler required for actual owner method checks")
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "task_inventory.cpp"
            executable = Path(temp) / "task_inventory.exe"
            path.write_text(fixture)
            built = subprocess.run([compiler, "-std=c++17", str(path), "-o", str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
