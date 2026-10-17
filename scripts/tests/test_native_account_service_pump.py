"""Compile the viewer's account pump and the production importer tick guard."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class NativeAccountServicePumpTest(unittest.TestCase):
    def test_actual_pump_preserves_owner_and_connection_boundaries(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        app = (ROOT / 'indra/newview/llappviewer.cpp').read_text(encoding='utf-8')
        market = (ROOT / 'indra/newview/llmarketplacefunctions.cpp').read_text(encoding='utf-8')
        definitions = method(market, 'void LLMarketplaceInventoryImporter::update()') + '\n' + method(app, 'static void pump_native_account_services(')
        fixture = r'''
#include <cassert>
using F32=float;
struct LLTimer{
 static inline bool expired=true;
 bool hasExpired(){return expired;}void setTimerExpirySec(float){expired=false;}
};
const float MARKET_IMPORTER_UPDATE_FREQUENCY=1;
struct LLMarketplaceInventoryImporter{
 static inline bool exists=false;static inline int ticks=0,creates=0;
 static bool instanceExists(){return exists;}
 static LLMarketplaceInventoryImporter& instance(){++creates;static LLMarketplaceInventoryImporter owner;return owner;}
 static void update();void updateImport(){++ticks;}
};
namespace LLMarketplaceInventoryNotifications{
 int calls=0;void update(){++calls;}
}
bool avatar=false;void* vs_avatar_character(){return avatar?&avatar:nullptr;}
struct LLGestureMgr{
 static inline bool exists=false;static inline int ticks=0,creates=0;
 static bool instanceExists(){return exists;}
 static LLGestureMgr& instance(){++creates;static LLGestureMgr owner;return owner;}
 void update(){++ticks;}
};
DEFINITIONS
int main(){
 pump_native_account_services(false);
 assert(LLMarketplaceInventoryNotifications::calls==0&&LLMarketplaceInventoryImporter::creates==0&&LLGestureMgr::creates==0);
 pump_native_account_services(true);
 assert(LLMarketplaceInventoryNotifications::calls==1&&LLMarketplaceInventoryImporter::creates==0&&LLGestureMgr::creates==0);
 LLMarketplaceInventoryImporter::exists=true;pump_native_account_services(true);
 assert(LLMarketplaceInventoryImporter::ticks==1);
 pump_native_account_services(true);assert(LLMarketplaceInventoryImporter::ticks==1);
 LLTimer::expired=true;pump_native_account_services(true);assert(LLMarketplaceInventoryImporter::ticks==2);
 LLGestureMgr::exists=true;pump_native_account_services(true);assert(LLGestureMgr::creates==0);
 avatar=true;pump_native_account_services(true);assert(LLGestureMgr::ticks==1);
 pump_native_account_services(false);assert(LLGestureMgr::ticks==1);
 avatar=false;pump_native_account_services(true);assert(LLGestureMgr::ticks==1);
}
'''.replace('DEFINITIONS', definitions)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'native_account_service_pump.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        frame = method(app, 'bool LLAppViewer::frameNativeVulkanLogin()')
        self.assertIn('pump_native_account_services(mNativeSession && mNativeSession->phase() == VSNativeSession::Phase::Connected)', frame)


if __name__ == '__main__':
    unittest.main()
