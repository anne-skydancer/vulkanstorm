#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Tests of scripts/shadows/check_legacy_shadows.py: the tree passes, and a deliberate edit inside
any <ShadowLegacy> region (or a missing or extra region) makes it fail."""
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('check_legacy_shadows', ROOT / 'scripts' / 'shadows' / 'check_legacy_shadows.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)

TEXT = (ROOT / checker.PIPELINE).read_text(encoding='utf-8')


def region_span(text, name):
    """(lines, index of the first line inside the region, index of its closing marker)."""
    lines = text.splitlines(keepends=True)
    start = next(i for i, l in enumerate(lines) if checker.OPEN.match(l) and f'"{name}"' in l)
    end = next(i for i in range(start, len(lines)) if checker.CLOSE.match(lines[i]))
    return lines, start + 1, end


def first_code_line(lines, a, b):
    return next(i for i in range(a, b) if lines[i].strip() and not lines[i].strip().startswith('//'))


class LegacyShadowCheck(unittest.TestCase):
    def test_tree_passes(self):
        self.assertEqual(checker.check(TEXT), [])

    def test_whitespace_and_blank_lines_are_ignored(self):
        lines, a, b = region_span(TEXT, 'sun-map-size')
        lines[a] = '\n\n        ' + lines[a].strip().replace(' = ', '  =   ') + '\n'
        self.assertEqual(checker.check(''.join(lines)), [])

    def test_edit_inside_each_region_fails(self):
        for name in sorted(checker.reference_set()):
            lines, a, b = region_span(TEXT, name)
            i = first_code_line(lines, a, b)
            edits = {
                'comment appended': lambda l: l.rstrip('\n') + ' // edited\n',
                'space in call': lambda l: l.replace('(', '( ', 1) if '(' in l else l.rstrip('\n') + ' x\n',
                'constant changed': lambda l: l.replace('0', '1', 1) if '0' in l else l.rstrip('\n') + ' + 1\n',
            }
            for what, edit in edits.items():
                changed = list(lines)
                changed[i] = edit(lines[i])
                with self.subTest(region=name, edit=what):
                    self.assertNotEqual(checker.check(''.join(changed)), [], f'edit of line {i + 1} in {name} passed')

    def test_deleted_line_and_extra_line_fail(self):
        for name in sorted(checker.reference_set()):
            lines, a, b = region_span(TEXT, name)
            i = first_code_line(lines, a, b)
            deleted = lines[:i] + lines[i + 1:]
            self.assertNotEqual(checker.check(''.join(deleted)), [], f'deleting a line in {name} passed')
            added = lines[:i] + ['    int extra = 0;\n'] + lines[i:]
            self.assertNotEqual(checker.check(''.join(added)), [], f'adding a line in {name} passed')

    def test_missing_extra_and_unclosed_region_fail(self):
        lines, a, b = region_span(TEXT, 'sun-map-size')
        without = lines[:a - 1] + lines[b + 1:]
        self.assertNotEqual(checker.check(''.join(without)), [])
        self.assertNotEqual(checker.check(TEXT + '\n// <ShadowLegacy id="other">\nint x;\n// </ShadowLegacy>\n'), [])
        self.assertNotEqual(checker.check(''.join(lines[:b] + lines[b + 1:])), [])

    def test_allowed_matches_reference_directory(self):
        self.assertEqual(set(checker.ALLOWED), checker.reference_set())
        self.assertTrue(checker.reference_set(), 'no reference excerpts: the check would pass vacuously')

    def test_conditionals_and_block_comments_in_a_region_fail(self):
        for name in sorted(checker.reference_set()):
            lines, a, b = region_span(TEXT, name)
            for extra in ('#if 0\n', '#else\n', '#ifdef X\n', '/* c */\n'):
                changed = lines[:a] + [extra] + lines[a:]
                with self.subTest(region=name, extra=extra.strip()):
                    self.assertNotEqual(checker.check(''.join(changed)), [])

    def test_legacy_calls_must_sit_in_the_legacy_branch(self):
        for call in checker.LEGACY_CALLS:
            self.assertNotEqual(checker.check(TEXT.replace(call, 'somethingElse(')), [], call)
        # swap the branch test so the calls sit under PCSS
        old = 'if (RenderShadowMethod == LLShadowMethod::LEGACY)\n    {\n        if (!computeSunSplitsLegacy'
        swapped = TEXT.replace(old, old.replace('LEGACY', 'PCSS'))
        self.assertNotEqual(swapped, TEXT)
        self.assertNotEqual(checker.check(swapped), [])

    def test_allowed_substitution_is_exact(self):
        # computeSunSplitsLegacy returns false instead of leaving the function; any other return is a change
        lines, a, b = region_span(TEXT, 'sun-splits')
        i = next(i for i in range(a, b) if lines[i].strip() == 'return false;')
        lines[i] = lines[i].replace('return false;', 'return true;')
        self.assertNotEqual(checker.check(''.join(lines)), [])

    def test_command_line_failure_exit_status(self):
        lines, a, b = region_span(TEXT, 'sun-map-size')
        lines[a] = lines[a].replace('resX', 'resY')
        with tempfile.TemporaryDirectory() as d:
            edited = Path(d) / 'pipeline.cpp'
            edited.write_text(''.join(lines), encoding='utf-8')
            old = sys.argv
            sys.argv = ['check_legacy_shadows.py', '--pipeline', str(edited)]
            try:
                self.assertEqual(checker.main(), 1)
            finally:
                sys.argv = old


if __name__ == '__main__':
    unittest.main()
