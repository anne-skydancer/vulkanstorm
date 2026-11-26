#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Identical compiled code for the style commits of a cleanup pull request (doc/clean/analysis.md, "Identical code").

doc/clean/plan.md splits each subsystem's cleanup into a bugs-and-safety pull request and a style pull request. A style
commit (its subject ends with the tag "(style)") must not change what the compiler emits. This script proves it:

  select   which commits of a range are style commits (checked) and which are not (skipped)
  check    build the library at the parent of each style commit and at the commit, and compare the object files

The comparison reads the ELF relocatable objects directly (no objdump): per section, the type, flags, alignment, size,
bytes and relocations (offset, type, target symbol name, addend), and per symbol its binding, type, visibility, section
and size. It ignores debug info, notes, the comment section, the unwind tables (derived from the code), the symbol
table's order and the build path. The build (analysis-setup mode "codegen") uses -ffunction-sections -fdata-sections so
that every function and variable is its own section with a stable name and position, -ffile-prefix-map so that __FILE__
carries no build path and a fixed -frandom-seed. A style edit that adds or removes a line shifts every __LINE__ baked
into the logging and assert call sites, so the check-only tree has the token __LINE__ replaced by 0 in the project's own
files (indra/), at each commit, before the build; the commit itself is not changed.
"""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shlex
import shutil
import struct
import subprocess
import sys

STYLE_TAG = re.compile(r"\(style\)\s*$")
SHT_NULL, SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_NOBITS, SHT_REL, SHT_GROUP, SHT_SYMTAB_SHNDX = 0, 2, 3, 4, 8, 9, 17, 18
SKIP_TYPES = {SHT_NULL, SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_REL, SHT_GROUP, SHT_SYMTAB_SHNDX}
SKIP_PREFIXES = (".debug", ".note", ".comment", ".eh_frame", ".rela", ".rel.", ".llvm", ".stab", ".gnu_debuglink",
                 ".gnu.lto")
LINE_TOKEN = re.compile(rb"\b__LINE__\b")
STT_SECTION, STT_FILE = 3, 4
SPECIAL_SECTIONS = {0: "UND", 0xFFF1: "ABS", 0xFFF2: "COM"}
LABEL = re.compile(r"^\.(?:text(?:\.(?:unlikely|startup|hot|exit))?|data(?:\.rel(?:\.ro)?(?:\.local)?)?|rodata|bss|tbss|"
                   r"tdata|gcc_except_table|init_array|fini_array)\.(.+)$")
MAX_LABELS = 20


# ---------------------------------------------------------------- selection

def is_style(subject):
    """A style commit ends its subject line with the tag "(style)"."""
    return bool(STYLE_TAG.search(subject))


def select_commits(entries, all_style=False):
    """Split [(sha, subject)], oldest first, into (checked, skipped); all_style checks every commit."""
    checked, skipped = [], []
    for sha, subject in entries:
        (checked if all_style or is_style(subject) else skipped).append((sha, subject))
    return checked, skipped


def git(*command, cwd=None, check=True):
    done = subprocess.run(["git", *command], cwd=cwd, capture_output=True, text=True, check=False)
    if check and done.returncode != 0:
        raise RuntimeError(f"git {' '.join(command)} failed: {done.stderr.strip()}")
    return done.stdout.strip()


def commits_of(spec, cwd=None):
    """[(sha, subject)], oldest first: spec is a range A..B (the commits B has and A has not, merges left out) or
    a whitespace separated list of commits, ordered by their history."""
    spec = spec.strip()
    if ".." in spec:
        shas = git("rev-list", "--reverse", "--no-merges", spec, cwd=cwd).split()
    else:
        wanted = [git("rev-parse", "--verify", f"{name}^{{commit}}", cwd=cwd) for name in spec.split()]
        shas = sorted(dict.fromkeys(wanted), key=lambda sha: int(git("rev-list", "--count", sha, cwd=cwd)))
    return [(sha, git("log", "-1", "--format=%s", sha, cwd=cwd)) for sha in shas]


def cmd_select(args):
    entries = commits_of(args.spec) if args.spec.strip() else []
    checked, skipped = select_commits(entries, args.all_style)
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "selection.json").write_text(json.dumps({"checked": checked, "skipped": skipped}, indent=1), encoding="utf-8")
    lines = ["### Identical compiled code: commits", "",
             f"{len(checked)} checked, {len(skipped)} skipped"
             + (" (every commit of the range is checked)" if args.all_style else " (subject without `(style)`)"), ""]
    lines += [f"- checked `{sha[:11]}` {subject}" for sha, subject in checked]
    lines += [f"- skipped `{sha[:11]}` {subject}" for sha, subject in skipped]
    summary("\n".join(lines))
    write_output("count", str(len(checked)))
    return 0


# ---------------------------------------------------------------- the ELF fingerprint

def cstring(blob, offset):
    end = blob.find(b"\0", offset)
    return blob[offset:end if end >= 0 else len(blob)].decode("utf-8", "replace")


def digest(*parts):
    h = hashlib.sha256()
    for part in parts:
        h.update(part if isinstance(part, bytes) else str(part).encode())
        h.update(b"\x1f")
    return h.hexdigest()[:20]


def parse_elf(data):
    """{"sections": {name: digest}, "symbols": {name: digest}} of an ELF64 little-endian relocatable object."""
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("not an ELF64 little-endian file")
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    if shoff == 0 or shnum == 0:
        raise ValueError("no section header table")
    raw = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize) for i in range(shnum)]
    names = data[raw[shstrndx][4]:raw[shstrndx][4] + raw[shstrndx][5]]
    sections = [{"name": cstring(names, r[0]), "type": r[1], "flags": r[2], "offset": r[4], "size": r[5], "link": r[6],
                 "info": r[7], "align": r[8], "entsize": r[9]} for r in raw]

    def content(section):
        return b"" if section["type"] == SHT_NOBITS else data[section["offset"]:section["offset"] + section["size"]]

    symbols = []  # (name, bind, type, visibility, shndx, value, size)
    for section in sections:
        if section["type"] != SHT_SYMTAB:
            continue
        strtab = content(sections[section["link"]])
        body = content(section)
        for off in range(24, len(body) - 23, 24):  # entry 0 is the null symbol
            st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from("<IBBHQQ", body, off)
            name = cstring(strtab, st_name)
            kind = st_info & 0xF
            if kind == STT_SECTION and st_shndx < len(sections):
                name = "section:" + sections[st_shndx]["name"]
            elif name.startswith(".L") and 0 < st_shndx < len(sections):
                # the compiler's numbered local label (.LC27 for a literal): its number shifts when an earlier literal
                # goes away, so a relocation names where it points (section and offset) instead
                name = f"local:{sections[st_shndx]['name']}+{st_value}"
            symbols.append((name, st_info >> 4, kind, st_other & 3, st_shndx, st_value, st_size))
        break
    target_name = {i + 1: s[0] for i, s in enumerate(symbols)}  # symbol index -> the name a relocation uses

    relocs = {}
    for section in sections:
        if section["type"] != SHT_RELA:
            continue
        body = content(section)
        entries = []
        for off in range(0, len(body) - 23, 24):
            r_offset, r_info, r_addend = struct.unpack_from("<QQq", body, off)
            entries.append(f"{r_offset}:{r_info & 0xFFFFFFFF}:{target_name.get(r_info >> 32, '')}:{r_addend}")
        relocs.setdefault(section["info"], []).extend(entries)

    out_sections, seen = {}, {}
    for index, section in enumerate(sections):
        name = section["name"]
        if section["type"] in SKIP_TYPES or name.startswith(SKIP_PREFIXES):
            continue
        seen[name] = seen.get(name, 0) + 1
        key = name if seen[name] == 1 else f"{name}#{seen[name]}"
        out_sections[key] = digest(section["type"], section["flags"], section["align"], section["entsize"],
                                   section["size"], content(section), ";".join(relocs.get(index, [])))

    grouped = {}
    for name, bind, kind, vis, shndx, value, size in symbols:
        if kind in (STT_SECTION, STT_FILE) or not name or name.startswith("local:"):
            continue
        where = SPECIAL_SECTIONS.get(shndx) or (sections[shndx]["name"] if shndx < len(sections) else str(shndx))
        grouped.setdefault(name, []).append(f"{bind}:{kind}:{vis}:{where}:{value}:{size}")
    return {"sections": out_sections, "symbols": {n: digest(*sorted(items)) for n, items in grouped.items()}}


# ---------------------------------------------------------------- the comparison

def label(name):
    """The symbol a section name stands for (.text._ZN3Foo3barEv -> _ZN3Foo3barEv)."""
    name = name.split("#", 1)[0]
    match = LABEL.match(name)
    return match.group(1) if match else name


def compare(prev, cur):
    """{object: [label, ...]} of what differs between two {object: fingerprint} maps; empty when identical."""
    diffs = {}
    for obj in sorted(set(prev) | set(cur)):
        if obj not in prev:
            diffs[obj] = ["(object added)"]
            continue
        if obj not in cur:
            diffs[obj] = ["(object removed)"]
            continue
        names = set()
        for part in ("sections", "symbols"):
            a, b = prev[obj][part], cur[obj][part]
            names.update(label(n) for n in set(a) | set(b) if a.get(n) != b.get(n))
        if names:
            diffs[obj] = sorted(names)
    return diffs


def demangle(names):
    """Readable names through c++filt when it exists; the mangled names otherwise."""
    tool = shutil.which("c++filt")
    if not tool or not names:
        return {n: n for n in names}
    done = subprocess.run([tool], input="\n".join(names) + "\n", capture_output=True, text=True, check=False)
    lines = done.stdout.splitlines()
    return dict(zip(names, lines)) if len(lines) == len(names) else {n: n for n in names}


def render_commit(result):
    """The markdown lines of one commit's result."""
    sha, subject, status = result["sha"][:11], result["subject"], result["status"]
    if status == "identical":
        return [f"- `{sha}` {subject}: **identical** ({result['objects']} objects)"]
    if status == "differs":
        lines = [f"- `{sha}` {subject}: **differs** in {len(result['differences'])} of {result['objects']} objects"]
        shown_items = list(result["differences"].items())[:MAX_LABELS]
        shown = demangle(sorted({n for _, names in shown_items for n in names[:MAX_LABELS]}))
        for obj, names in shown_items:
            listed = ", ".join(f"`{shown.get(n, n)}`" for n in names[:MAX_LABELS])
            more = f" and {len(names) - MAX_LABELS} more" if len(names) > MAX_LABELS else ""
            lines.append(f"  - `{obj}`: {listed}{more}")
        if len(result["differences"]) > MAX_LABELS:
            lines.append(f"  - and {len(result['differences']) - MAX_LABELS} more objects (codegen.json has them all)")
        return lines
    return [f"- `{sha}` {subject}: **{status}** {result.get('note', '')}".rstrip()]


