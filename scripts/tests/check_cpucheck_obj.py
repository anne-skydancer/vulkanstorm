#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the viewer's CPU-check object and the executable's TLS callbacks.

indra/newview/llcpucheck_win.cpp is the executable's entry point; it must run
on any x86-64 processor, so it is compiled /arch:SSE2 and must contain none of
the instructions that need a newer processor: every VEX/EVEX-encoded
instruction (all v-mnemonics: AVX, AVX2, FMA, F16C, BMI1/BMI2), LZCNT, TZCNT,
MOVBE, POPCNT, SSSE3, SSE4.1/4.2, CRC32, PCLMULQDQ, AES-NI, ADCX/ADOX,
RDRAND/RDSEED, CMPXCHG16B and the SHA extensions. (A blocklist, not an
allowlist: the object is a few dozen instructions, and an allowlist of every
baseline x86-64 mnemonic would break on compiler changes without protecting
more.)

Given a build directory, it also checks the TLS callback list of the
executable linked next to the object (newview/<config>/), with `dumpbin /tls`:
exactly three callbacks, the Boost one (unnamed in dumpbin), __dyn_tls_init and
__dyn_tls_dtor. The comment in llcpucheck_win.cpp about what runs before the
entry point depends on that list; a new callback needs that comment looked at
again.

Usage: check_cpucheck_obj.py BUILD_DIR     (object and executable)
       check_cpucheck_obj.py FILE.obj      (object only)
       check_cpucheck_obj.py --selftest    (any platform; no dumpbin needed)

Needs dumpbin (Visual Studio); it is looked up on PATH, then with vswhere,
then under Program Files. On Windows a missing dumpbin is a hard failure.
Without --selftest on another platform the exit status is non-zero ("not
supported"), so a misconfigured call is not mistaken for a pass; the CI step
runs on Windows only.
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
    # SSSE3
    "pshufb", "phaddw", "phaddd", "phaddsw", "phsubw", "phsubd", "phsubsw",
    "pmaddubsw", "pmulhrsw", "psignb", "psignw", "psignd", "pabsb", "pabsw",
    "pabsd", "palignr",
    # SSE4.1
    "pblendw", "blendps", "blendpd", "blendvps", "blendvpd", "pblendvb",
    "dpps", "dppd", "extractps", "insertps", "pextrb", "pextrd", "pextrq",
    "pinsrb", "pinsrd", "pinsrq", "pmaxsb", "pmaxsd", "pmaxud", "pmaxuw",
    "pminsb", "pminsd", "pminud", "pminuw", "pmovsxbw", "pmovsxbd",
    "pmovsxbq", "pmovsxwd", "pmovsxwq", "pmovsxdq", "pmovzxbw", "pmovzxbd",
    "pmovzxbq", "pmovzxwd", "pmovzxwq", "pmovzxdq", "pmuldq", "pmulld",
    "ptest", "roundps", "roundpd", "roundss", "roundsd", "phminposuw",
    "mpsadbw", "packusdw", "pcmpeqq", "movntdqa",
    # SSE4.2 and other extensions
    "pcmpestri", "pcmpestrm", "pcmpistri", "pcmpistrm", "pcmpgtq", "crc32",
    "pclmulqdq", "aesenc", "aesenclast", "aesdec", "aesdeclast", "aesimc",
    "aeskeygenassist", "adcx", "adox", "rdrand", "rdseed", "cmpxchg16b",
    "sha1rnds4", "sha1nexte", "sha1msg1", "sha1msg2", "sha256rnds2",
    "sha256msg1", "sha256msg2",
}

# A real function disassembles to dozens of instructions (llcpucheck_win.obj
# holds 64); fewer means the dumpbin output format changed and the scan
# saw nothing, which must not pass as "no forbidden instructions".
MIN_INSTRUCTIONS = 20

# The executable's TLS callbacks (dumpbin /tls, "TLS Callbacks" table). The
# Boost one is a file-static function that dumpbin lists without a name.
EXPECTED_TLS_CALLBACKS = 3
KNOWN_TLS_NAMES = {"__dyn_tls_init", "__dyn_tls_dtor"}

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


def tls_callbacks(lines):
    """[(address, name)] from the "TLS Callbacks" table of dumpbin /tls output."""
    found = []
    in_table = False
    for line in lines:
        if "TLS Callbacks" in line:
            in_table = True
            continue
        if not in_table:
            continue
        m = re.match(r"^\s+([0-9A-Fa-f]{16})(?:\s+(\S+))?\s*$", line)
        if m:
            if int(m.group(1), 16) == 0:        # the list terminator
                break
            found.append((m.group(1), m.group(2) or ""))
    return found


def tls_problem(callbacks):
    """None if the callback list is the expected one, else a message."""
    if len(callbacks) != EXPECTED_TLS_CALLBACKS:
        return "%d TLS callbacks, expected %d: %s" % (
            len(callbacks), EXPECTED_TLS_CALLBACKS, callbacks)
    for _, name in callbacks:
        if name and name not in KNOWN_TLS_NAMES:
            return "unexpected TLS callback " + name
    names = {n for _, n in callbacks}
    if not KNOWN_TLS_NAMES <= names:
        return "TLS callbacks lack %s" % sorted(KNOWN_TLS_NAMES - names)
    return None


