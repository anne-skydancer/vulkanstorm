"""A surviving viewer cannot qualify after an abnormal media-child death."""
from pathlib import Path
import ast
import re
import shutil
import subprocess
import tempfile
import unittest


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class MediaPluginCrashEvidenceTest(unittest.TestCase):
    def test_actual_cross_platform_child_status_formats_are_decisive(self):
        root = Path(__file__).resolve().parents[2]
        source = (root/'scripts/run_vulkan_session_replay.py').read_text()
        node = next(item for item in ast.parse(source).body if isinstance(item, ast.FunctionDef) and item.name == 'require_no_child_process_crashes')
        namespace = {'re': re}
        exec(compile(ast.Module(body=[node], type_ignores=[]), '<production child crash guard>', 'exec'), namespace)
        check = namespace['require_no_child_process_crashes']
        # The runner must evaluate the combined process log before reporting a
        # successful session, rather than merely defining an unused helper.
        main = next(item for item in ast.parse(source).body if isinstance(item, ast.FunctionDef) and item.name == 'main')
        calls = [item for item in ast.walk(main) if isinstance(item, ast.Call) and isinstance(item.func, ast.Name) and item.func.id == 'require_no_child_process_crashes']
        self.assertEqual(len(calls), 1)
        self.assertEqual(ast.unparse(calls[0].args[0]), 'log')

        cpp = (root/'indra/llcommon/llprocess.cpp').read_text()
        formatter = function(cpp, 'std::string LLProcess::getStatusString(const std::string& desc, const Status& status)')
        fixture = r'''
#include <iostream>
#include <sstream>
#include <string>
#define STRINGIZE(expression) ([&]{std::ostringstream text;text << expression;return text.str();}())
const char* apr_signal_description_get(int){return "signal description";}
struct LLProcess {enum{UNSTARTED,RUNNING,EXITED,KILLED};struct Status{int mState;unsigned mData;};
 static std::string getStatusString(const std::string&,const Status&);};
FORMATTER
int main(){
 for(unsigned status:{0xc0000005u,0xc0000409u,6u,7u,11u,9u,15u})
  std::cout << "INFO #LLProcess# LLProcess::handle_status : " << LLProcess::getStatusString("SLPlugin.exe (42)",{LLProcess::KILLED,status}) << '\n';
 std::cout << "INFO #LLProcess# LLProcess::handle_status : " << LLProcess::getStatusString("SLPlugin.exe (42)",{LLProcess::EXITED,0}) << '\n';
 std::cout << "INFO #LLProcess# LLProcess::handle_status : SLPlugin.exe (42) exited with code -1\n";
}
'''.replace('FORMATTER', formatter)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            path = directory/'status.cpp'; path.write_text(fixture)
            for platform in (0, 1):
                executable = directory/f'status-{platform}.exe'
                built = subprocess.run([compiler, '-std=c++17', f'-DLL_WINDOWS={platform}', str(path), '-o', str(executable)], capture_output=True, text=True, timeout=120)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                output = subprocess.run([str(executable)], capture_output=True, text=True, check=True, timeout=15).stdout.splitlines()
                # Faults, unsolicited SIGTERM/SIGKILL and nonzero exits fail.
                for line in output[:7]:
                    with self.assertRaisesRegex(RuntimeError, 'Media plugin crashed'):
                        check(line)
                check(output[7])  # Normal exit zero.
                with self.assertRaisesRegex(RuntimeError, 'Media plugin failed'):
                    check(output[8])
                intent = 'INFO #LLProcess# LLProcess::kill : owner killing SLPlugin.exe (42)\n'
                check(intent + output[8])  # Actual Windows TerminateProcess(-1).
                if not platform:
                    check(intent + output[6])  # Actual POSIX requested SIGTERM.
                    with self.assertRaisesRegex(RuntimeError, 'Media plugin crashed'):
                        check(intent + output[5])  # SIGKILL is not requested by LLProcess.
                    with self.assertRaisesRegex(RuntimeError, 'Media plugin crashed'):
                        check(intent.replace('(42)', '(41)') + output[6])
        check('normal startup without child exits')
        check('user chat text mentioning SLPlugin.exe (42) killed with exception c0000005')


if __name__ == '__main__':
    unittest.main()
