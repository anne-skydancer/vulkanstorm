#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Identical compiled code for the style commits of a cleanup pull request (doc/clean/analysis.md, "Identical compiled code").

doc/clean/plan.md splits each subsystem's cleanup into a bugs-and-safety pull request and a style pull request. A style
commit (its subject ends with the tag "(style)") must not change what the compiler emits. This script proves it:

  select   which commits of a range are style commits (checked) and which are not (skipped)
  check    build the library at the parent of each style commit and at the commit, and compare the object files

The comparison reads the ELF relocatable objects directly (no objdump): per section, the type, flags, alignment, entry size,
size, bytes and relocations (offset, type, target symbol name, addend), and per symbol its binding, type, visibility, section
and size. It ignores debug info, notes, the comment section, the unwind tables (derived from the code), the symbol
table's order and the build path. The build (analysis-setup mode "codegen") uses -ffunction-sections -fdata-sections so
that every function and variable is its own section with a stable name and position, -ffile-prefix-map so that __FILE__
carries no build path and a fixed -frandom-seed. A style edit that adds or removes a line shifts every __LINE__ baked
into the logging and assert call sites, so the check-only tree has the token __LINE__ replaced by 0 in the project's own
files (indra/), at each commit, before the build; the commit itself is not changed.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import pathlib
import re
import shlex
import shutil
import struct
import tempfile
import subprocess
import sys

STYLE_TAG = re.compile(r"\(style\)\s*$")
SHT_NULL, SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_NOBITS, SHT_REL, SHT_GROUP, SHT_SYMTAB_SHNDX = 0, 2, 3, 4, 8, 9, 17, 18
SKIP_TYPES = {SHT_NULL, SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_REL, SHT_GROUP, SHT_SYMTAB_SHNDX}
SKIP_PREFIXES = (".debug", ".note", ".comment", ".eh_frame", ".rela", ".rel.", ".llvm", ".stab", ".gnu_debuglink")
MACHINES = {62: "x86-64", 183: "aarch64"}
CODE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".inl", ".mm")
HEADER_SUFFIXES = (".h", ".hpp", ".inl")
HUNK = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")
CONFIGURATION = "Linux, GCC, LL_RELEASE_FOR_DOWNLOAD"
MAX_FINDINGS = 15
PROBE = "__codegen_probe_{}__"
PROBE_RE = re.compile(r"__codegen_probe_(\d+)__")
NOT_COMPILED = "not compiled by any translation unit of the target"
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
        shas = git("rev-list", "--reverse", "--no-merges", "--end-of-options", spec, cwd=cwd).split()
    else:
        wanted = [git("rev-parse", "--verify", "--end-of-options", f"{name}^{{commit}}", cwd=cwd) for name in spec.split()]
        shas = sorted(dict.fromkeys(wanted), key=lambda sha: int(git("rev-list", "--count", "--end-of-options", sha, cwd=cwd)))
    return [(sha, git("log", "-1", "--format=%s", "--end-of-options", sha, cwd=cwd)) for sha in shas]


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
    if not checked and skipped:
        print(f"::warning::none of the {len(skipped)} commits ends with (style): nothing is checked")
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
    machine, = struct.unpack_from("<H", data, 0x12)
    if machine not in MACHINES:
        raise ValueError(f"unsupported machine {machine}: only x86-64 and aarch64 objects are read")
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

    out_sections, seen, text_bytes = {}, {}, 0
    for index, section in enumerate(sections):
        name = section["name"]
        if name.startswith(".gnu.lto"):
            raise ValueError(f"{name}: an LTO object holds intermediate code, not machine code")
        if section["type"] == SHT_REL:
            raise ValueError(f"{name}: SHT_REL relocations (implicit addends) are not read")
        if (name == ".text" or name.startswith(".text.")) and section["type"] != SHT_NOBITS:
            text_bytes += section["size"]
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
    return {"sections": out_sections, "symbols": {n: digest(*sorted(items)) for n, items in grouped.items()},
            "text": text_bytes}


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
        lines = [f"- `{sha}` {subject}: **identical in the compiled configuration** ({CONFIGURATION}; "
                 f"{result['objects']} objects)"]
    elif status == "differs":
        lines = [f"- `{sha}` {subject}: **differs** in {len(result['differences'])} of {result['objects']} objects"]
        shown_items = list(result["differences"].items())[:MAX_LABELS]
        shown = demangle(sorted({n for _, names in shown_items for n in names[:MAX_LABELS]}))
        for obj, names in shown_items:
            listed = ", ".join(f"`{shown.get(n, n)}`" for n in names[:MAX_LABELS])
            more = f" and {len(names) - MAX_LABELS} more" if len(names) > MAX_LABELS else ""
            lines.append(f"  - `{obj}`: {listed}{more}")
        if len(result["differences"]) > MAX_LABELS:
            lines.append(f"  - and {len(result['differences']) - MAX_LABELS} more objects (codegen.json has them all)")
    else:
        lines = [f"- `{sha}` {subject}: **{status}** {result.get('note', '')}".rstrip()]
    for key, text in (("unproven", "changed lines no translation unit of this build compiles (an inactive branch, or a line that cannot "
                                   "be probed): the objects say nothing about them"),
                      ("not_covered", "changed code files the compiled objects do not depend on (not compared)")):
        items = result.get(key) or []
        if items:
            lines.append(f"  - **{key.replace('_', '-')}**: {len(items)} {text}")
            lines += [f"    - `{item}`" for item in items[:MAX_FINDINGS]]
            if len(items) > MAX_FINDINGS:
                lines.append(f"    - and {len(items) - MAX_FINDINGS} more (codegen.json has them all)")
    if result.get("headers"):
        lines.append(f"  - headers changed ({', '.join(result['headers'][:5])}): other libraries' objects are not compared")
    return lines


