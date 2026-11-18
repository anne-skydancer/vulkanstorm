#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Check that the viewer executable holds no more fused multiply-adds than its platform's baseline.

Multi-core stage 3 (doc/perf/multicore-simd-study.md sections 6.3 and 9): the viewer is bit-exact by decision.
Multiply and add round separately (MSVC /fp:precise without /fp:contract, GCC -ffp-contract=off in
indra/cmake/00-Common.cmake), and the SIMD kernels (indra/llmath/llsimdkernels.h) use no FMA. A count above the
baseline means that something fuses that did not before: a compiler flag or default, an FMA intrinsic, a new library.
When the count rises for a reason that keeps results unchanged (an exact use, third-party code), the change that
causes it raises the baseline in BASELINES, with the reason.

Usage: check_fma_count.py BUILD_DIR PLATFORM    the viewer executable under BUILD_DIR/newview; PLATFORM is the
                                                viewer-build action's platform: windows, linux or linux-lts
       check_fma_count.py --exe EXE PLATFORM    a given executable
       check_fma_count.py --selftest            the parsers on recorded disassembly (any platform, no tools)

Windows disassembles with dumpbin (found as check_cpucheck_obj.py finds it), Linux with objdump. The count covers
the whole executable, statically linked libraries included, and the report names the functions that hold FMAs.
The baselines are for RelWithDebInfo test builds (the CI step runs where the regression tests run); a Release
build inlines differently and is not checked.
"""
import glob
import os
import re
import subprocess
import sys

# platform: (count, why), counted in the RelWithDebInfo CI qualification builds of 2026-11-18: Build Windows run
# 44231920726 and Build Linux run 44231920729 (vkstorm-devel df1a0ce92f), Build Linux LTS run 44222009398 (PR #554 at
# dae06b2b5b, the last linux-lts build before; devel since adds no fma(), lerp() or FMA intrinsic, and no compiler flag).
BASELINES = {
    "windows": (1, "std::lerp(float, float, float): MSVC's standard library computes it with fma()"),
    "linux": (998, "995 in the prebuilt SoLoud package (SoLoud, miniaudio, dr_mp3, stb_vorbis and its FFT); "
                   "3 in GCC's expansion of vectorised unsigned-int-to-float conversions "
                   "(float(hi) * 65536 + float(lo), exact)"),
    "linux-lts": (3, "GCC's expansion of vectorised unsigned-int-to-float conversions (float(hi) * 65536 + "
                     "float(lo), exact); the Ubuntu 24.04 SoLoud package holds none"),
}

# The FMA3 mnemonics (vfmadd132ps .. vfnmsub231sd, vfmaddsub*, vfmsubadd*) and AMD's FMA4 ones (vfmaddps ..).
FMA = re.compile(r"^vf(?:n?m(?:add|sub)|maddsub|msubadd)")

# dumpbin /disasm:nobytes: "  000000014040D3F6: vfmadd213ss xmm0,xmm4,xmm5"; a function is a column-0 line ending in ':'.
DUMPBIN_INSN = re.compile(r"^\s+[0-9A-Fa-f]+:\s+(\S+)")
DUMPBIN_LABEL = re.compile(r"^(\S.*):$")
# objdump -d --no-show-raw-insn: " 1fe735b:\tvfmadd132ps %xmm3,%xmm1,%xmm0"; a function: "0000000001fe7000 <name>:".
OBJDUMP_INSN = re.compile(r"^\s+[0-9a-f]+:\t(\S+)")
OBJDUMP_LABEL = re.compile(r"^[0-9a-f]+ <(.*)>:$")

# A disassembled viewer has millions of instructions (8.9 M in the Windows CI executable); far fewer means the output
# format changed and the scan saw nothing, which must not pass as "no FMA".
MIN_INSTRUCTIONS = 100000


def count_fma(lines, insn, label):
    """(instructions, fused multiply-adds, {function: fused multiply-adds}) of disassembly lines."""
    instructions = 0
    fma = 0
    per_function = {}
    function = "?"
    for line in lines:
        m = insn.match(line)
        if m:
            instructions += 1
            mnemonic = m.group(1).lower()
            if mnemonic.startswith("vf") and FMA.match(mnemonic):
                fma += 1
                per_function[function] = per_function.get(function, 0) + 1
            continue
        m = label.match(line)
        if m:
            function = m.group(1)
    return instructions, fma, per_function


# Verbatim lines of `dumpbin /nologo /disasm:nobytes vulkanstorm-bin.exe` (VS 18, MSVC 14.51, run 44231920726).
DUMPBIN_SAMPLE = """\
Dump of file vulkanstorm-bin.exe

File Type: EXECUTABLE IMAGE

jpeg_simd_cpu_support:
  0000000140001000: push        rbp
  0000000140001001: mov         rbp,rsp
?lerp@std@@YAMMMM@Z:
  000000014040D3E9: vsubss      xmm0,xmm3,xmm5
  000000014040D3F2: vcomiss     xmm4,xmm1
  000000014040D3F6: vfmadd213ss xmm0,xmm4,xmm5
  000000014040D3FB: seta        al
"""

# Verbatim lines of `objdump -d --no-show-raw-insn do-not-directly-run-vulkanstorm-bin` (binutils 2.42, run 44231920729).
OBJDUMP_SAMPLE = """\
Disassembly of section .init:

0000000000563000 <_init>:
  563000:\tendbr64
  563004:\tsub    $0x8,%rsp
