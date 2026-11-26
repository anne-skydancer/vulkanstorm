# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Tests of scripts/ci_codegen.py: the commit selection, the ELF fingerprint and the comparison. No compiler is used:
the objects are built byte by byte."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import ci_codegen as cg  # noqa: E402


def make_object(functions, symbols=(), debug=b"", comment=b"GCC"):
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
    for i, (_name, _code, relocs) in enumerate(functions):
        body = b"".join(struct.pack("<QQq", off, (index[target] << 32) | 4, addend) for off, target, addend in relocs)
        sections.append((f".rela{functions[i][0]}", 4, 0x40, body, 0, i + 1, 8, 24))
    symtab_index = len(sections) + 1
    sections = [(n, t, f, c, 0 if t != 4 else symtab_index, i, a, e) for n, t, f, c, _l, i, a, e in sections]
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
        body += content
        headers.append(struct.pack("<IIQQQQIIQQ", name_off, typ, flags, 0, offset, len(content), link, info, align, entsize))
        offset += len(content)
    shoff = offset
    header = bytearray(64)
    header[:6] = b"\x7fELF\x02\x01"
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
        self.assertIn("**identical**", text)
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


if __name__ == "__main__":
    unittest.main()