# ---------------------------------------------------------------- what the build does not compile

def changed_line_numbers(diff_text):
    """(old line numbers, new line numbers) of the lines a `git diff -U0` of one file changes."""
    old, new = [], []
    for line in diff_text.splitlines():
        match = HUNK.match(line)
        if match:
            a, b, c, d = match.groups()
            old += range(int(a), int(a) + int(1 if b is None else b))
            new += range(int(c), int(c) + int(1 if d is None else d))
    return old, new


def code_lines(text):
    """The line numbers (1-based) that hold anything besides blanks and comments. A conservative reading: strings
    and character literals are skipped, a comment that starts inside one is not seen."""
    result, in_block = set(), False
    for number, line in enumerate(text.split("\n"), 1):
        i, code = 0, False
        while i < len(line):
            if in_block:
                j = line.find("*/", i)
                in_block, i = (False, j + 2) if j >= 0 else (True, len(line))
            elif line.startswith("//", i):
                break
            elif line.startswith("/*", i):
                in_block, i = True, i + 2
            elif line[i] in "\"'":
                code, quote, i = True, line[i], i + 1
                while i < len(line) and line[i] != quote:
                    i += 2 if line[i] == "\\" else 1
                i += 1
            else:
                code = code or not line[i].isspace()
                i += 1
        if code:
            result.add(number)
    return result


def add_probes(text, wanted, first_id):
    """(text with a probe line before each wanted line, {id: line}, {line: reason}) for the lines that hold code.
    A line that continues the one before it (a multi-line macro or statement) or that is an #else, #elif or #endif
    takes no probe (it would change the macro, or sit in the wrong branch): its reason says so."""
    lines = text.split("\n")
    holds = code_lines(text)
    ids, refused = {}, {}
    for number in sorted(n for n in wanted if n in holds and n <= len(lines)):
        if number > 1 and lines[number - 2].rstrip("\r").endswith("\\"):
            refused[number] = "a continuation line of a multi-line macro or statement"
        elif re.match(r"\s*#\s*(else|elif|endif)\b", lines[number - 1]):
            refused[number] = "a conditional directive that closes or switches a branch"
        else:
            ids[first_id + len(ids)] = number
    for probe, number in sorted(ids.items(), key=lambda item: -item[1]):
        lines.insert(number - 1, PROBE.format(probe))
    return "\n".join(lines), ids, refused


