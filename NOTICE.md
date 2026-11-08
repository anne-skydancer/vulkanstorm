# Vulkanstorm licensing notice

Vulkanstorm is a viewer for Second Life and OpenSim derived from Firestorm,
which is derived from the Linden Lab Second Life viewer. Since 2026-11-08 it
is licensed under the **GNU General Public License, version 3 or (at your
option) any later version** (`GPL-3.0-or-later`). The text is in
[`LICENSE`](LICENSE), verbatim from <https://www.gnu.org/licenses/gpl-3.0.txt>.

The dual licence of 2026-11-04 (`LGPL-2.1` as well as `GPL-3.0-or-later`) is
withdrawn: the LGPL-2.1 option is dropped.

| Group | SPDX expression | Where it lives |
|---|---|---|
| Original Vulkanstorm code, written for this project | `GPL-3.0-or-later` | Files carrying `SPDX-License-Identifier: GPL-3.0-or-later`; see [Original Vulkanstorm files](#original-vulkanstorm-files) |
| Code from Linden Lab and the Phoenix Firestorm Project, and Vulkanstorm changes to that code | LGPL-2.1 headers, used under the GPL | Every file carrying a Linden Research or Phoenix Firestorm copyright and `$LicenseInfo` header (`license=viewerlgpl`, `fsviewerlgpl`, and the other LL/FS headers). Their headers are kept unchanged |
| Rust ports of Linden Lab and Firestorm code ([vkstorm-core-rust](https://github.com/anne-skydancer/vkstorm-core-rust)) | LGPL-2.1 headers, used under the GPL | The crates of that repository; its own `NOTICE.md` lists each crate |
| Third-party components | Their own licences, all compatible with GPL-3.0 | Fonts, dictionaries, the Tycho-2 star data (`indra/newview/app_settings/stars/LICENSE-TYCHO2.md`, BSD-2-Clause), the GL registry symbol list (`doc/vulkan/gl-api-symbols.LICENSE.txt`, Apache-2.0), the Linden Lab artwork and logos (`doc/LICENSE-logos.txt`, CC BY-SA 3.0, trademarks excluded), and the prebuilt libraries listed in `autobuild.xml` (their texts ship in `licenses/` and `licenses.txt`) |

## Linden Lab and Firestorm code: LGPL-2.1 used under the GPL

Linden Lab and Firestorm license their code under LGPL-2.1 only. The text
they refer to is kept in [`LICENSE-LGPL-2.1`](LICENSE-LGPL-2.1) (it ships in
the installed viewer as `LGPL-2.1-license.txt`) so that their file headers
stay accurate. Vulkanstorm uses that code under the ordinary GNU General
Public License, as section 3 of LGPL-2.1 allows:

> 3. You may opt to apply the terms of the ordinary GNU General Public
> License instead of this License to a given copy of the Library.  To do
> this, you must alter all the notices that refer to this License, so
> that they refer to the ordinary GNU General Public License, version 2,
> instead of to this License.  (If a newer version than version 2 of the
> ordinary GNU General Public License has appeared, then you can specify
> that version instead if you wish.)  Do not make any other change in
> these notices.
>
> Once this change is made in a given copy, it is irreversible for
> that copy, so the ordinary GNU General Public License applies to all
> subsequent copies and derivative works made from that copy.

The source headers of Linden Lab and Firestorm files are left as they are
(by decision of the project owner). Section 3 of LGPL-2.1 asks for the
notices of a converted copy to be altered; this is recorded as a finding in
[`doc/licensing.md`](doc/licensing.md#findings). Copyright notices in the
source files remain in force.

One exception to the GPL-only reading: `indra/media_plugins/libvlc/vlc_speakerfill.cpp`
is a VLC module derived from LGPL-2.1-or-later VLC code. It ships with its
source and `COPYING.LIB` (the LGPL-2.1 text) beside the viewer and stays under
LGPL-2.1-or-later, so that VLC can load it.

## Linking and bundling

Whether a binary can be distributed under the GPL depends on the libraries
linked into it: see the compatibility audit in
[`doc/licensing.md`](doc/licensing.md). Every library and bundled component
in the shipped configuration is compatible with GPL-3.0. Proprietary
components cannot be distributed with a GPL-3.0 viewer and are not built or
shipped: Kakadu, FMOD, Havok (a stub is used), BugSplat, the Discord Social
SDK, and, since 2026-11-08, Vivox (the SLVoice SDK, which only OpenSim builds
used; OpenSim voice uses WebRTC).

## Contributions

Contributions are accepted under `GPL-3.0-or-later`. Changes to Linden Lab or
Firestorm files keep those files' headers. See
[`CONTRIBUTING.md`](CONTRIBUTING.md).

## Other people's GPL code

Code available only under the GPL (for example the Aurora Viewer,
GPL-3.0-or-later) can be used where its authors permit and the licence
audit is updated. Aurora is still a design reference first. The night sky
(`indra/newview/llnightsky.*` and its data tools) is the owner's own work,
relicensed by her from her Aurora fork.

## Original Vulkanstorm files

New source files carry the header

```
SPDX-License-Identifier: GPL-3.0-or-later
```

followed by the Vulkanstorm copyright line. The files that have it are those
written for Vulkanstorm that carried the dual header (`LGPL-2.1-or-later OR
GPL-3.0-or-later`, replaced on 2026-11-08), plus the files listed in
[`doc/licensing.md`](doc/licensing.md#source-headers). Files added by
Vulkanstorm that do not carry the header yet are listed there for review;
until a file is reviewed, take it under the GPL, version 3 or later.
