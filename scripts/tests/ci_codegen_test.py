# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Tests of scripts/ci_codegen.py: the commit selection, the ELF fingerprint and the comparison. No compiler is used:
the objects are built byte by byte."""
import argparse
import contextlib
import io
import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import ci_codegen as cg  # noqa: E402


def make_object(functions, symbols=(), debug=b"", comment=b"GCC", machine=62, extra=(), rela_type=4):
    """A minimal ELF64 relocatable object. functions: [(section name, code bytes, [(offset, symbol, addend)])];
    symbols: extra [(name, bind, kind, section index or 0)]. Section symbols are not written (a relocation names its
    target symbol instead)."""
    strtab = b"\0"

    names = b"\0"
    sections = []  # (name, type, flags, content, link, info, align, entsize)
    sym_names = {}
    sym_entries = [b"\0" * 24]
    defined = {s[0] for s in symbols}
    for _name, _code, relocs in functions:
        for _off, target, _addend in relocs:
            if target not in defined:
                sym_names.setdefault(target, None)
    for target in sym_names:
        sym_names[target] = len(strtab)
        strtab += target.encode() + b"\0"
        sym_entries.append(struct.pack("<IBBHQQ", sym_names[target], 0x10, 0, 0, 0, 0))  # global, notype, undefined
    index = {target: i + 1 for i, target in enumerate(sym_names)}
    for name, bind, kind, shndx in symbols:
        offset = len(strtab)
        strtab += name.encode() + b"\0"
        sym_entries.append(struct.pack("<IBBHQQ", offset, (bind << 4) | kind, 0, shndx, 0, 4))
        index[name] = len(sym_entries) - 1
    for name, code, _relocs in functions:
        sections.append((name, 1, 6, code, 0, 0, 16, 0))
    if debug:
        sections.append((".debug_line", 1, 0, debug, 0, 0, 1, 0))
    if comment:
        sections.append((".comment", 1, 0x30, comment, 0, 0, 1, 1))
    for name, typ, flags, content in extra:  # (name, type, flags, content); a NOBITS section has only its size
        sections.append((name, typ, flags, content, 0, 0, 8, 0))
    for i, (_name, _code, relocs) in enumerate(functions):
        body = b"".join(struct.pack("<QQq", off, (index[target] << 32) | 4, addend) for off, target, addend in relocs)
        sections.append((f".rela{functions[i][0]}", rela_type, 0x40, body, 0, i + 1, 8, 24))
    symtab_index = len(sections) + 1
    sections = [(n, t, f, c, 0 if t not in (4, 9) else symtab_index, i, a, e) for n, t, f, c, _l, i, a, e in sections]
    sections.append((".symtab", 2, 0, b"".join(sym_entries), symtab_index + 1, 1, 8, 24))
    sections.append((".strtab", 3, 0, strtab, 0, 0, 1, 0))
    sections.append((".shstrtab", 3, 0, b"", 0, 0, 1, 0))
    body, headers = b"", [b"\0" * 64]
    shstr = b"\0"
    placed = []
    for name, typ, flags, content, link, info, align, entsize in sections:
        name_off = len(shstr)
        shstr += name.encode() + b"\0"
        if name == ".shstrtab":
            content = shstr
        placed.append((name_off, typ, flags, content, link, info, align, entsize))
    offset = 64
    for name_off, typ, flags, content, link, info, align, entsize in placed:
        headers.append(struct.pack("<IIQQQQIIQQ", name_off, typ, flags, 0, offset, len(content), link, info, align, entsize))
        if typ != 8:  # NOBITS: no bytes in the file
            body += content
            offset += len(content)
    shoff = offset
    header = bytearray(64)
    header[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<H", header, 0x12, machine)
    struct.pack_into("<Q", header, 0x28, shoff)
    struct.pack_into("<HHH", header, 0x3A, 64, len(headers), len(headers) - 1)
    return bytes(header) + body + b"".join(headers)


class SelectionTest(unittest.TestCase):
    def test_style_tag(self):
        self.assertTrue(cg.is_style("llmath: modernize-use-auto (style)"))
        self.assertTrue(cg.is_style("llmath: modernize-use-auto (style)  "))
        self.assertFalse(cg.is_style("llmath: (style) use-nullptr"))  # the tag ends the subject
        self.assertFalse(cg.is_style("CI: check the (style) commits"))
        self.assertFalse(cg.is_style("llmath: modernize-use-override"))
        self.assertFalse(cg.is_style("llmath: style fixes"))
        self.assertFalse(cg.is_style("llmath: a (stylesheet) change"))

    def test_split_keeps_order(self):
        entries = [("a", "x (style)"), ("b", "y"), ("c", "z (style)")]
        self.assertEqual(cg.select_commits(entries), ([("a", "x (style)"), ("c", "z (style)")], [("b", "y")]))
        self.assertEqual(cg.select_commits(entries, all_style=True), (entries, []))
        self.assertEqual(cg.select_commits([]), ([], []))

    def test_commits_of_range_and_list(self):
        with tempfile.TemporaryDirectory() as tmp:
            def run(*cmd):
                return subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@example.com", *cmd], cwd=tmp,
                                      capture_output=True, text=True, check=True).stdout.strip()
            run("init", "-q")
            shas = []
            for subject in ("one", "two (style)", "three"):
                (pathlib.Path(tmp) / "f").write_text(subject, encoding="utf-8")
                run("add", "f")
                run("commit", "-q", "-m", subject)
                shas.append(run("rev-parse", "HEAD"))
            self.assertEqual(cg.commits_of(f"{shas[0]}..{shas[2]}", cwd=tmp), [(shas[1], "two (style)"), (shas[2], "three")])
            # a list is ordered by history, whatever the order given, and a repeated commit counts once
            self.assertEqual([s for s, _ in cg.commits_of(f"{shas[2]} {shas[0]} {shas[2]}", cwd=tmp)], [shas[0], shas[2]])
            self.assertEqual(cg.commits_of(f"{shas[2]}..{shas[2]}", cwd=tmp), [])
            with self.assertRaises(RuntimeError):
                cg.commits_of("nosuchcommit", cwd=tmp)


class TreeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)

    def run_git(self, *cmd):
        return subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@example.com", *cmd], cwd=self.root,
                              capture_output=True, text=True, check=True).stdout.strip()

    def commit(self, files, message):
        for name, text in files.items():
            path = self.root / name
            if text is None:
                path.unlink()
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(text)
        self.run_git("add", "-A")
        self.run_git("commit", "-q", "-m", message)
        return self.run_git("rev-parse", "HEAD")

    def text(self, name):
        return (self.root / name).read_bytes()

    def test_neutralise_replaces_only_the_token(self):
        self.assertEqual(cg.neutralise(b"f(__LINE__, __LINE__); a__LINE__ __LINE__b __LINE__\r\n"),
                         b"f(0, 0); a__LINE__ __LINE__b 0\r\n")

    def test_goto_neutralises_and_restores_what_a_commit_changes(self):
        self.run_git("init", "-q")
        self.run_git("config", "core.autocrlf", "false")
        first = self.commit({"indra/a.h": b"int a = __LINE__;\n", "indra/b.h": b"int b = __LINE__;\n",
                             "other/c.h": b"int c = __LINE__;\n", "indra/gone.h": b"__LINE__\n"}, "one")
        second = self.commit({"indra/a.h": b"\nint a = __LINE__; // moved\n", "indra/gone.h": None,
                              "indra/new.h": b"int n = __LINE__;\n"}, "two (style)")
        tree = cg.Tree(self.root)
        tree.goto(first)
        self.assertEqual(self.text("indra/a.h"), b"int a = 0;\n")
        self.assertEqual(self.text("other/c.h"), b"int c = __LINE__;\n")  # outside indra/: not touched
        b_stat = (self.root / "indra" / "b.h").stat().st_mtime_ns
        tree.goto(second)
        self.assertEqual(self.text("indra/a.h"), b"\nint a = 0; // moved\n")
        self.assertEqual(self.text("indra/new.h"), b"int n = 0;\n")
        self.assertFalse((self.root / "indra" / "gone.h").exists())
        self.assertEqual((self.root / "indra" / "b.h").stat().st_mtime_ns, b_stat)  # unchanged file, untouched
        tree.goto(first)
        self.assertEqual(self.text("indra/a.h"), b"int a = 0;\n")
        self.assertEqual(self.text("indra/gone.h"), b"0\n")
        self.assertFalse((self.root / "indra" / "new.h").exists())
        tree.restore()
        self.assertEqual(self.run_git("rev-parse", "HEAD"), second)
        self.assertEqual(self.run_git("status", "--porcelain"), "")
        self.assertEqual(self.text("indra/a.h"), b"\nint a = __LINE__; // moved\n")


class FingerprintTest(unittest.TestCase):
    BASE = [(".text._Z3foov", b"\x55\x48\x89\xe5\xe8\0\0\0\0\xc3", [(5, "_Z3barv", -4)])]

    def prints(self, **kwargs):
        return cg.parse_elf(make_object(kwargs.get("functions", self.BASE), kwargs.get("symbols", ()),
                                        kwargs.get("debug", b""), kwargs.get("comment", b"GCC")))

    def test_same_object_same_fingerprint(self):
        self.assertEqual(self.prints(), self.prints())
        self.assertIn(".text._Z3foov", self.prints()["sections"])

    def test_debug_info_and_comment_are_ignored(self):
        a = self.prints(debug=b"line 10", comment=b"GCC 14")
        b = self.prints(debug=b"line 12 and more", comment=b"GCC 15")
        self.assertEqual(a, b)
        self.assertEqual(cg.compare({"o": a}, {"o": b}), {})

    def test_code_change_is_found_by_symbol(self):
        other = [(".text._Z3foov", b"\x55\x48\x89\xe5\xe8\0\0\0\0\x90\xc3", [(5, "_Z3barv", -4)])]
        diffs = cg.compare({"o": self.prints()}, {"o": self.prints(functions=other)})
        self.assertEqual(diffs, {"o": ["_Z3foov"]})

    def test_relocation_target_and_addend_matter(self):
        target = [(".text._Z3foov", self.BASE[0][1], [(5, "_Z3bazv", -4)])]
        addend = [(".text._Z3foov", self.BASE[0][1], [(5, "_Z3barv", 0)])]
        self.assertEqual(cg.compare({"o": self.prints()}, {"o": self.prints(functions=target)}),
                         {"o": ["_Z3barv", "_Z3bazv", "_Z3foov"]})  # the undefined symbols differ too
        self.assertEqual(cg.compare({"o": self.prints()}, {"o": self.prints(functions=addend)}), {"o": ["_Z3foov"]})

    def test_numbered_local_labels_are_resolved_to_their_place(self):
        def with_label(label):
            fn = [(".text._Z3foov", self.BASE[0][1], [(5, label, 0)]), (".rodata.str1.1", b"hi"+bytes(1), [])]
            return self.prints(functions=fn, symbols=[(label, 0, 0, 2)])
        self.assertEqual(with_label(".LC5"), with_label(".LC9"))  # same place, another number
        other = [(".text._Z3foov", self.BASE[0][1], [(5, ".LC5", 0)]), (".rodata.str1.1", b"hi"+bytes(1), [])]
        moved = self.prints(functions=other, symbols=[(".LC5", 0, 0, 1)])  # points into the code section instead
        self.assertNotEqual(with_label(".LC5"), moved)
        self.assertNotIn(".LC5", with_label(".LC5")["symbols"])

    def test_section_order_does_not_matter(self):
        two = self.BASE + [(".text._Z1gv", b"\xc3", [])]
        swapped = [two[1], two[0]]
        self.assertEqual(self.prints(functions=two), self.prints(functions=swapped))

    def test_added_and_removed_sections_and_symbols(self):
        two = self.BASE + [(".text._Z1gv", b"\xc3", [])]
        self.assertEqual(cg.compare({"o": self.prints()}, {"o": self.prints(functions=two)}), {"o": ["_Z1gv"]})
        self.assertEqual(cg.compare({"o": self.prints(functions=two)}, {"o": self.prints()}), {"o": ["_Z1gv"]})
        local = self.prints(symbols=[("_Z3foov", 1, 2, 1)])
        weak = self.prints(symbols=[("_Z3foov", 2, 2, 1)])
        self.assertEqual(cg.compare({"o": local}, {"o": weak}), {"o": ["_Z3foov"]})

    def test_objects_added_and_removed(self):
        a = {"x.o": self.prints()}
        self.assertEqual(cg.compare(a, {}), {"x.o": ["(object removed)"]})
        self.assertEqual(cg.compare({}, a), {"x.o": ["(object added)"]})

    def test_not_elf(self):
        with self.assertRaises(ValueError):
            cg.parse_elf(b"MZ\x90\0" + b"\0" * 100)

    def test_labels(self):
        self.assertEqual(cg.label(".text._ZN3Foo3barEv"), "_ZN3Foo3barEv")
        self.assertEqual(cg.label(".text.startup._GLOBAL__sub_I_x"), "_GLOBAL__sub_I_x")
        self.assertEqual(cg.label(".rodata.str1.1"), "str1.1")
        self.assertEqual(cg.label(".data.rel.ro._ZTV3Foo"), "_ZTV3Foo")
        self.assertEqual(cg.label(".init_array"), ".init_array")
        self.assertEqual(cg.label(".text._Z1fv#2"), "_Z1fv")


class RenderTest(unittest.TestCase):
    def test_identical_and_differs(self):
        same = {"sha": "a" * 40, "subject": "s (style)", "status": "identical", "objects": 3, "differences": {}}
        text = "\n".join(cg.render_commit(same))
        self.assertIn("**identical in the compiled configuration**", text)
        self.assertIn("aaaaaaaaaaa", text)
        bad = dict(same, status="differs", differences={"llfoo.cpp.o": ["_Z1fv", "_Z1gv"]})
        text = "\n".join(cg.render_commit(bad))
        self.assertIn("**differs** in 1 of 3 objects", text)
        self.assertIn("llfoo.cpp.o", text)
        self.assertTrue("_Z1fv" in text or "f()" in text)  # c++filt, when installed, demangles

    def test_many_differences_are_cut(self):
        diffs = {f"o{i}.o": [f"_Z{i}v"] for i in range(30)}
        bad = {"sha": "b" * 40, "subject": "s", "status": "differs", "objects": 30, "differences": diffs}
        text = "\n".join(cg.render_commit(bad))
        self.assertIn("and 10 more objects", text)

    def test_build_failure(self):
        failed = {"sha": "c" * 40, "subject": "s", "status": "build-failed", "note": "the build failed"}
        self.assertIn("**build-failed** the build failed", "\n".join(cg.render_commit(failed)))


class ElfRulesTest(unittest.TestCase):
    CODE = [(".text._Z1fv", b"\x90\xc3", [])]

    def prints(self, **kwargs):
        return cg.parse_elf(make_object(kwargs.pop("functions", self.CODE), **kwargs))

    def differs(self, a, b):
        return cg.compare({"o": a}, {"o": b}) != {}

    def test_lto_objects_machines_and_rel_are_refused(self):
        with self.assertRaisesRegex(ValueError, "LTO"):
            self.prints(extra=[(".gnu.lto_foo.1234", 1, 0, b"gimple")])
        with self.assertRaisesRegex(ValueError, "machine"):
            self.prints(machine=3)
        with self.assertRaisesRegex(ValueError, "SHT_REL"):
            self.prints(functions=[(".text._Z1fv", b"\x90\xe8\0\0\0\0", [(2, "_Z1gv", 0)])], rela_type=9)
        self.assertEqual(self.prints(machine=183)["text"], 2)  # aarch64 is read

    def test_an_object_set_without_code_fails(self):
        data_only = self.prints(functions=[], extra=[(".data.x", 1, 3, b"\1\0\0\0")])
        self.assertEqual(data_only["text"], 0)
        with self.assertRaisesRegex(RuntimeError, "no .text"):
            cg.check_has_code({"a.o": data_only})
        cg.check_has_code({"a.o": data_only, "b.o": self.prints()})  # one object without code is fine

    def test_every_kind_of_section_counts(self):
        for name, typ in ((".gcc_except_table._Z1fv", 1), (".init_array", 14), (".rodata._Z1fv.str1.1", 1)):
            a = self.prints(extra=[(name, typ, 3, b"\1\2\3\4")])
            b = self.prints(extra=[(name, typ, 3, b"\1\2\3\5")])
            self.assertTrue(self.differs(a, b), name)
            self.assertFalse(self.differs(a, self.prints(extra=[(name, typ, 3, b"\1\2\3\4")])), name)

    def test_flags_alone_nobits_size_and_duplicate_names(self):
        a = self.prints(extra=[(".data.x", 1, 3, b"\1\2\3\4")])
        self.assertTrue(self.differs(a, self.prints(extra=[(".data.x", 1, 2, b"\1\2\3\4")])))  # flags only
        bss4 = self.prints(extra=[(".bss.x", 8, 3, bytes(4))])
        bss8 = self.prints(extra=[(".bss.x", 8, 3, bytes(8))])
        self.assertTrue(self.differs(bss4, bss8))  # a NOBITS section has a size and no bytes
        twice = self.prints(extra=[(".data.x", 1, 3, b"\1"), (".data.x", 1, 3, b"\2")])
        twice_changed = self.prints(extra=[(".data.x", 1, 3, b"\1"), (".data.x", 1, 3, b"\3")])
        self.assertTrue(self.differs(twice, twice_changed))  # the second of two equal names is not lost


class LinkAndDepfileTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name).resolve()
        self.build = self.root / "build"

    def target(self, name, link, objects=()):
        folder = self.build / "lib" / "CMakeFiles" / f"{name}.dir"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "link.txt").write_text(link, encoding="utf-8")
        for obj in objects:
            (self.build / "lib" / obj).parent.mkdir(parents=True, exist_ok=True)
            (self.build / "lib" / obj).write_bytes(b"obj")
        return folder

    def test_link_line_objects_and_response_files(self):
        self.target("x", "ar qc libx.a CMakeFiles/x.dir/a.cpp.o @objs.rsp\n", ["CMakeFiles/x.dir/a.cpp.o",
                                                                                "CMakeFiles/x.dir/b.cpp.o"])
        (self.build / "lib" / "objs.rsp").write_text("CMakeFiles/x.dir/b.cpp.o", encoding="utf-8")
        found = cg.target_objects(self.build, "x")
        self.assertEqual(sorted(found), ["CMakeFiles/x.dir/a.cpp.o", "CMakeFiles/x.dir/b.cpp.o"])

    def test_no_object_or_a_missing_one_fails(self):
        self.target("y", "ar qc liby.a\n")
        with self.assertRaisesRegex(RuntimeError, "names no object"):
            cg.target_objects(self.build, "y")
        self.target("z", "ar qc libz.a CMakeFiles/z.dir/gone.cpp.o\n")
        with self.assertRaisesRegex(RuntimeError, "not built"):
            cg.target_objects(self.build, "z")
        with self.assertRaisesRegex(RuntimeError, "names no object"):
            cg.target_objects(self.build, "nosuchtarget")

    def test_parse_depfile(self):
        text = "CMakeFiles/x.dir/a.cpp.o: /r/a.cpp \\\n /r/with\\ space.h \\\n /usr/include/stdio.h\n"
        self.assertEqual(cg.parse_depfile(text), ["/r/a.cpp", "/r/with space.h", "/usr/include/stdio.h"])

    def test_covered_files_are_the_union_inside_the_repository(self):
        (self.root / "indra" / "lib").mkdir(parents=True)
        folder = self.target("x", "")
        (folder / "a.cpp.o.d").write_text(f"a.o: {self.root}/indra/lib/a.cpp ../../indra/lib/a.h /usr/include/x.h\n")
        (folder / "b.cpp.o.d").write_text(f"b.o: {self.root}/indra/lib/b.cpp\n")
        # the second line of the first test is relative to the directory that holds CMakeFiles (build/lib)
        covered = cg.covered_files(self.build, ["x"], self.root)
        self.assertEqual(covered, {"indra/lib/a.cpp", "indra/lib/a.h", "indra/lib/b.cpp"})
        with self.assertRaisesRegex(RuntimeError, "no depfile"):
            cg.covered_files(self.build, ["other"], self.root)


class CoverageRulesTest(unittest.TestCase):
    def test_conditional_chains_and_the_include_guard(self):
        text = "\n".join(["#ifndef GUARD_H", "#define GUARD_H", "int a;", "#ifdef _WIN32", "int win;", "#else",
                          "int other;", "#endif", "#if LL_DEBUG && X", "int dbg;", "#endif", "int b;", "#endif"])
        chains = cg.conditional_chains(text)
        self.assertNotIn(3, chains)    # inside the guard only
        self.assertNotIn(12, chains)
        self.assertEqual(chains[5], ["4: #ifdef _WIN32"])
        self.assertIn("#else", chains[7][0])
        self.assertEqual(chains[10], ["9: #if LL_DEBUG && X"])
        self.assertIn(4, chains)       # the directive that opens a conditional belongs to it
        self.assertIn(8, chains)       # and so does its #endif
        self.assertNotIn(2, chains)

    def test_a_conditional_around_a_file_that_is_not_a_guard_counts(self):
        chains = cg.conditional_chains("#if LL_WINDOWS\nint a;\n#endif\n")
        self.assertEqual(chains[2], ["1: #if LL_WINDOWS"])
        self.assertEqual(cg.conditional_chains("#pragma once\nint a;\n"), {})

    def test_changed_line_numbers(self):
        diff = "diff --git a/f b/f\n@@ -3,2 +3,3 @@\n-a\n-b\n+c\n+d\n+e\n@@ -10 +11,0 @@\n-x\n@@ -0,0 +1,2 @@\n"
        self.assertEqual(cg.changed_line_numbers(diff), ([3, 4, 10], [3, 4, 5, 1, 2]))

    def test_verdict_order(self):
        self.assertEqual(cg.verdict({"o": ["a"]}, ["x"], ["y"]), "differs")
        self.assertEqual(cg.verdict({}, ["x"], ["y"]), "unproven")
        self.assertEqual(cg.verdict({}, [], ["y"]), "not-covered")
        self.assertEqual(cg.verdict({}, [], []), "identical")


class AssessTest(unittest.TestCase):
    setUp, run_git, commit, text = TreeTest.setUp, TreeTest.run_git, TreeTest.commit, TreeTest.text

    def test_changes_in_inactive_code_and_outside_the_objects(self):
        self.run_git("init", "-q")
        self.run_git("config", "core.autocrlf", "false")
        first = self.commit({"indra/lib/a.cpp": b"int a() { return 0; }\n#ifndef LL_RELEASE_FOR_DOWNLOAD\nint dbg = 0;\n#endif\n",
                             "indra/lib/win.cpp": b"int w = 0;\n", "indra/lib/tests/t.cpp": b"int t = 0;\n",
                             "indra/lib/h.h": b"#pragma once\nint h = 0;\n", "doc.md": b"x\n"}, "one")
        second = self.commit({"indra/lib/a.cpp": b"int a() { return nullptr == 0; }\n#ifndef LL_RELEASE_FOR_DOWNLOAD\nint dbg = nullptr;\n#endif\n",
                              "indra/lib/win.cpp": b"int w = 1;\n", "indra/lib/tests/t.cpp": b"int t = 1;\n",
                              "indra/lib/h.h": b"#pragma once\nint h = 1;\n", "doc.md": b"y\n"}, "two (style)")
        covered = {"indra/lib/a.cpp", "indra/lib/h.h"}
        unproven, not_covered, headers = cg.assess(first, second, covered, cwd=self.root)
        self.assertEqual(len(unproven), 2)  # the old and the new text of the changed line in the conditional
        self.assertTrue(all(item.startswith("indra/lib/a.cpp:3") for item in unproven))
        self.assertIn("#ifndef LL_RELEASE_FOR_DOWNLOAD", unproven[0])
        self.assertEqual(not_covered, ["indra/lib/tests/t.cpp", "indra/lib/win.cpp"])  # doc.md is no code
        self.assertEqual(headers, ["indra/lib/h.h"])


class CheckCommandTest(unittest.TestCase):
    """cmd_check with the git, build and comparison steps replaced: the state reuse and the failure paths."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.out = pathlib.Path(self.tmp.name)
        self.built = []
        self.fail_on = set()
        self.boom_on = set()
        self.differ_on = set()
        parents = {"B": "A", "C": "B", "D": "X"}

        class FakeTree:
            root = pathlib.Path(".")

            def restore(inner):
                self.restored = True

        def fake_git(*command, cwd=None, check=True):
            return parents[command[-1][:-1]]

        def fake_state(tree, targets, build_dir, sha, out, cache):
            self.built.append(sha)
            if sha in self.boom_on:
                raise RuntimeError("the link line names no object")
            if sha in self.fail_on:
                return None, None, f"the build at {sha} failed"
            fp = {"o": {"sections": {".text.f": sha if sha in self.differ_on else "same"}, "symbols": {}}}
            return fp, {"indra/a.cpp"}, ""

        patches = [mock.patch.object(cg, "Tree", FakeTree), mock.patch.object(cg, "git", fake_git),
                   mock.patch.object(cg, "state_of", fake_state),
                   mock.patch.object(cg, "assess", lambda *a, **k: ([], [], []))]
        for item in patches:
            item.start()
            self.addCleanup(item.stop)
        self.restored = False

    def run_check(self, shas):
        selection = self.out / "selection.json"
        selection.write_text(json.dumps({"checked": [[s, f"{s} (style)"] for s in shas], "skipped": []}), encoding="utf-8")
        args = argparse.Namespace(out=str(self.out), selection=str(selection), lib="lib", dir="b", extra_target=[])
        with contextlib.redirect_stdout(io.StringIO()):
            code = cg.cmd_check(args)
        return code, json.loads((self.out / "codegen.json").read_text(encoding="utf-8"))["results"]

    def test_state_is_reused_between_neighbouring_commits(self):
        code, results = self.run_check(["B", "C"])
        self.assertEqual(self.built, ["A", "B", "C"])  # A once, B once
        self.assertEqual([r["status"] for r in results], ["identical", "identical"])
        self.assertEqual(code, 0)
        self.assertTrue(self.restored)

    def test_a_difference_is_red_and_a_gap_rebuilds_the_parent(self):
        self.differ_on = {"C"}
        code, results = self.run_check(["B", "D"])
        self.assertEqual(self.built, ["A", "B", "X", "D"])
        self.assertEqual(code, 0)
        code, results = self.run_check(["C"])
        self.assertEqual([r["status"] for r in results], ["differs"])
        self.assertEqual(code, 1)

    def test_build_failures_at_the_parent_and_at_the_commit(self):
        self.fail_on = {"A"}
        code, results = self.run_check(["B", "C"])
        self.assertEqual([r["status"] for r in results], ["build-failed", "identical"])  # C starts from a fresh B
        self.assertEqual(code, 1)
        self.fail_on, self.built = {"C"}, []
        code, results = self.run_check(["B", "C"])
        self.assertEqual([r["status"] for r in results], ["identical", "build-failed"])
        self.assertEqual(code, 1)

    def test_an_exception_is_a_result_not_a_crash(self):
        self.boom_on = {"C"}
        code, results = self.run_check(["B", "C"])
        self.assertEqual([r["status"] for r in results], ["identical", "error"])
        self.assertIn("names no object", results[1]["note"])
        self.assertEqual(code, 1)
        self.boom_on = {"B"}
        code, results = self.run_check(["B", "C"])
        self.assertEqual([r["status"] for r in results], ["error", "error"])  # the next commit still ran


if __name__ == "__main__":
    unittest.main()
