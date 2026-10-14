"""Exercise the production JSON allocator without a device or window."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT=Path(__file__).resolve().parents[2]

class WorldGLTFOwnership(unittest.TestCase):
    def test_actual_parser_allocator_rejection_and_last_owner_accounting(self):
        code=(ROOT/'indra/newview/vsworldgltf.cpp').read_text()
        allocator=method(code,'class BudgetJSONResource')+';'
        fixture=r"""
#include "vsresourcebudget.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <map>
#include <new>
namespace boost::container::pmr {
// Adapter for the installed polymorphic-resource ABI. Allocation, alignment,
// exception rollback and accounting below are the actual production methods.
class memory_resource {
 virtual void* do_allocate(std::size_t,std::size_t)=0;
 virtual void do_deallocate(void*,std::size_t,std::size_t)=0;
 virtual bool do_is_equal(const memory_resource&)const noexcept=0;
public:
 virtual ~memory_resource()=default;
 void* allocate(std::size_t n,std::size_t a){return do_allocate(n,a);}
 void deallocate(void* p,std::size_t n,std::size_t a){do_deallocate(p,n,a);}
};
}
constexpr std::size_t maximumBytes=64*1024*1024;
ALLOCATOR
int main() {
 VSResourceBudget budget(1200);
 {
  BudgetJSONResource resource(budget);
  auto* first=resource.allocate(512,64);
  assert(reinterpret_cast<std::size_t>(first)%64==0);
  const auto used=budget.used();assert(used>512&&used<1200);
  bool refused=false;try{resource.allocate(800,16);}catch(const std::bad_alloc&){refused=true;}
  assert(refused&&budget.used()==used);
  refused=false;try{resource.allocate(maximumBytes+1,16);}catch(const std::bad_alloc&){refused=true;}
  assert(refused&&budget.used()==used);
  resource.deallocate(first,512,64);assert(budget.used()==0);
  auto pressure=budget.reserve(1100);assert(pressure);
  refused=false;try{resource.allocate(256,16);}catch(const std::bad_alloc&){refused=true;}
  assert(refused&&budget.used()==1100);
  pressure.reset();assert(budget.used()==0);
  // Parser resource cleanup must release every outstanding allocation as well.
  resource.allocate(128,16);assert(budget.used()>128);
 }
 assert(budget.used()==0);
}
""".replace('ALLOCATOR',allocator)
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/'ownership.cpp';binary=source.with_suffix('.exe')
            source.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',
                                  '-I',str(ROOT/'indra/newview'),str(source),'-o',str(binary)],
                                 capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

if __name__=='__main__':unittest.main()