0000000001fe7000 <_ZN2LL4GLTF4copyI10LLVector4aEEbRNS0_5AssetERNS0_8AccessorER9LLStriderIT_E.constprop.0>:
 1fe7353:\tvcvtdq2ps %xmm1,%xmm1
 1fe7357:\tvcvtdq2ps %xmm0,%xmm0
 1fe735b:\tvfmadd132ps %xmm3,%xmm1,%xmm0
 1fe7360:\tvmovaps %xmm0,(%rsi)
"""


def selftest():
    for mnemonic in ("vfmadd132ps", "vfmadd213ss", "vfmadd231sd", "vfmsub132pd", "vfnmadd213ps", "vfnmsub231ss",
                     "vfmaddsub132ps", "vfmsubadd231pd", "vfmaddps", "vfnmsubsd"):
        assert FMA.match(mnemonic), mnemonic
    for mnemonic in ("vmulps", "vaddss", "vfixupimmps", "vfpclassps", "vfrczps", "fmul", "vpermilps", "vfmulcph"):
        assert not FMA.match(mnemonic), mnemonic

    instructions, fma, per_function = count_fma(DUMPBIN_SAMPLE.splitlines(), DUMPBIN_INSN, DUMPBIN_LABEL)
    assert (instructions, fma) == (6, 1), (instructions, fma)
    assert per_function == {"?lerp@std@@YAMMMM@Z": 1}, per_function

    instructions, fma, per_function = count_fma(OBJDUMP_SAMPLE.splitlines(), OBJDUMP_INSN, OBJDUMP_LABEL)
    assert (instructions, fma) == (6, 1), (instructions, fma)
    assert per_function == {
        "_ZN2LL4GLTF4copyI10LLVector4aEEbRNS0_5AssetERNS0_8AccessorER9LLStriderIT_E.constprop.0": 1}, per_function

    assert verdict(1, "windows")[0] == 0 and verdict(0, "windows")[0] == 0 and verdict(2, "windows")[0] == 1
    assert verdict(5, "freebsd")[0] == 1
    print("selftest ok")


def verdict(fma, platform):
    """(exit status, message) for a count on a platform."""
    if platform not in BASELINES or BASELINES[platform][0] is None:
        return 1, "no fused multiply-add baseline for platform %r (known: %s)" % (
            platform, ", ".join(p for p, b in BASELINES.items() if b[0] is not None))
    baseline, why = BASELINES[platform]
    if fma > baseline:
        return 1, ("%d fused multiply-adds, %d above the %s baseline of %d (%s). Something fuses multiply and add "
                   "that did not: see the functions below. If the new ones keep results unchanged, raise the baseline "
                   "in scripts/tests/check_fma_count.py with the reason." % (fma, fma - baseline, platform, baseline, why))
    note = "" if fma == baseline else "; below the baseline, which can be lowered"
    return 0, "%d fused multiply-adds, baseline %d (%s)%s" % (fma, baseline, why, note)


def find_dumpbin():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from check_cpucheck_obj import find_dumpbin as find
    return find()


def find_viewer(build_dir, platform):
    """The newest viewer executable under BUILD_DIR/newview, or None."""
    newview = os.path.join(build_dir, "newview")
    if platform == "windows":
        hits = glob.glob(os.path.join(newview, "*", "*-bin.exe"))
    else:
        hits = (glob.glob(os.path.join(newview, "packaged", "bin", "do-not-directly-run-*-bin"))
                or glob.glob(os.path.join(newview, "*-bin")))
    hits = [h for h in hits if os.path.isfile(h)]
    return max(hits, key=os.path.getmtime) if hits else None


def disassemble(exe, platform):
    """(instructions, fma, per_function), or raises RuntimeError."""
    if platform == "windows":
        dumpbin = find_dumpbin()
        if not dumpbin:
            raise RuntimeError("dumpbin.exe not found")
        cmd = [dumpbin, "/nologo", "/disasm:nobytes", exe]
        insn, label = DUMPBIN_INSN, DUMPBIN_LABEL
    else:
        cmd = ["objdump", "-d", "--no-show-raw-insn", exe]
        insn, label = OBJDUMP_INSN, OBJDUMP_LABEL
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace",
                            bufsize=1 << 20)
    result = count_fma(proc.stdout, insn, label)
    err = proc.stderr.read()
    if proc.wait() != 0:
        raise RuntimeError("%s failed (%d): %s" % (cmd[0], proc.returncode, err.strip()[:2000]))
    return result


def main(argv):
    if len(argv) == 2 and argv[1] == "--selftest":
        selftest()
        return 0
    if len(argv) == 4 and argv[1] == "--exe":
        exe, platform = argv[2], argv[3]
    elif len(argv) == 3:
        platform = argv[2]
        exe = find_viewer(argv[1], platform)
        if not exe:
            print("error: no viewer executable under " + os.path.join(argv[1], "newview"))
            return 1
    else:
        print(__doc__)
        return 2
    if (platform == "windows") != (os.name == "nt"):
        print("error: the %s executable is disassembled on its own platform" % platform)
        return 2
    try:
        instructions, fma, per_function = disassemble(exe, platform)
    except (OSError, RuntimeError) as e:
        print("error: " + str(e))
        return 1
    if instructions < MIN_INSTRUCTIONS:
        print("error: only %d instructions disassembled from %s" % (instructions, exe))
        return 1
    status, message = verdict(fma, platform)
    print("%s: %s: %d instructions, %s" % ("ok" if status == 0 else "error", exe, instructions, message))
    shown = sorted(per_function.items(), key=lambda kv: (-kv[1], kv[0]))
    for function, n in shown if status else shown[:12]:
        print("  %6d  %s" % (n, function))
    if status == 0 and len(shown) > 12:
        print("  ... and %d more functions" % (len(shown) - 12))
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv))
