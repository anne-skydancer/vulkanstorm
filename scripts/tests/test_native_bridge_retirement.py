"""Compile actual bridge retirement; never fabricate an attached native bridge."""
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


class NativeBridgeRetirementTest(unittest.TestCase):
    def test_actual_retirement_preserves_legacy_availability(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source = (ROOT / 'indra/newview/fslslbridge.cpp').read_text()
        definitions = method(source, 'static bool fs_bridge_scene_owner_available()') + '\n' + method(source, 'std::function<bool()> fs_bridge_account_guard()') + '\n' + method(source, 'void FSLSLBridge::resetNativeAccount()') + '\n' + method(source, 'bool FSLSLBridge::canUseBridge()')
        fixture = (ROOT / 'scripts/tests/native_bridge_retirement_fixture.cpp').read_text().replace('DEFINITIONS', definitions)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'bridge_retirement.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_late_callbacks_and_scene_entrypoints_are_guarded(self):
        source = (ROOT / 'indra/newview/fslslbridge.cpp').read_text()
        for signature in ('void FSLSLBridge::initBridge()', 'void FSLSLBridge::recreateBridge()',
                          'void FSLSLBridge::startCreation()', 'void FSLSLBridge::processAttachItem(',
                          'void FSLSLBridge::processDetachItem(', 'void FSLSLBridge::onIdle(',
                          'void FSLSLBridgeRezCallback::fire(', 'void FSLSLBridgeScriptCallback::fire('):
            body = method(source, signature).split('#endif', 1)[0]
            self.assertIn('fs_bridge_scene_owner_available()', body)
            self.assertIn('return;', body)
        session = (ROOT / 'indra/newview/vsnativesession.cpp').read_text()
        for signature in ('void VSNativeSession::reset()', 'bool VSNativeSession::changeRegion('):
            body = method(session, signature)
            self.assertIn('FSLSLBridge::instanceExists()', body)
            self.assertIn('resetNativeAccount()', body)
        header = (ROOT / 'indra/newview/fslslbridge.h').read_text()
        self.assertEqual(header.count('mAccountCurrent = fs_bridge_account_guard()'), 7)
        self.assertEqual(header.count('if (mAccountCurrent()) FSLSLBridge::instance()'), 2)
        for signature in ('bool FSLSLBridgeCleanupTimer::tick()', 'bool FSLSLBridgeReAttachTimer::tick()', 'bool FSLSLBridgeStartCreationTimer::tick()'):
            self.assertIn('if (!mAccountCurrent()', method(source, signature))


if __name__ == '__main__':
    unittest.main()