def preprocessor_command(entry, root, scratch, build_rel, source):
    """The compile command of a compile_commands.json entry as a preprocessing run over the scratch copy: no output
    file or dependency file, -E, and every path under the repository moved to the scratch tree (the build
    directory, with its generated headers and packages, stays where it is)."""
    args = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
    out, skip = [], 0
    move = re.compile(re.escape(root) + r"/(?!" + re.escape(build_rel) + r"(?:/|$))")
    for arg in args:
        if skip:
            skip -= 1
        elif arg in ("-o", "-MT", "-MF", "-MQ"):
            skip = 1
        elif arg in ("-c", "-MD", "-MMD", "-MP"):
            continue
        elif arg == entry["file"] or os.path.normpath(arg) == os.path.normpath(entry["file"]):
            out.append(str(source))
        else:
            out.append(move.sub(scratch + "/", arg))
    return [*out, "-E", "-P", "-w"]


def run_preprocessor(command, cwd):
    done = subprocess.run(command, cwd=cwd, capture_output=True, text=True, check=False)
    if done.returncode != 0:
        raise RuntimeError(f"preprocessing failed ({' '.join(command[:1])}): {done.stderr.strip()[-300:]}")
    return done.stdout


def surviving_probes(side, rev_files, wanted, tus, commands, root, build_rel, cwd=None):
    """({probe id: (path, line)}, {(path, line): reason}) of one side. rev_files(path) is the text of a file on this
    side (None when it does not exist); wanted is {path: set of changed lines}; tus {source: set of repository
    files the translation unit read}; commands {source: compile_commands entry}. A probe is alive when its token is in
    the preprocessed output of at least one translation unit that reads the file."""
    needed = {src: deps for src, deps in tus.items() if deps & set(wanted)}
    probed, refused, ids, texts = {}, {}, {}, {}
    next_id = 0
    for path, lines in sorted(wanted.items()):
        text = rev_files(path)
        if text is None:
            continue
        probed_text, found, bad = add_probes(text, lines, next_id)
        probed[path] = probed_text
        next_id += len(found)
        ids.update({probe: (path, number) for probe, number in found.items()})
        refused.update({(path, number): reason for number, reason in bad.items()})
    alive = set()
    if not needed or not ids:
        return ids, refused, alive
    root_text = str(pathlib.Path(root).resolve()).replace("\\", "/")
    with tempfile.TemporaryDirectory(prefix="codegen-probe-") as scratch:
        scratch_text = pathlib.Path(scratch).resolve().as_posix()
        for path in sorted(set().union(*needed.values()) | set(needed)):
            target = pathlib.Path(scratch) / path
            target.parent.mkdir(parents=True, exist_ok=True)
            if path in probed:
                target.write_text(probed[path], encoding="utf-8", newline="\n")
            else:
                text = rev_files(path)
                if text is not None:
                    target.write_text(text, encoding="utf-8", newline="\n")

        def one(source):
            command = preprocessor_command(commands[source], root_text, scratch_text, build_rel,
                                           pathlib.Path(scratch) / source)
            return {int(n) for n in PROBE_RE.findall(run_preprocessor(command, commands[source]["directory"]))}

        with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
            for found in pool.map(one, sorted(needed)):
                alive |= found
    return ids, refused, alive