# ---------------------------------------------------------------- the build

def summary(text):
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    print(text)
    if path:
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(text + "\n")


def write_output(key, value):
    path = os.environ.get("GITHUB_OUTPUT")
    if path:
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(f"{key}={value}\n")
    print(f"{key}={value}")


def target_objects(build_dir, lib):
    """The object files the library target links: {name relative to the target's build directory: path}."""
    build_dir = pathlib.Path(build_dir)
    found = {}
    for link in sorted(build_dir.glob(f"**/CMakeFiles/{lib}.dir/link.txt")):
        base = link.parents[2]
        for token in shlex.split(link.read_text(encoding="utf-8", errors="replace")):
            if token.endswith(".o"):
                path = (base / token).resolve()
                if path.is_file():
                    found[pathlib.PurePosixPath(token).as_posix()] = path
    if not found:  # a link line that names no objects: every object the target compiled
        for path in sorted(build_dir.glob(f"**/CMakeFiles/{lib}.dir/**/*.o")):
            found[path.relative_to(build_dir).as_posix()] = path
    return found


def fingerprints(build_dir, lib, cache):
    """{object: fingerprint} of the library's objects; cache maps a file's sha256 to its fingerprint."""
    result = {}
    for name, path in target_objects(build_dir, lib).items():
        data = path.read_bytes()
        key = hashlib.sha256(data).hexdigest()
        if key not in cache:
            cache[key] = parse_elf(data)
        result[name] = cache[key]
    return result


