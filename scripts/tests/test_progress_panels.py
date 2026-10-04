"""Skin-selected progress panels must be complete, not localization fragments."""
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET


SKINS = Path(__file__).resolve().parents[2] / "indra/newview/skins"
COLOUR = "LoginProgressBarFgColor"


def structure(node):
    return (node.tag, dict(node.attrib), (node.text or "").strip(),
            [structure(child) for child in node])


class ProgressPanelTests(unittest.TestCase):
    def test_complete_layout_with_only_fill_colour_changed(self):
        # CURRENT_SKIN selects the most specialized English file; it does not
        # merge the base skin's English panel into this skin's English panel.
        for filename in ("panel_progress.xml", "panel_progress_mini.xml"):
            with self.subTest(filename=filename):
                base = ET.parse(SKINS / "default/xui/en" / filename).getroot()
                modern = ET.parse(SKINS / "ansastorm_modern/xui/en" / filename).getroot()
                base.find(".//progress_bar").set("color_bar", COLOUR)
                self.assertEqual(structure(base), structure(modern))

    def test_postbuild_controls_and_blue_binding(self):
        required = {
            "panel_progress.xml": {"login_progress_bar", "logos_lbl", "progress_text",
                                   "message_text", "login_media_panel", "cancel_btn",
                                   "panel4", "panel_motd", "title_text"},
            "panel_progress_mini.xml": {"progress_bar_mini", "cancel_btn", "progress_text"},
        }
        for filename, names in required.items():
            with self.subTest(filename=filename):
                panel = ET.parse(SKINS / "ansastorm_modern/xui/en" / filename).getroot()
                self.assertTrue(names <= {n.get("name") for n in panel.iter()})
                self.assertGreater(int(panel.get("width")), 0)
                self.assertGreater(int(panel.get("height")), 0)
                self.assertEqual(panel.find(".//progress_bar").get("color_bar"), COLOUR)
        colours = ET.parse(SKINS / "ansastorm_modern/colors.xml").getroot()
        colour = next(c for c in colours if c.get("name") == COLOUR)
        self.assertEqual([float(v) for v in colour.get("value").split()],
                         [0, 0.6211, 0.8477, 0.96])


if __name__ == "__main__":
    unittest.main()
