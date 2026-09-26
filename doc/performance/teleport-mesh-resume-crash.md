# Teleport crash: resumed mesh preparation

Investigation date: 2026-09-26. Source fix targets master.

## Evidence

The installed Vulkanstorm Release 7.2.5.81996 crashed at 21:02:41 UTC
(22:02:41 local). Windows Application event 1000 records access violation
`0xc0000005` in the viewer executable at RVA `0x6ffb4c`.
The session used Mesa/Zink on an AMD RX 9070 XT. Its log records teleport
completion immediately before old avatars are removed and logging stops.

Build 81996 came from source `49e54a9750ccc94032df0f7094e0ae35a54806b5`,
before the OpenGL modernization merge. The installed executable SHA-256 is
`24b3b5a6945962687564ab94a5cc783971cddc018a988626be3de7d4ee44c3cd`.
Its CodeView record references the CI build's `vulkanstorm-bin.pdb`; that exact
PDB and a crash memory dump were not available during this investigation.

The fault instruction is `mov r9d, [rcx+0x70]`, immediately after loading a
pointer from the returned face at offset `0xb0`. Its surrounding instruction
sequence matches the local cleanup build, with relocated calls. The local
matching sequence at VA `0x1406ff71c` resolves through that build's own PDB to
`advanceJob`, `llcomputelod.cpp:595`, the resumed face's
`face.getVertexBuffer()->getTypeMask()` access. This is a binary comparison,
not symbolization of the installed executable using a mismatched PDB.

These observations strongly identify the failing source operation. Without
registers or a dump, they do not prove whether the pointer was null versus
otherwise invalid, or establish a complete crash stack.

## Defect and correction

Mesh preparation yields across frames. Previously, face/buffer, skin, and
compute-eligibility checks ran only while `job.initialized` was false.
Later steps dereferenced the live face's vertex buffer without repeating that
validation. A drawable can lose its buffer between those steps, including
during teleport/rebuild transitions.

Move the existing checks into the preflight that runs on every preparation
step, alongside material readiness. Missing faces/buffers wait for DRAWABLE;
missing rigged dependencies wait for SKIN/DRAWABLE; faces that no longer
qualify leave compute preparation. Normal drawable rendering and existing
dependency notifications remain responsible for readiness and retry.

The check remains within the existing bounded preparation job, uses no GPU
readback or synchronization, and introduces no runtime profiling hook.

## Regression and limits

`scripts/tests/test_compute_resume.py` compiles the actual production
`advanceJob` entry checks and initialization preflight against small dependency
doubles. It simulates a valid job resuming after buffer removal, restoration,
face removal, drawable rebuild, skin changes, media/material changes, loss of
compute ownership eligibility, and object death.

The regression fails against build 81996's source with an initialized job and
missing vertex buffer, and passes after the correction. It does not execute
the entire mesh upload/publish pipeline or reproduce a network teleport.
In-world reproduction remains necessary to confirm resolution of the reported
crash. Original logs and disassembly are preserved locally under `.tmp/` and
are deliberately excluded from source control.

The corrected Windows Release viewer compiled and linked successfully using
the existing Autobuild-configured tree. Runtime assets and media plugins were
staged without creating an installer. A 20-second Zink startup/shutdown smoke
test with an isolated profile exited 0; both main and upload contexts requested
OpenGL 4.3 Core. This did not log in or perform a teleport.
