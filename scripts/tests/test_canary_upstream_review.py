"""Run actual reviewed C++ methods with device-free dependency fixtures."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).parent / 'fixtures'


def method(source, signature):
    """Extract one unchanged definition, counting braces outside comments/literals."""
    start = source.index(signature)
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda match: ' ' * len(match.group()), source, flags=re.S)
    opening = masked.index('{', start)
    depth = 0
    for index in range(opening, len(masked)):
        depth += (masked[index] == '{') - (masked[index] == '}')
        if depth == 0:
            return source[start:index + 1]
    raise ValueError(f'Unterminated method: {signature}')


class CanaryUpstreamReviewTest(unittest.TestCase):
    def run_fixture(self, fixture, source, signatures, extra=''):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required for these regressions')
        production = (ROOT / source).read_text(encoding='utf-8')
        functions = '\n'.join(method(production, signature) for signature in signatures) + extra
        text = (FIXTURES / fixture).read_text(encoding='utf-8')
        self.assertEqual(text.count('// PRODUCTION_METHODS'), 1)
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary) / fixture
            executable = Path(temporary) / 'regression.exe'
            cpp.write_text(text.replace('// PRODUCTION_METHODS', functions), encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                    str(cpp), '-o', str(executable)], capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_voice_activation_and_pending_device_reset_balance_deployments(self):
        self.run_fixture('webrtc_deployment.cpp', 'indra/llwebrtc/llwebrtc.cpp', [
            'void LLWebRTCImpl::setVoiceEnabled(bool enable)',
            'void LLWebRTCImpl::deployDevices(bool reset_module)',
            'void LLWebRTCImpl::workerDeployDevices(bool reset_module)'])

    def test_missing_and_variable_region_extents_invalidate_or_validate_tracking(self):
        decoders = ''
        for name, source in [('decodeViewer', 'llworldmapmessage.cpp'),
                             ('decodeFS', 'fsworldmapmessage.cpp')]:
            text = (ROOT / 'indra/newview' / source).read_text(encoding='utf-8')
            start = re.search(r'if\s*\(msg->getNumberOfBlocksFast\(_PREHASH_Size\) > 0\)', text).start()
            end = text.index('// </FS:CR>', start)
            # Compile the unchanged extent-reading/validation block used by each
            # live decoder, then feed its output through production insertRegion.
            decoders += f'\nvoid {name}(Message* msg, U16& x_size, U16& y_size) {{\nint block = 0;\n'
            decoders += text[start:end] + '\n}\n'
        self.run_fixture('worldmap_extents.cpp', 'indra/newview/llworldmap.cpp', [
            'bool LLWorldMap::insertRegion(U32 x_world, U32 y_world, U16 x_size, U16 y_size,',
            'bool LLWorldMap::isTrackingInRectangle(F64 x0, F64 y0, F64 x1, F64 y1)'], decoders)


if __name__ == '__main__':
    unittest.main()
