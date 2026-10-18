"""Run the production attachment-resource coroutine with retiring UI/accounts."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]

class AttachmentResources(unittest.TestCase):
    def test_actual_request_failure_and_late_reply_ownership(self):
        source=(ROOT/'indra/newview/llfloatermyscripts.cpp').read_text()
        request=method(source,'bool LLFloaterMyScripts::requestAttachmentDetails()')
        branch=request[request.index('#if VS_NATIVE_VULKAN')+len('#if VS_NATIVE_VULKAN'):request.index('#endif')]
        request='bool LLFloaterMyScripts::requestAttachmentDetails()\n{\n'+branch+'\nreturn false;\n}'
        fixture=Path(__file__).with_name('native_attachment_resources_fixture.cpp').read_text().replace('REQUEST',request)
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as folder:
            cpp=Path(folder)/'resources.cpp';exe=Path(folder)/'resources.exe';cpp.write_text(fixture)
            result=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__=='__main__':unittest.main()