# Verbatim lines of `dumpbin /nologo /disasm llcpucheck_win.obj` (VS 18, MSVC
# 14.51), including the continuation line a long instruction wraps onto.
DUMPBIN_DISASM_SAMPLE = """\
Dump of file build-vc180-64\\newview\\vulkanstorm-bin.dir\\Release\\llcpucheck_win.obj

File Type: EXTENDED COFF OBJECT

  0000000000000000: 40 53              push        rbx
  0000000000000002: 48 83 EC 30        sub         rsp,30h
  000000000000000A: 0F A2              cpuid
  000000000000000F: 0F 8C 99 00 00 00  jl          00000000000000AE
  0000000000000037: 41 81 E2 00 10 00  and         r10d,1000h
                    00
  000000000000005D: 41 0F BA E0 16     bt          r8d,16h
  0000000000000092: 0F 01 D0           xgetbv
  00000000000000A9: E9 00 00 00 00     jmp         wWinMainCRTStartup
"""

# Verbatim `dumpbin /nologo /tls Vulkanstorm-Release.exe` TLS callback table.
DUMPBIN_TLS_SAMPLE = """\
    TLS Callbacks

          Address
          ----------------
          00000001423C19F0
          0000000142419810  __dyn_tls_init
          0000000142419C70  __dyn_tls_dtor
          0000000000000000

  Summary
"""


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
        "  0000000000000016: 66 0F 38 00 C1     pshufb      xmm0,xmm1",
        "  000000000000001B: 66 0F 3A 0B C1 09  roundsd     xmm0,xmm1,9",
        "  0000000000000021: F2 0F 38 F1 C1     crc32       eax,ecx",
        "  0000000000000026: 66 0F 3A 63 C1 0C  pcmpistri   xmm0,xmm1,0Ch",
        "  000000000000002C: 66 0F 3A 44 C1 00  pclmulqdq   xmm0,xmm1,0",
        "  0000000000000032: 66 0F 38 DC C1     aesenc      xmm0,xmm1",
        "  0000000000000037: 66 0F 38 F6 C1     adcx        eax,ecx",
        "  000000000000003C: 0F C7 F0           rdrand      eax",
        "  000000000000003F: 48 0F C7 0E        cmpxchg16b  xmmword ptr [rsi]",
    ]
    assert forbidden(ok) == [], forbidden(ok)
    assert [m for _, m in forbidden(bad)] == [
        "vmovdqa", "vbroadcastss", "andn", "lzcnt", "pshufb", "roundsd",
        "crc32", "pcmpistri", "pclmulqdq", "aesenc", "adcx", "rdrand",
        "cmpxchg16b"], forbidden(bad)

    lines = DUMPBIN_DISASM_SAMPLE.splitlines()
    assert sum(1 for l in lines if INSN.match(l)) == 8, "the wrapped '00' is no instruction"
    assert forbidden(lines) == []

    callbacks = tls_callbacks(DUMPBIN_TLS_SAMPLE.splitlines())
    assert callbacks == [
        ("00000001423C19F0", ""),
        ("0000000142419810", "__dyn_tls_init"),
        ("0000000142419C70", "__dyn_tls_dtor")], callbacks
    assert tls_problem(callbacks) is None
    extra = DUMPBIN_TLS_SAMPLE.replace(
        "          0000000000000000\n",
        "          0000000142419D00  surprise\n          0000000000000000\n")
    assert tls_problem(tls_callbacks(extra.splitlines())) is not None
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


def find_exe(obj):
    """The viewer executable linked next to llcpucheck_win.obj, or None.

    The object is newview/<target>.dir/<config>/llcpucheck_win.obj and the
    executable newview/<config>/<name>.exe (the installers and SLVoice live
    there too).
    """
    config_dir = os.path.dirname(obj)
    newview = os.path.dirname(os.path.dirname(config_dir))
    exes = [e for e in glob.glob(os.path.join(
                newview, os.path.basename(config_dir), "*.exe"))
            if "Setup" not in os.path.basename(e)
            and os.path.basename(e).lower() != "slvoice.exe"]
    return max(exes, key=os.path.getmtime) if exes else None


def main(argv):
    if len(argv) == 2 and argv[1] == "--selftest":
        selftest()
        return 0
    if len(argv) != 2:
        print(__doc__)
        return 2
    if os.name != "nt":
        print("error: needs dumpbin (Windows); only --selftest runs elsewhere")
        return 2
    target = argv[1]
    check_exe = False
    if os.path.isdir(target):
        hits = glob.glob(os.path.join(target, "**", "llcpucheck_win.obj"), recursive=True)
        if not hits:
            print("error: no llcpucheck_win.obj under " + target)
            return 1
        # newest one if a multi-config tree holds several
        target = max(hits, key=os.path.getmtime)
        check_exe = True
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
    if count < MIN_INSTRUCTIONS:
        print("error: only %d instructions disassembled from %s" % (count, target))
        return 1
    bad = forbidden(proc.stdout.splitlines())
    if bad:
        print("error: %s contains instructions that need a newer processor:" % target)
        for line, mnemonic in bad:
            print("  " + line)
        return 1
    print("ok: %s, %d instructions, none that need more than baseline x86-64" % (target, count))

    if check_exe:
        exe = find_exe(target)
        if not exe:
            print("error: no executable next to " + target)
            return 1
        proc = subprocess.run([dumpbin, "/nologo", "/tls", exe],
                              capture_output=True, text=True)
        if proc.returncode != 0:
            print("error: dumpbin /tls failed:\n" + proc.stdout + proc.stderr)
            return 1
        callbacks = tls_callbacks(proc.stdout.splitlines())
        problem = tls_problem(callbacks)
        if problem:
            print("error: %s: %s" % (exe, problem))
            print("  llcpucheck_win.cpp's account of what runs before the entry"
                  " point assumes the list in check_cpucheck_obj.py")
            return 1
        print("ok: %s, TLS callbacks %s" % (
            exe, [n or "(unnamed)" for _, n in callbacks]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
