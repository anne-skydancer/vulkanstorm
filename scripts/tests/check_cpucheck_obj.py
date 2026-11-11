#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail if the viewer's CPU-check object contains AVX2-era instructions.

indra/newview/llcpucheck_win.cpp is the executable's entry point; it must
run on any x86-64 processor, so it is compiled /arch:SSE2 and must not
contain VEX/EVEX-encoded instructions (every v-mnemonic: AVX, AVX2, FMA, F16C),
the BMI1/BMI2 instructions (VEX-encoded too), or LZCNT/TZCNT/MOVBE/POPCNT.

Usage: check_cpucheck_obj.py BUILD_DIR     (searches for llcpucheck_win.obj)
       check_cpucheck_obj.py FILE.obj
       check_cpucheck_obj.py --selftest

Needs dumpbin (Visual Studio); it is looked up on PATH, then with vswhere,
then under Program Files. Windows only: other platforms skip with exit 0.
"""
import glob
import os
import re
import subprocess
import sys

# VEX/EVEX mnemonics all start with "v"; these are the ones that do not.
NOT_VEX_V = {
    "verr", "verw", "vmcall", "vmclear", "vmfunc", "vmlaunch", "vmptrld",
    "vmptrst", "vmread", "vmresume", "vmwrite", "vmxoff", "vmxon",
}
OTHER_FORBIDDEN = {
    "andn", "bextr", "blsi", "blsmsk", "blsr", "bzhi", "mulx", "pdep", "pext",
    "rorx", "sarx", "shlx", "shrx",     # BMI1/BMI2 (VEX-encoded)
    "lzcnt", "tzcnt", "movbe", "popcnt",
}

# "  0000000000000040: C5 F9 6F 05 00 00 00 00 vmovdqa      xmm0,xmmword ptr [..]"
# The hex bytes are two-digit groups; the mnemonic is the first token after them.
INSN = re.compile(r"^\s*[0-9A-Fa-f]+:\s+((?:[0-9A-Fa-f]{2}\s)+)\s*(\S+)")


def forbidden(lines):
    """Return [(line, mnemonic)] of the forbidden instructions in dumpbin /disasm output."""
    found = []
    for line in lines:
        m = INSN.match(line)
        if not m:
            continue
        mnemonic = m.group(2).lower()
        if mnemonic in OTHER_FORBIDDEN or (
                mnemonic.startswith("v") and mnemonic not in NOT_VEX_V):
            found.append((line.rstrip(), mnemonic))
    return found


def selftest():
    ok = [
        "  0000000000000000: 48 83 EC 28        sub         rsp,28h",
        "  0000000000000004: 0F 28 C1           movaps      xmm0,xmm1",
        "  0000000000000007: 0F A2              cpuid",
        "  0000000000000009: 0F 01 D0           xgetbv",
        "  000000000000000C: 0F 00 E0           verr        ax",
        "  Summary",
    ]
    bad = [
        "  0000000000000000: C5 F9 6F 05 00 00 00 00 vmovdqa      xmm0,xmmword ptr [x]",
        "  0000000000000008: C4 E2 79 18 C0     vbroadcastss xmm0,xmm0",
        "  000000000000000D: C4 E2 78 F2 C1     andn        eax,eax,ecx",
        "  0000000000000012: F3 0F BD C0        lzcnt       eax,eax",
    ]
    assert forbidden(ok) == [], forbidden(ok)
    assert [m for _, m in forbidden(bad)] == ["vmovdqa", "vbroadcastss", "andn", "lzcnt"]
    print("selftest ok")


def find_dumpbin():
    for d in os.environ.get("PATH", "").split(os.pathsep):
        p = os.path.join(d, "dumpbin.exe")
        if os.path.isfile(p):
            return p
    pf86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = os.path.join(pf86, "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if os.path.isfile(vswhere):
        out = subprocess.run(
            [vswhere, "-latest", "-products", "*", "-find",
             r"VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe"],
            capture_output=True, text=True).stdout.split("\n")
        for p in out:
            if p.strip() and os.path.isfile(p.strip()):
                return p.strip()
    for root in (os.environ.get("ProgramFiles", r"C:\Program Files"), pf86):
        hits = sorted(glob.glob(os.path.join(
            root, "Microsoft Visual Studio", "*", "*", "VC", "Tools", "MSVC", "*",
            "bin", "Hostx64", "x64", "dumpbin.exe")))
        if hits:
            return hits[-1]
    return None


def main(argv):
    if len(argv) == 2 and argv[1] == "--selftest":
        selftest()
        return 0
    if len(argv) != 2:
        print(__doc__)
        return 2
    if os.name != "nt":
        print("not Windows: skipped")
        return 0
    target = argv[1]
    if os.path.isdir(target):
        hits = glob.glob(os.path.join(target, "**", "llcpucheck_win.obj"), recursive=True)
        if not hits:
            print("error: no llcpucheck_win.obj under " + target)
            return 1
        # newest one if a multi-config tree holds several
        target = max(hits, key=os.path.getmtime)
    dumpbin = find_dumpbin()
    if not dumpbin:
        print("error: dumpbin.exe not found")
        return 1
    proc = subprocess.run([dumpbin, "/nologo", "/disasm", target],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        print("error: dumpbin failed:\n" + proc.stdout + proc.stderr)
        return 1
    count = sum(1 for line in proc.stdout.splitlines() if INSN.match(line))
    if count < 20:
        print("error: only %d instructions disassembled from %s" % (count, target))
        return 1
    bad = forbidden(proc.stdout.splitlines())
    if bad:
        print("error: %s contains instructions that need a newer processor:" % target)
        for line, mnemonic in bad:
            print("  " + line)
        return 1
    print("ok: %s, %d instructions, no VEX/BMI/LZCNT/MOVBE/POPCNT" % (target, count))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
