"""Compare real owner command processing with the original viewer controller."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def method(source, signature):
    begin = source.index(signature)
    end = source.index('{', begin) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]

class OwnerChatPolicy(unittest.TestCase):
    def test_original_command_results_side_effects_and_account_retirement(self):
        current = method((ROOT/'indra/newview/llviewermessage.cpp').read_text(), 'VSOwnerChatResult vs_process_owner_chat(')
        fixture = (HERE/'native_owner_chat_fixture.cpp').read_text()
        fixture = fixture.replace('REFERENCE_POLICY', (HERE/'native_owner_chat_reference.cpp').read_text())
        fixture = fixture.replace('CURRENT_POLICY', current)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)/'owner_chat.cpp'
            executable = source.with_suffix('.exe')
            source.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', '-O2', str(source), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

if __name__ == '__main__':
    unittest.main()