def neutralise(data):
    """The bytes of a source file with every __LINE__ token replaced by 0."""
    return LINE_TOKEN.sub(b"0", data)


class Tree:
    """The working tree, moved between commits with the project's __LINE__ tokens replaced by 0 (check-only).

    Only the files a commit changes are restored and rewritten, so the build's timestamps stay valid for the rest."""

    def __init__(self, root="."):
        self.root = pathlib.Path(root)
        self.current = self.original = self.git("rev-parse", "HEAD")
        self.neutralised = set()

    def git(self, *command, check=True):
        return git(*command, cwd=self.root, check=check)

    def goto(self, sha):
        if sha != self.current:
            moved = sorted(set(self.git("diff", "--no-renames", "--name-only", self.current, sha).splitlines())
                           & self.neutralised)
            for start in range(0, len(moved), 200):
                self.git("checkout", "--quiet", "--", *moved[start:start + 200])
            self.neutralised -= set(moved)
            self.git("checkout", "--detach", "--quiet", sha)
            self.current = sha
        for name in self.git("grep", "-lw", "__LINE__", "--", "indra", check=False).splitlines():
            path = self.root / name
            if path.is_file():
                path.write_bytes(neutralise(path.read_bytes()))
                self.neutralised.add(name)

    def restore(self):
        self.git("checkout", "--force", "--detach", "--quiet", self.original, check=False)


