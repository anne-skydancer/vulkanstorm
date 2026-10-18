"""Compare the peer media permission implementation with the frozen GL policy."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MediaPolicyTests(unittest.TestCase):
    def test_original_branch_order_and_lazy_permission_queries(self):
        header = (ROOT / 'indra/newview/vsmediapolicy.h').read_text()
        header = header.replace('#pragma once', '').replace('#include "llmediaentry.h"', '')
        reference = (Path(__file__).parent / 'media_permission_gl_reference.cpp').read_text()
        fixture = r'''
#include <cassert>
#include <cstddef>
#include <vector>
using U8 = unsigned char;
struct LLMediaEntry {
 enum { PERM_ANYONE=1, PERM_GROUP=2, PERM_OWNER=4 };
 U8 interact=0,control=0;
 U8 getPermsInteract() const { return interact; }
 U8 getPermsControl() const { return control; }
};
std::vector<int> trace;
struct LLPermissions { int getGroup()const { trace.push_back(2);return 7; } };
struct Agent { bool allowed=false;bool isInGroup(int){trace.push_back(3);return allowed;} } gAgent;
struct LLSelectMgr {
 bool known=false; LLPermissions permission;
 static LLSelectMgr* getInstance(){static LLSelectMgr manager;return &manager;}
 LLPermissions* findObjectPermissions(void*){trace.push_back(1);return known?&permission:nullptr;}
};
struct LLVOVolume {
 enum MediaPermType { MEDIA_PERM_INTERACT, MEDIA_PERM_CONTROL };
 bool owner=false;
 bool permYouOwner(){trace.push_back(4);return owner;}
 bool hasMediaPermission(const LLMediaEntry*,MediaPermType);
};
REFERENCE
HEADER
int main() {
 for(int interact=0;interact<8;++interact)
 for(int control=0;control<8;++control)
 for(int known=0;known<2;++known)
 for(int group=0;group<2;++group)
 for(int owner=0;owner<2;++owner)
 for(int type=0;type<2;++type)
 for(int present=0;present<2;++present) {
  LLMediaEntry entry{U8(interact),U8(control)};
  const auto* data=present?&entry:nullptr;
  LLSelectMgr::getInstance()->known=known;gAgent.allowed=group;
  LLVOVolume object;object.owner=owner;
  trace.clear();
  const bool expected=object.hasMediaPermission(data,LLVOVolume::MediaPermType(type));
  const auto expectedTrace=trace;trace.clear();
  const bool actual=vs_has_media_permission(data,type==0,[&]{
   auto* permission=LLSelectMgr::getInstance()->findObjectPermissions(&object);
   return permission && gAgent.isInGroup(permission->getGroup());
  },[&]{return object.permYouOwner();});
  assert(actual==expected && trace==expectedTrace);
 }
}
'''.replace('REFERENCE', reference).replace('HEADER', header)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'policy.cpp'
            exe = Path(directory) / 'policy.exe'
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
