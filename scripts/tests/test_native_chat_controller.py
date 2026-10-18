"""Compare the actual shared receive controller with the frozen OpenGL flow."""
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

class ChatController(unittest.TestCase):
    def test_frozen_reference_side_effects_and_branch_order(self):
        source = (ROOT/'indra/newview/llviewermessage.cpp').read_text()
        frozen = (HERE/'native_chat_controller_reference.cpp').read_text()
        body = frozen[frozen.index('    // NaCL - Antispam Registry'):]
        body = body[:body.rfind('}') + 1]
        reference = """void reference_receive(LLChat chat, const std::string& from_name, const LLUUID& owner_id, LLMessageSystem* msg)
{
    if (gNonInteractive) return;
    std::string mesg;
    U8 audible_temp;
    const LLUUID from_id = chat.mFromID;
    LLColor4 color(1,1,1,1);
    LLViewerObject* chatter;
""" + body
        reference = reference.replace('&translateSuccess', '&reference_translateSuccess').replace('&translateFailure', '&reference_translateFailure')
        fixture = (HERE/'native_chat_controller_fixture.cpp').read_text()
        fixture = fixture.replace('void translateSuccess(LLChat c,', 'void reference_translateSuccess(LLChat c,')
        fixture = fixture.replace('void translateFailure(LLChat c,', 'void reference_translateFailure(LLChat c,')
        translation = method(source, 'void translateSuccess(') + '\n' + method(source, 'void translateFailure(')
        fixture = fixture.replace('TRANSLATION', translation).replace('REFERENCE', reference)
        fixture = fixture.replace('CURRENT', method(source, 'void vs_receive_chat('))
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory)/'chat_controller.cpp'
            executable = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', '-O2', str(cpp), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

if __name__ == '__main__':
    unittest.main()
