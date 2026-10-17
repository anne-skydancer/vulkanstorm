import unittest
import native_source_fixture as module
class SourceExtractor(unittest.TestCase):
 def test_comments_strings_forward_declarations_and_nested_default_braces(self):
  source=r'''// void target() { obsolete(); }
/* void target() { old_prototype(); } */
const char*quoted="void target() { incorrect(); }";
const char*raw=R"glsl(void target() { "ignored"; })glsl";
void target();
void target() { const char*text="}"; /* } */ if(true){ correct(); } }
'''
  expected='void target() { const char*text="}"; /* } */ if(true){ correct(); } }'
  self.assertEqual(module.method(source,'void target('),expected)
  self.assertEqual(module.method('void f(std::function<void()> cb={}) { cb(); }','void f('),'void f(std::function<void()> cb={}) { cb(); }')
  with self.assertRaises(AssertionError):module.method('// void absent() {}','void absent(')
if __name__=='__main__':unittest.main()