def probe_findings(parent, sha, files, old, new, root, build_rel, cwd=None):
    """["file:line (old|new text): reason"] for each changed line of the files that no translation unit of this build
    compiles: an inactive branch (Windows, macOS, debug-only, LL_RELEASE_FOR_DOWNLOAD), or a line that cannot be probed. A changed line
    that the preprocessor keeps in at least one translation unit is compiled, and the object comparison covers it."""
    root_path = pathlib.Path(root).resolve()
    build_rel = os.path.relpath(pathlib.Path(build_rel).resolve(), root_path).replace(chr(92), "/")
    wanted_old, wanted_new = {}, {}
    for path in files:
        diff = git("diff", "-U0", "--no-renames", parent, sha, "--", path, cwd=cwd, check=False)
        old_lines, new_lines = changed_line_numbers(diff)
        if old_lines:
            wanted_old[path] = set(old_lines)
        if new_lines:
            wanted_new[path] = set(new_lines)
    changed = set(git("diff", "--no-renames", "--name-only", parent, sha, cwd=cwd).splitlines())

    def at(rev, use_tree):
        def read(path):
            if use_tree and path not in changed and (root_path / path).is_file():
                return (root_path / path).read_bytes().decode("utf-8", "replace")
            done = subprocess.run(["git", "show", f"{rev}:{path}"], cwd=cwd, capture_output=True, check=False)
            return done.stdout.decode("utf-8", "replace") if done.returncode == 0 else None
        return read

    found = []
    for side, rev, wanted, info in (("old", parent, wanted_old, old), ("new", sha, wanted_new, new)):
        if not wanted:
            continue
        ids, refused, alive = surviving_probes(side, at(rev, True), wanted, info["tus"],
                                               {**old["commands"], **new["commands"]}, root, build_rel, cwd)
        for probe, (path, number) in sorted(ids.items()):
            if probe not in alive:
                found.append(f"{path}:{number} ({side} text): {NOT_COMPILED}")
        for (path, number), reason in sorted(refused.items()):
            found.append(f"{path}:{number} ({side} text): not probed, {reason}")
    return found


def parse_depfile(text):
    """The prerequisites of a compiler's -MD file (Makefile syntax: a backslash continues the line or escapes a space)."""
    text = text.replace("\\\r\n", " ").replace("\\\n", " ")
    _, _, deps = text.partition(": ")
    return [d.replace("\\ ", " ") for d in re.findall(r"(?:\\ |\S)+", deps)]


def translation_units(build_dir, targets, root):
    """{source file: the repository files its object was compiled from}, from the depfiles
    (CMakeFiles/<target>.dir/*.o.d; the first prerequisite is the source). Paths are relative to the repository and
    posix. Fails when there is no depfile: nothing would say what the build covers."""
    root = pathlib.Path(root).resolve()
    units, count = {}, 0
    for target in targets:
        for dep in sorted(pathlib.Path(build_dir).glob(f"**/CMakeFiles/{target}.dir/**/*.o.d")):
            count += 1
            base = next(parent.parent for parent in dep.parents if parent.name == "CMakeFiles")
            files = []
            for item in parse_depfile(dep.read_text(encoding="utf-8", errors="replace")):
                try:
                    files.append((base / item).resolve().relative_to(root).as_posix())
                except ValueError:
                    continue  # outside the repository: packages, the toolchain
            if files:
                units[files[0]] = set(files)
    if not count:
        raise RuntimeError(f"no depfile (CMakeFiles/<target>.dir/*.o.d) for {', '.join(targets)}: the build covers an unknown set")
    return units


def covered_files(build_dir, targets, root):
    """The union of the repository files of the targets' translation units."""
    return set().union(*translation_units(build_dir, targets, root).values())


