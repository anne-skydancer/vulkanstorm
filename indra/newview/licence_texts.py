# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The licence texts shipped with the viewer and the installer's licence page.

Vulkanstorm is licensed under the GNU General Public License, version 3 or
(at your option) any later version (LICENSE and NOTICE.md at the top of the
source tree). viewer_manifest.py ships the GPL-3.0 text, the LGPL-2.1 text that
the Linden Lab and Firestorm file headers refer to, and NOTICE.md beside the
viewer on every platform, and the Windows installer shows
INSTALLER_LICENCE_HEADER followed by the GPL-3.0 text.
"""

# (source relative to indra/newview, destination in the installed viewer)
LICENCE_TEXTS = (
    ("../../LICENSE", "LICENSE.txt"),
    ("../../LICENSE-LGPL-2.1", "LGPL-2.1-license.txt"),
    ("../../NOTICE.md", "NOTICE.md"),
)

INSTALLER_LICENCE_HEADER = """\
Vulkanstorm is free software, licensed under the GNU General Public License,
version 3 or (at your option) any later version (the text below).

Linden Lab and the Phoenix Firestorm Project code stays under the GNU Lesser
General Public License, version 2.1, with its file headers unchanged
(LGPL-2.1-license.txt and NOTICE.md beside the viewer), and is combined into
Vulkanstorm, which as a whole is licensed GPL-3.0-or-later (LGPL-2.1 is
compatible with inclusion in a GPL-3.0 work). Third-party components keep their own licences: see licenses.txt
and the licenses folder after installation.

The licence does not limit running the program; it sets the terms for
copying, modifying and distributing it. The source code is available at
https://github.com/anne-skydancer/vulkanstorm (see NOTICE.md there).
"""

INSTALLER_LICENCE_SEPARATOR = "=" * 75


def write_installer_licence(path, gpl_path):
    """Write the installer's licence page: the header, then the GPL-3.0 text."""
    with open(gpl_path, "r", encoding="ascii") as f:
        gpl = f.read().strip("\n") + "\n"
    parts = [INSTALLER_LICENCE_HEADER, INSTALLER_LICENCE_SEPARATOR + "\n\n" + gpl]
    # Plain ASCII with CRLF line ends, the form Inno Setup's licence page reads.
    with open(path, "w", encoding="ascii", newline="\r\n") as f:
        f.write("\n".join(parts))
