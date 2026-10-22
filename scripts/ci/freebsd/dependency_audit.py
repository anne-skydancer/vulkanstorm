"""Report native FreeBSD dependency coverage in the existing Autobuild manifest."""

import json
from pathlib import Path
import xml.etree.ElementTree as ET


def llsd_map(node):
    children = list(node)
    return {children[i].text: children[i + 1] for i in range(0, len(children), 2)}


def audit(path):
    manifest = llsd_map(ET.parse(path).getroot().find("map"))
    packages = llsd_map(manifest["installables"])
    missing = []
    covered = []
    for name, package in sorted(packages.items()):
        platforms = llsd_map(llsd_map(package)["platforms"])
        # 'common' packages can be architecture-independent assets; they do not
        # demonstrate that any of the native libraries have been ported.
        target = covered if "freebsd64" in platforms else missing
        target.append(name)
    return {
        "platform": "freebsd64",
        "native_entries": covered,
        "without_native_entries": missing,
        "viewer_qualified": False,
        "strategy": "Prefer FreeBSD pkg/ports system libraries; source-build exceptions explicitly.",
        "note": "Missing archive entries are not a requirement to create FreeBSD archives. Review system library discovery, common assets and optional dependencies individually.",
    }


if __name__ == "__main__":
    print(json.dumps(audit(Path("autobuild.xml")), indent=2))
