#!/usr/bin/env python3
"""Check native script/IDE UI calls against the bundled English resources."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
VIEWER = ROOT / "indra" / "newview"
SOURCES = (
    "llscripteditorws.cpp",
    "llfloaterscripting.cpp",
    "llpreviewscript.cpp",
    "llpanelcontents.cpp",
)


class ScriptUIResourcesTest(unittest.TestCase):
    def check_resources(self, resource, pattern):
        names = {node.get("name") for node in ET.parse(
            VIEWER / "skins" / "default" / "xui" / "en" / resource).getroot()}
        missing = []
        for filename in SOURCES:
            source = (VIEWER / filename).read_text(encoding="utf-8")
            missing.extend(f"{filename}: {name}" for name in re.findall(pattern, source)
                           if name not in names)
        self.assertEqual([], missing, "Native script UI references missing resources")

    def test_notifications(self):
        self.check_resources("notifications.xml", r'LLNotificationsUtil::add\s*\(\s*"([^\"]+)"')

    def test_strings(self):
        self.check_resources("strings.xml", r'LLTrans::getString\s*\(\s*"([^\"]+)"')


if __name__ == "__main__":
    unittest.main()