def compile_commands(build_dir, root, sources):
    """{source: compile_commands.json entry} for the translation units; fails on a missing entry."""
    root = pathlib.Path(root).resolve()
    path = pathlib.Path(build_dir) / "compile_commands.json"
    if not path.is_file():
        raise RuntimeError(f"{path} is missing: the configure must export the compile commands")
    entries = {}
    for entry in json.loads(path.read_text(encoding="utf-8")):
        try:
            entries[pathlib.Path(entry["file"]).resolve().relative_to(root).as_posix()] = entry
        except ValueError:
            continue
    missing = sorted(source for source in sources if source not in entries)
    if missing:
        raise RuntimeError(f"no compile command for {', '.join(missing[:3])}")
    return {source: entries[source] for source in sources}


def assess(parent, sha, old, new, root, build_rel, cwd=None):
    """(changed lines no translation unit compiles, code files changed but not compiled, headers changed) of one
    commit. old and new are the {"tus", "commands"} of the parent's and the commit's build."""
    covered = set().union(*old["tus"].values(), *new["tus"].values())
    changed = git("diff", "--no-renames", "--name-only", parent, sha, cwd=cwd).splitlines()
    code = [name for name in changed if name.lower().endswith(CODE_SUFFIXES)]
    inside = [name for name in code if name in covered]
    not_covered = sorted(name for name in code if name not in covered)
    headers = sorted(name for name in inside if name.lower().endswith(HEADER_SUFFIXES))
    return probe_findings(parent, sha, inside, old, new, root, build_rel, cwd), not_covered, headers


def verdict(differences, unproven, not_covered):
    """differs, unproven (changed lines the build does not compile), not-covered (changed code the build does not compile), or
    identical, in this order of importance."""
    if differences:
        return "differs"
    if unproven:
        return "unproven"
    return "not-covered" if not_covered else "identical"


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


def link_tokens(link, base):
    """The words of a link.txt, with @response files read."""
    tokens = []
    for token in shlex.split(link.read_text(encoding="utf-8", errors="replace")):
        if token.startswith("@") and (base / token[1:]).is_file():
            tokens += shlex.split((base / token[1:]).read_text(encoding="utf-8", errors="replace"))
        else:
            tokens.append(token)
    return tokens


def target_objects(build_dir, target):
    """The object files the target links: {name relative to the target's build directory: path}. Fails when the link
    line names none (a glob of whatever is on disk could include stale objects)."""
    found = {}
    for link in sorted(pathlib.Path(build_dir).glob(f"**/CMakeFiles/{target}.dir/link.txt")):
        base = link.parents[2]
        for token in link_tokens(link, base):
            if token.endswith(".o"):
                path = (base / token).resolve()
                if not path.is_file():
                    raise RuntimeError(f"{target}: the link line names {token}, which is not built")
                found[pathlib.PurePosixPath(token).as_posix()] = path
    if not found:
        raise RuntimeError(f"{target}: the link line (CMakeFiles/{target}.dir/link.txt) names no object file")
    return found


def check_has_code(prints):
    """Fails when the objects hold no code at all: the build compiled nothing the comparison could say anything about."""
    if not any(fp.get("text") for fp in prints.values()):
        raise RuntimeError("the objects have no .text bytes at all")


def fingerprints(build_dir, targets, cache):
    """{object: fingerprint} of the targets' objects (keyed target:object); cache maps a file's sha256 to its fingerprint."""
    result = {}
    for target in targets:
        for name, path in target_objects(build_dir, target).items():
            data = path.read_bytes()
            key = hashlib.sha256(data).hexdigest()
            if key not in cache:
                cache[key] = parse_elf(data)
            result[f"{target}:{name}"] = cache[key]
    check_has_code(result)
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
        for name in self.git("grep", "-Ilw", "__LINE__", "--", "indra", check=False).splitlines():
            path = self.root / name
            if path.is_file():
                path.write_bytes(neutralise(path.read_bytes()))
                self.neutralised.add(name)

    def restore(self):
        self.git("checkout", "--force", "--detach", "--quiet", self.original, check=False)


