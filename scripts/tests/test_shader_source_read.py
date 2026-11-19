# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The shader loader reads a GLSL file whole: no line cap, no silent drop."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

PROGRAM = r'''
#include "llshadersource.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using V = std::vector<std::string>;
static std::FILE* open(const std::string& text) {
    std::FILE* f = std::fopen(TMP_PATH, "w+b"); assert(f);
    std::fwrite(text.data(), 1, text.size(), f); std::rewind(f); return f;
}
static std::string join(const V& v) { std::string s; for (auto& p : v) s += p; return s; }
int main() {
    // Far past the old 4096-line cap, with a line longer than one fgets chunk and no final newline.
    std::string text = "/*[EXTRA_CODE_HERE]*/\n";
    for (int i = 0; i < 20000; ++i) text += "line " + std::to_string(i) + "\n";
    text += std::string(5000, 'x') + "\nlast";
    std::FILE* f = open(text); V lines;
    assert(ll_read_shader_lines(f, lines)); std::fclose(f);
    assert(lines.size() == 1 + 20000 + 2);
    assert(lines[20001] == std::string(5000, 'x') + "\n");
    assert(lines.back() == "last");
    V out = ll_assemble_shader_source("#version 420\n", {"A\n", "B\n"}, lines);
    assert(out[0] == "#version 420\n" && out[1] == "A\n" && out[2] == "B\n" && out[3] == "line 0\n");
    assert(join(out) == "#version 420\nA\nB\n" + text.substr(text.find('\n') + 1));
    assert(out.back() == "last");

    // Marker late in the file: extra code goes there, the marker line is dropped.
    out = ll_assemble_shader_source("V\n", {"E\n"}, {"a\n", "b\n", "c\n", "// [EXTRA_CODE_HERE]\n", "d\n", "[EXTRA_CODE_HERE]\n"});
    assert(join(out) == "V\na\nb\nc\nE\nd\n[EXTRA_CODE_HERE]\n");
    // No marker: extra code follows the version line.
    out = ll_assemble_shader_source("V\n", {"E\n"}, {"a\n", "b\n"});
    assert(join(out) == "V\nE\na\nb\n");
    // Empty file.
    out = ll_assemble_shader_source("V\n", {"E\n"}, {});
    assert(join(out) == "V\nE\n");
    return 0;
}
'''


class ShaderSourceReadTest(unittest.TestCase):
    def test_whole_file_is_read_and_assembled(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'shader_source.cpp'
            binary = source.with_suffix('.exe')
            tmp = (Path(directory) / 'shader.glsl').as_posix()
            source.write_text('#define TMP_PATH "%s"\n' % tmp + PROGRAM)
            built = subprocess.run([compiler, '-std=c++17', '-UNDEBUG', '-I', str(ROOT / 'indra/llrender'),
                                    str(source), '-o', str(binary)],
                                   capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_loader_uses_the_helpers_and_keeps_no_fixed_caps(self):
        text = (ROOT / 'indra/llrender/llshadermgr.cpp').read_text()
        self.assertIn('ll_read_shader_lines', text)
        self.assertIn('ll_assemble_shader_source', text)
        self.assertNotIn('shader_code_text[4096', text)


if __name__ == '__main__':
    unittest.main()
