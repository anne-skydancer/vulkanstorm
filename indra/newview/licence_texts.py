# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The licence texts shipped with the viewer and the installer's licence page.

Vulkanstorm is dual-licensed (NOTICE.md at the top of the source tree):
LGPL-2.1, or GPL-3.0 or later. viewer_manifest.py ships both texts and the
dual-licence statement beside the viewer on every platform, and the Windows
installer shows INSTALLER_LICENCE_HEADER followed by both texts.
"""

# (source relative to indra/newview, destination in the installed viewer)
LICENCE_TEXTS = (
    ("LGPL-license.txt", "LGPL-license.txt"),
    ("../../LICENSE-GPL-3.0", "GPL-3.0-license.txt"),
    ("../../LICENSE", "LICENSE.txt"),
)

INSTALLER_LICENCE_HEADER = """\
Vulkanstorm is free software, offered under two licences. You may choose
either of them:

  1. the GNU Lesser General Public License, version 2.1 (the first text
     below), or
  2. the GNU General Public License, version 3 or (at your option) any
     later version (the second text below).

Linden Lab and the Phoenix Firestorm Project license their code under
LGPL-2.1 only; section 3 of LGPL-2.1 lets you apply the GNU GPL, version 3
or later, to your copy instead. Original Vulkanstorm code is offered under
LGPL-2.1 or later, or GPL-3.0 or later. Third-party components keep their
own licences: see licenses.txt and the licenses folder after installation.

Neither licence limits running the program; they set the terms for
copying, modifying and distributing it. The source code is available at
https://github.com/anne-skydancer/vulkanstorm (see NOTICE.md there).
"""

INSTALLER_LICENCE_SEPARATOR = "=" * 75


def write_installer_licence(path, lgpl_path, gpl_path):
    """Write the installer's licence page: the header, then both licences."""
    parts = [INSTALLER_LICENCE_HEADER]
    for source in (lgpl_path, gpl_path):
        with open(source, "r", encoding="ascii") as f:
            parts.append(INSTALLER_LICENCE_SEPARATOR + "\n\n" + f.read().strip("\n") + "\n")
    # Plain ASCII with CRLF line ends, the form Inno Setup's licence page reads.
    with open(path, "w", encoding="ascii", newline="\r\n") as f:
        f.write("\n".join(parts))