def build(build_dir, lib, log_path):
    with open(log_path, "w", encoding="utf-8") as log:
        done = subprocess.run(["cmake", "--build", str(build_dir), "--target", lib, "--parallel", str(os.cpu_count() or 2)],
                              stdout=log, stderr=subprocess.STDOUT, check=False)
    return done.returncode == 0


def state_of(tree, build_dir, lib, sha, out, cache):
    """Check the commit out, build it and fingerprint the objects: (fingerprints or None, note)."""
    tree.goto(sha)
    log = pathlib.Path(out) / f"build-{sha[:11]}.log"
    if not build(build_dir, lib, log):
        print("\n".join(log.read_text(encoding="utf-8", errors="replace").splitlines()[-15:]))
        return None, f"the build at {sha[:11]} failed (see {log.name})"
    prints = fingerprints(build_dir, lib, cache)
    if not prints:
        return None, f"no object file of {lib} at {sha[:11]}"
    return prints, ""


def cmd_check(args):
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    selection = json.loads(pathlib.Path(args.selection).read_text(encoding="utf-8"))
    tree = Tree()
    cache, state, results = {}, ("", None), []
    try:
        for sha, subject in selection["checked"]:
            result = {"sha": sha, "subject": subject, "status": "", "objects": 0, "differences": {}}
            results.append(result)
            parent = git("rev-parse", f"{sha}^")
            if state[0] != parent:
                prints, note = state_of(tree, args.dir, args.lib, parent, out, cache)
                if prints is None:
                    state = ("", None)
                    result.update(status="build-failed", note=note)
                    continue
                state = (parent, prints)
            prints, note = state_of(tree, args.dir, args.lib, sha, out, cache)
            if prints is None:
                state = ("", None)
                result.update(status="build-failed", note=note)
                continue
            diffs = compare(state[1], prints)
            result.update(status="differs" if diffs else "identical", objects=len(prints), differences=diffs)
            state = (sha, prints)
    finally:
        tree.restore()
    lines = ["### Identical compiled code: result", ""]
    for result in results:
        lines += render_commit(result)
    bad = [r for r in results if r["status"] != "identical"]
    lines += ["", (f"{len(bad)} of {len(results)} checked commits are not proven identical: move them to the bugs "
                   "and safety pull request") if bad else f"All {len(results)} checked commits compile identically."]
    summary("\n".join(lines))
    (out / "codegen.json").write_text(json.dumps({"results": results, "skipped": selection["skipped"]}, indent=1),
                                      encoding="utf-8")
    (out / "codegen.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 1 if bad else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("select")
    p.add_argument("--spec", default="", help="a range A..B, or a list of commits")
    p.add_argument("--all-style", action="store_true", help="check every commit, whatever its subject")
    p.add_argument("--out", required=True)
    p.set_defaults(func=cmd_select)
    p = sub.add_parser("check")
    p.add_argument("--lib", required=True)
    p.add_argument("--dir", required=True, help="the CMake build directory, configured in mode codegen")
    p.add_argument("--selection", required=True)
    p.add_argument("--out", required=True)
    p.set_defaults(func=cmd_check)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