def build(build_dir, targets, log_path):
    command = ["cmake", "--build", str(build_dir), "--parallel", str(os.cpu_count() or 2), "--target", *targets]
    with open(log_path, "w", encoding="utf-8") as log:
        done = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
    return done.returncode == 0


def state_of(tree, targets, build_dir, sha, out, cache):
    """Check the commit out, build it and read the objects: (fingerprints, {"tus", "commands"}, note); the first
    two are None when the build failed."""
    tree.goto(sha)
    log = pathlib.Path(out) / f"build-{sha[:11]}.log"
    if not build(build_dir, targets, log):
        print("\n".join(log.read_text(encoding="utf-8", errors="replace").splitlines()[-15:]))
        return None, None, f"the build at {sha[:11]} failed (see {log.name})"
    units = translation_units(build_dir, targets, tree.root)
    return (fingerprints(build_dir, targets, cache),
            {"tus": units, "commands": compile_commands(build_dir, tree.root, units)}, "")


def write_results(out, results, skipped, final=False):
    lines = ["### Identical compiled code: result", ""]
    for result in results:
        lines += render_commit(result)
    bad = [r for r in results if r["status"] != "identical"]
    lines += ["", (f"{len(bad)} of {len(results)} checked commits are not proven identical: move them to the bugs "
                   f"and safety pull request") if bad else
              f"All {len(results)} checked commits compile identically in the compiled configuration ({CONFIGURATION})."]
    (pathlib.Path(out) / "codegen.json").write_text(json.dumps({"results": results, "skipped": skipped}, indent=1),
                                                    encoding="utf-8")
    (pathlib.Path(out) / "codegen.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return lines, bad


def check_commit(tree, targets, build_dir, sha, subject, state, out, cache):
    """(result, new state): state is (commit, fingerprints, translation units) of the last build, or ("", None, None)."""
    result = {"sha": sha, "subject": subject, "status": "", "objects": 0, "differences": {}, "unproven": [],
              "not_covered": [], "headers": []}
    parent = git("rev-parse", "--verify", "--end-of-options", f"{sha}^", cwd=tree.root)
    if state[0] != parent:
        prints, covered, note = state_of(tree, targets, build_dir, parent, out, cache)
        if prints is None:
            result.update(status="build-failed", note=note)
            return result, ("", None, None)
        state = (parent, prints, covered)
    prints, covered, note = state_of(tree, targets, build_dir, sha, out, cache)
    if prints is None:
        result.update(status="build-failed", note=note)
        return result, ("", None, None)
    diffs = compare(state[1], prints)
    unproven, not_covered, headers = assess(parent, sha, state[2], covered, tree.root, build_dir, cwd=tree.root)
    result.update(status=verdict(diffs, unproven, not_covered), objects=len(prints), differences=diffs,
                  unproven=unproven, not_covered=not_covered, headers=headers)
    return result, (sha, prints, covered)


def cmd_check(args):
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    selection = json.loads(pathlib.Path(args.selection).read_text(encoding="utf-8"))
    targets = [args.lib, *args.extra_target]
    tree = Tree()
    cache, state, results = {}, ("", None, None), []
    try:
        for sha, subject in selection["checked"]:
            try:
                result, state = check_commit(tree, targets, args.dir, sha, subject, state, out, cache)
            except Exception as error:  # a timeout of the step cannot be caught, but its earlier results are written
                result = {"sha": sha, "subject": subject, "status": "error", "note": f"{type(error).__name__}: {error}"}
                state = ("", None, None)
            results.append(result)
            summary("\n".join(render_commit(result)))
            write_results(out, results, selection["skipped"])
    finally:
        tree.restore()
    lines, bad = write_results(out, results, selection["skipped"])
    summary("\n".join(lines[-1:]))
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
    p.add_argument("--extra-target", action="append", default=[], help="another CMake target to build and compare")
    p.add_argument("--selection", required=True)
    p.add_argument("--out", required=True)
    p.set_defaults(func=cmd_check)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
