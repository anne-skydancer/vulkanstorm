"""Exercise production location-bar rectangle expressions without a viewer build."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "indra/newview/lllocationinputctrl.cpp").read_text()
RECT_SOURCE = (ROOT / "indra/llmath/llrect.h").read_text()


def body(source, signature):
    start = source.index("{", source.index(signature)) + 1
    end, depth = start, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end - 1]


class Rect:
    def __init__(self, left, bottom, width, height):
        self.mLeft, self.mBottom = left, bottom
        self.mRight, self.mTop = left + width, bottom + height

    def getWidth(self):
        return self.mRight - self.mLeft

    def getHeight(self):
        return self.mTop - self.mBottom

    def apply(self, method, args):
        # Execute the actual LLRect assignment statements, preserving their
        # absolute (=) or relative (+=) semantics rather than copying a model.
        signature, names = {
            "translate": ("LLRectBase& translate(", ("horiz", "vertical")),
            "setOriginAndSize": ("LLRectBase& setOriginAndSize(",
                                 ("left", "bottom", "width", "height")),
        }[method]
        scope = dict(zip(names, args), self=self)
        for line in body(RECT_SOURCE, signature).splitlines():
            line = line.strip()
            if line.startswith("m"):
                exec(re.sub(r"\bm(Left|Right|Top|Bottom)\b", r"self.m\1",
                            line.rstrip(";")), {}, scope)


def layout(star, width, height, arrow_width, icon_pad, method=None):
    production = body(SOURCE, "void LLLocationInputCtrl::updateWidgetlayout()")
    match = re.search(r"al_btn_rect\.(translate|setOriginAndSize)\((.*?)\);",
                      production, re.S)
    expressions = match[2].replace("/", "//").split(",")
    scope = {"al_btn_rect": star, "rect": Rect(0, 0, width, height),
             "hist_btn_rect": Rect(width - arrow_width, 0, arrow_width, height),
             "mIconHPad": icon_pad}
    args = [eval(expression.strip(), {}, scope) for expression in expressions]
    star.apply(method or match[1], args[:2] if method == "translate" else args)


class LocationLayoutTest(unittest.TestCase):
    def test_skin_width_scale_and_repeated_layout(self):
        skins = ROOT / "indra/newview/skins"
        for path in skins.glob("*/xui/en/widgets/location_input.xml"):
            widget = ET.parse(path).getroot()
            button = widget.find("add_landmark_button")
            for width in (120, 240, 580):
                for height in (23, 31):
                    for arrow in (8, 16, 24):
                        for scale in (0.75, 1.0, 1.25, 1.5, 2.0):
                            with self.subTest(skin=path.parts[-5], width=width,
                                              height=height, arrow=arrow, scale=scale):
                                w, h = int(button.get("width")), int(button.get("height"))
                                pad = int(widget.get("icon_hpad", "0"))
                                # Negative XUI left is relative to parent's right.
                                left = int(button.get("left", "0"))
                                star = Rect(width + left if left < 0 else left, 4, w, h)
                                for _ in range(3):
                                    layout(star, width, height, arrow, pad)
                                    self.assertEqual(star.mRight, width - arrow - pad)
                                    self.assertEqual(star.mBottom, (height - h) // 2)
                                    self.assertGreaterEqual(star.mLeft * scale, 0)
                                    self.assertLessEqual(star.mTop * scale, height * scale)
                                    self.assertLess(star.mRight * scale, (width - arrow) * scale)

    def test_old_translation_reproduces_outside_bar(self):
        star = Rect(577, 4, 18, 18)
        layout(star, 580, 23, 16, 2, method="translate")
        self.assertGreater(star.mRight, 580)

    def test_reshape_relayouts_after_children_follow(self):
        reshape = body(SOURCE, "void LLLocationInputCtrl::reshape(")
        self.assertLess(reshape.index("LLComboBox::reshape("),
                        reshape.index("updateWidgetlayout();"))
        star = Rect(0, 0, 18, 18)
        layout(star, 580, 23, 16, 2)
        # Actual right/top follow rules from LLView::reshape.
        star.apply("translate", (120 - 580, 31 - 23))
        layout(star, 120, 31, 16, 2)
        self.assertEqual((star.mRight, star.mBottom), (102, 6))


if __name__ == "__main__":
    unittest.main()
