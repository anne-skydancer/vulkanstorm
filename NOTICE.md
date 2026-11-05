# Vulkanstorm licensing notice

Vulkanstorm is a viewer for Second Life and OpenSim derived from Firestorm,
which is derived from the Linden Lab Second Life viewer. Since 2026-11-03 it
is offered under the GNU General Public License, version 3 or later,
**as well as** the GNU Lesser General Public License, version 2.1, under
which Linden Lab and Firestorm publish their code. The recipient chooses.

| Group | SPDX expression | Where it lives |
|---|---|---|
| Code from Linden Lab and the Phoenix Firestorm Project, and Vulkanstorm changes to that code | `LGPL-2.1-only OR GPL-3.0-or-later` | Every file carrying a Linden Research or Phoenix Firestorm copyright and `$LicenseInfo` header (`license=viewerlgpl`, `fsviewerlgpl`, and the other LL/FS headers) |
| Original Vulkanstorm code, written for this project | `LGPL-2.1-or-later OR GPL-3.0-or-later` | Files carrying `SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later`; see [Original Vulkanstorm files](#original-vulkanstorm-files) |
| Rust ports of Linden Lab and Firestorm code ([vkstorm-core-rust](https://github.com/anne-skydancer/vkstorm-core-rust)) | `LGPL-2.1-only OR GPL-3.0-or-later` | The crates of that repository; its own `NOTICE.md` lists each crate |
| Third-party components | Their own licences | Fonts, dictionaries, the Tycho-2 star data (`indra/newview/app_settings/stars/LICENSE-TYCHO2.md`, BSD-2-Clause), the GL registry symbol list (`doc/vulkan/gl-api-symbols.LICENSE.txt`, Apache-2.0), the Linden Lab artwork and logos (`doc/LICENSE-logos.txt`, CC BY-SA 3.0, trademarks excluded), and the prebuilt libraries listed in `autobuild.xml` (their texts ship in `licenses/` and `licenses.txt`) |

The licence texts are [`LICENSE-LGPL-2.1`](LICENSE-LGPL-2.1) and
[`LICENSE-GPL-3.0`](LICENSE-GPL-3.0) (the latter verbatim from
<https://www.gnu.org/licenses/gpl-3.0.txt>). The same texts are also at
`doc/LGPL-license.txt` and `indra/newview/LGPL-license.txt`, and ship in
the installed viewer as `LGPL-license.txt` and `GPL-3.0-license.txt`.

## Why GPL-3.0 can be offered for Linden Lab and Firestorm code

Linden Lab and Firestorm license their code under LGPL-2.1 only. Section 3
of LGPL-2.1 lets every licensee move a copy to the ordinary GPL:

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

Vulkanstorm itself does not make that change: it keeps the LGPL-2.1-only
notices of that code, so every recipient keeps both options. The
GPL-3.0-or-later option for this code is the section 3 conversion that each
recipient may make for their own copy.

The source headers of Linden Lab and Firestorm files are therefore left as
they are: the LGPL-2.1-only terms they state remain accurate, as one of the
two options.

## How to choose a licence

- **LGPL-2.1**: use and distribute Vulkanstorm as it is. Linden Lab and
  Firestorm code is then under LGPL-2.1 only, original Vulkanstorm code
  under LGPL-2.1 or (at your option) a later version.
- **GPL-3.0 or later**: for Linden Lab and Firestorm code, make the change
  section 3 above describes in your copy (point its LGPL notices to the GNU
  GPL, version 3 or later); this cannot be undone for that copy and the
  copies made from it. Original Vulkanstorm code is already offered under
  GPL-3.0-or-later and needs no change.

Third-party components stay under their own licences whichever you choose.
Whether a given binary can be distributed under a given option depends on
the libraries linked into it: see the compatibility audit in
[`doc/licensing.md`](doc/licensing.md). **At present the viewer binaries
link OpenSSL 1.1.1, which is not compatible with GPL-3.0, so the
GPL-3.0 option applies to the source code only and the binaries are offered
under LGPL-2.1** until it is replaced (the open point there; GLOD, the other one, has been removed).

## Contributions

Contributions are accepted under the same terms as the file they change:
changes to Linden Lab or Firestorm files under
`LGPL-2.1-only OR GPL-3.0-or-later`, new files and other original code
under `LGPL-2.1-or-later OR GPL-3.0-or-later`. See
[`CONTRIBUTING.md`](CONTRIBUTING.md).

## GPL-only code

There is none in this tree, and there must be none: code available only
under the GPL (for example the Aurora Viewer, GPL-3.0-or-later only) would
remove the LGPL option for the whole viewer. Aurora is a design reference
only; its code is never copied. The night sky (`indra/newview/llnightsky.*`
and its data tools) is the owner's own work, relicensed by her from her
Aurora fork, and is original Vulkanstorm code under the dual licence.

## Original Vulkanstorm files

New source files carry the header

```
SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
```

followed by the Vulkanstorm copyright line. The files that have it so far
are those that were written for Vulkanstorm and either carried a Vulkanstorm
LGPL-2.1-only header or no header at all in `scripts/` and `doc/vulkan/`.
Other files added by Vulkanstorm that do not yet carry the header are
listed in [`doc/licensing.md`](doc/licensing.md#source-headers) for review.
Until a file is reviewed, take it under `LGPL-2.1-only OR GPL-3.0-or-later`,
which is accurate whatever its origin.
