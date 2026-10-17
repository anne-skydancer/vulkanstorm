"""Compare shared action inheritance against the original OpenGL controller."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if not depth:
            return source[start:end + 1]
    raise AssertionError(signature)


class SharedClickAction(unittest.TestCase):
    def test_all_wire_values_match_reference_inheritance(self):
        constants = (ROOT / 'indra/llcommon/indra_constants.h').read_text(encoding='utf-8')
        constants = '\n'.join(re.findall(r'constexpr U8 CLICK_ACTION_\w+\s*=\s*\d+;', constants))
        helper = (ROOT / 'indra/newview/vsobjectinteraction.h').read_text(encoding='utf-8')
        helper = '\n'.join(line for line in helper.splitlines() if not line.startswith(('#include', '#pragma')))
        current = method((ROOT / 'indra/newview/lltoolpie.cpp').read_text(encoding='utf-8'),
                         'U8 final_click_action(LLViewerObject* obj)')
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <initializer_list>
using U8=uint8_t;
struct LLViewerObject {
 U8 action=0;bool attachment=false;LLViewerObject* parent=nullptr;
 bool isAttachment()const{return attachment;}
 U8 getClickAction()const{return action;}
 LLViewerObject* getRootEdit(){return parent;}
};
CONSTANTS
REFERENCE
HELPER
CURRENT
int main(){
 assert(final_click_action(nullptr)==reference_click_action(nullptr));
 for(unsigned object=0;object<256;++object)
  for(unsigned parent=0;parent<256;++parent)
   for(bool attached:{false,true}){
    LLViewerObject root{U8(parent)},child{U8(object),attached,&root};
    assert(vs_object_click_action(child.action,root.action,attached)==reference_click_action(&child));
    assert(final_click_action(&child)==reference_click_action(&child));
    child.parent=nullptr;
    assert(vs_object_click_action(child.action,CLICK_ACTION_TOUCH,attached)==reference_click_action(&child));
    assert(final_click_action(&child)==reference_click_action(&child));
   }
}
'''.replace('CONSTANTS', constants).replace(
            'REFERENCE', (HERE / 'click_action_reference.cpp').read_text(encoding='utf-8')).replace(
            'HELPER', helper).replace('CURRENT', current)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary) / 'actions.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture, encoding='utf-8')
            compiled = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == '__main__':
    unittest.main()
