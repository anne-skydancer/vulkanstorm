"""Check release layout and native width compatibility without a viewer build.

Firestorm_Release_7.2.4.80712: 10bd3c9f930c76e1427ddd4ecece6cdf36b4406d.
"""
from pathlib import Path
import hashlib
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = "10bd3c9f930c76e1427ddd4ecece6cdf36b4406d"
# Digests extracted from the pinned official release; no fetched Git object or
# network is required to run these checks in a fresh/shallow source checkout.
FUNCTION_DIGESTS = {
    "LLLocationInputCtrl::LLLocationInputCtrl(": "d67f2b096cef9ed55eb0b3a48d2f037461410aebd7dfe780ef79422e08761f19",
    "void LLLocationInputCtrl::reshape(": "949ef881d7172bf22fa38cb7e16f7baf0dd5dd3e980f4ab688691607455584f0",
    "void LLLocationInputCtrl::updateWidgetlayout()": "d14db21250f8e0a0891403bb4f64ea7d3a58790fb8858d8f7dc8e79d5aa4eff8",
    "void LLLocationInputCtrl::refreshParcelIcons()": "601dfec0276b65844f9c7e2f412e81b510221d64ae7d950902d8084baffab089",
}
WIDGET_DIGESTS = {
    "default": "c3c7d1a8127dd42db2cfeed4ebd201361caea329238fe2d38fb6c6686b249524",
    "starlight": "28be92adb8ae299c816dd9a864ee8369ae6beffa654f65e689b07ea3e83db5ec",
    "starlightcui": "28be92adb8ae299c816dd9a864ee8369ae6beffa654f65e689b07ea3e83db5ec",
    "vintage": "807e73a075e7ded069b1941aa8c075c758e82486d82490ddbf6e69937d04bd94",
}
PANEL_DIGESTS = {
    "default": "9e76455553f85145987645b60c23b745c90be97af235137879d02ba7de1c97f4",
    "metaharper": "a9cff3b761edc6874d4fe433bdb36bfda3ff2bfae274810b57fc2f0bdc279a07",
    "starlight": "b14f060353958772188435541bb6b7ebc468d935506a4554f60c6d750fbb2521",
    "starlightcui": "b997bf640eaf5008831c627ce970e86f172ca838f35d3678053e2eab50780659",
}
SOURCE = (ROOT / "indra/newview/lllocationinputctrl.cpp").read_text()
RECT_SOURCE = (ROOT / "indra/llmath/llrect.h").read_text()
COMBO_SOURCE = (ROOT / "indra/llui/llcombobox.cpp").read_text()
COMBO_HEADER = (ROOT / "indra/llui/llcombobox.h").read_text()
VK_SOURCE = (ROOT / "indra/llvulkan/llvkuirender.cpp").read_text()
SHADOW = int(re.search(r"BTN_DROP_SHADOW = (\d+)",
                      (ROOT / "indra/llui/llbutton.cpp").read_text())[1])


def body(source, signature):
    start = source.index("{", source.index(signature)) + 1
    end, depth = start, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end - 1]


def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()


class Rect:
    def __init__(self, left, bottom, width, height):
        self.mLeft, self.mBottom = left, bottom
        self.mRight, self.mTop = left + width, bottom + height

    def getWidth(self):
        return self.mRight - self.mLeft

    def getHeight(self):
        return self.mTop - self.mBottom

    def apply(self, args):
        # Execute the actual LLRect assignment statements, preserving their
        # absolute (=) or relative (+=) semantics rather than copying a model.
        scope = dict(zip(("horiz", "vertical"), args), self=self)
        for line in body(RECT_SOURCE, "LLRectBase& translate(").splitlines():
            line = line.strip()
            if line.startswith("m"):
                exec(re.sub(r"\bm(Left|Right|Top|Bottom)\b", r"self.m\1",
                            line.rstrip(";")), {}, scope)


def layout(star, width, height, arrow_width, icon_pad):
    production = body(SOURCE, "void LLLocationInputCtrl::updateWidgetlayout()")
    match = re.search(r"al_btn_rect\.translate\((.*?)\);",
                      production, re.S)
    expressions = match[1].replace("/", "//").split(",")
    scope = {"al_btn_rect": star, "rect": Rect(0, 0, width, height),
             "hist_btn_rect": Rect(width - arrow_width, 0, arrow_width, height),
             "mIconHPad": icon_pad}
    args = [eval(expression.strip(), {}, scope) for expression in expressions]
    star.apply(args)


def run_callback(production, owner, **args):
    """Execute the production setter/hook bodies (their small C++ subset)."""
    lines, indent = ["def callback(self, width=None, delta_left=None):"], 1
    for line in production.splitlines():
        line = line.strip()
        if not line or line.startswith("//") or line == "{":
            continue
        if line == "}":
            indent -= 1
            continue
        line = re.sub(r"^const S32 ", "", line.rstrip(";")).replace("||", " or ")
        line = line.replace("mButton->getRect()", "self.dropdown")
        line = line.replace("mAddLandmarkBtn->translate", "self.translateStar")
        for name in ("mVkArrowImageWidth", "imageLoaded", "onVkArrowImageWidthChanged",
                     "updateWidgetlayout", "refreshParcelIcons"):
            line = re.sub(r"\b" + name + r"\b", "self." + name, line)
        if line.startswith("if ("):
            line = "if " + line[3:] + ":"
            lines.append("    " * indent + line)
            indent += 1
        else:
            lines.append("    " * indent + line)
    scope = {}
    exec("\n".join(lines), {}, scope)
    scope["callback"](owner, **args)


class DeferredLayout:
    def __init__(self, width, height, image_width=0, left=-3, bottom=1):
        self.rect = Rect(0, 0, width, height)
        self.star = Rect(left, bottom, 18, 18)
        self.mVkArrowImageWidth = image_width
        self.notifications = self.parcel_updates = 0
        self.imageLoaded()
        self.updateWidgetlayout()

    def imageLoaded(self):
        # The native configuration has no LLUIImage. Evaluate the real base
        # callback's dropdown rectangle and editor-width expressions.
        production = body(COMBO_SOURCE, "void LLComboBox::imageLoaded()")
        expression = re.search(r"mButton->setRect\(LLRect\((.*?)\)\);",
                               production, re.S)[1]
        expression = expression.replace("getRect()", "rect")
        scope = {"rect": self.rect, "arrow_width": self.mVkArrowImageWidth,
                 "shadow_size": SHADOW, "llmax": max}
        left, top, right, bottom = eval("(" + expression + ")", {}, scope)
        self.dropdown = Rect(left, bottom, right - left, top - bottom)
        deduction = re.search(r"text_entry_rect.mRight -= (.*?);", production)[1]
        self.editor_right = self.rect.mRight - eval(
            deduction, {}, dict(scope, BTN_DROP_SHADOW=SHADOW))

    def updateWidgetlayout(self):
        layout(self.star, self.rect.getWidth(), self.rect.getHeight(),
               self.dropdown.getWidth(), 2)

    def translateStar(self, dx, dy):
        self.star.apply((dx, dy))

    def refreshParcelIcons(self):
        self.parcel_updates += 1
        production = body(SOURCE, "void LLLocationInputCtrl::refreshParcelIcons()")
        self.icon_cursor = eval(re.search(r"S32 x = (.*?);", production)[1]
                                .replace("mAddLandmarkBtn->getRect()", "star"),
                                {}, {"star": self.star})
        expression = re.search(r"right_pad = (.*?);", production)[1]
        self.right_padding = eval(expression.replace("mTextEntry->getRect().mRight",
                                                     "editor_right"), {},
                                  {"editor_right": self.editor_right, "x": self.icon_cursor})

    def onVkArrowImageWidthChanged(self, delta_left):
        self.notifications += 1
        run_callback(body(SOURCE, "void LLLocationInputCtrl::onVkArrowImageWidthChanged("),
                     self, delta_left=delta_left)

    def set_width(self, width, omit_notification=False):
        production = body(COMBO_SOURCE, "void LLComboBox::setVkArrowImageWidth(")
        if omit_notification:
            production = production.replace("onVkArrowImageWidthChanged(delta_left);", "pass;")
        run_callback(production, self, width=width)


class LocationLayoutTest(unittest.TestCase):
    def test_deferred_width_callback_sequence(self):
        for width in (120, 240, 580):
            for height in (23, 31):
                for scale in (0.75, 1, 1.25, 1.5, 2):
                    owner = DeferredLayout(width, height)
                    for image_width in (16, 16, 24, 8, 0, -1, 8, 18, 16):
                        previous, count = owner.mVkArrowImageWidth, owner.notifications
                        old_left = owner.dropdown.mLeft
                        owner.set_width(image_width)
                        effective = image_width if image_width > 0 else previous
                        oracle = DeferredLayout(width, height, effective)
                        changed = owner.dropdown.mLeft != old_left
                        self.assertEqual(owner.notifications, count + changed)
                        self.assertEqual(vars(owner.star), vars(oracle.star))
                        self.assertLess(owner.star.mRight * scale, owner.dropdown.mLeft * scale)
                        self.assertGreaterEqual(owner.star.mLeft * scale, 0)
                        self.assertEqual(owner.star.mBottom, oracle.star.mBottom)
                        if changed:
                            self.assertEqual(owner.parcel_updates, owner.notifications)
                            self.assertEqual(owner.icon_cursor, owner.star.mLeft)
                            self.assertEqual(owner.right_padding,
                                             owner.editor_right - owner.star.mLeft)

    def test_missing_notification_reproduces_overlap(self):
        owner = DeferredLayout(580, 23)
        gap = owner.dropdown.mLeft - owner.star.mRight
        owner.set_width(16, omit_notification=True)
        self.assertEqual(owner.dropdown.mLeft - owner.star.mRight, gap - 8)
        self.assertGreater(owner.star.mRight, owner.dropdown.mLeft)

    def test_width_resize_preserves_release_follow_offsets(self):
        owner = DeferredLayout(580, 23)
        owner.set_width(24)
        for width in (120, 580, 240, 120):
            # Right-follow behavior is the release contract; don't call its
            # constructor-only layout a second time during resize.
            dx = width - owner.rect.getWidth()
            owner.star.apply((dx, 0))
            owner.rect = Rect(0, 0, width, 23)
            owner.imageLoaded()
            oracle = DeferredLayout(width, 23, owner.mVkArrowImageWidth)
            self.assertEqual(vars(owner.star), vars(oracle.star))
            owner.set_width(16)
            self.assertEqual(vars(owner.star), vars(DeferredLayout(width, 23, 16).star))

    def test_renderer_classifies_only_owned_dropdown(self):
        expression = re.search(r"const bool is_combo_button = (.*?);", VK_SOURCE, re.S)[1]
        expression = expression.replace("nullptr", "None").replace("&&", "and").replace("->", ".")

        class Combo:
            def __init__(self, editable):
                self.editable, self.mButton = editable, object()

            def acceptsTextInput(self):
                return self.editable

            def getDropdownButton(self):
                getter = re.search(r"getDropdownButton\(\) const\s*\{\s*return (.*?);", COMBO_HEADER)[1]
                return eval(getter, {}, vars(self))

        for parent_combo in (None, Combo(False), Combo(True)):
            owned = parent_combo.mButton if parent_combo else object()
            for button in (owned, object(), object(), object(), object()):
                actual = bool(eval("(" + expression + ")", {}, {"parent_combo": parent_combo, "button": button}))
                self.assertEqual(actual, parent_combo is not None and
                                 parent_combo.editable and button is owned)

    def test_native_width_is_prepared_before_children(self):
        prepare = body(VK_SOURCE, "void prepareView(")
        self.assertEqual(VK_SOURCE.count("setVkArrowImageWidth("), 1)
        self.assertLess(prepare.index("combo->setVkArrowImageWidth("),
                        prepare.index("for (LLView::child_list_const_iter_t"))
        self.assertIn("combo->getDropdownButton()", prepare)
        native_hook = body(SOURCE, "void LLLocationInputCtrl::onVkArrowImageWidthChanged(")
        self.assertNotIn("refreshMaturityButton", native_hook)

    def test_release_widget_xui_and_address_bar_geometry(self):
        skins = ROOT / "indra/newview/skins"
        for path in skins.glob("*/xui/en/widgets/location_input.xml"):
            skin = path.relative_to(skins).parts[0]
            self.assertEqual(digest(path.read_text()), WIDGET_DIGESTS[skin], str(path))
            widget = ET.parse(path).getroot()
            button = widget.find("add_landmark_button")
            for width in (120, 240, 580):
                for height in (23, 31):
                    for arrow in (8, 16, 24):
                        for scale in (0.75, 1.0, 1.25, 1.5, 2.0):
                            with self.subTest(skin=path.parts[-5], width=width,
                                              height=height, arrow=arrow, scale=scale):
                                w, h = int(button.get("width")), int(button.get("height"))
                                self.assertEqual((w, h), (18, 18))
                                left = int(button.get("left"))
                                bottom = int(button.get("top")) - h
                                owner = DeferredLayout(width, height, left=left, bottom=bottom)
                                owner.set_width(arrow)
                                owner.set_width(arrow)
                                oracle = DeferredLayout(width, height, arrow, left, bottom)
                                self.assertEqual(vars(owner.star), vars(oracle.star))
                                self.assertLess(owner.star.mRight * scale, owner.dropdown.mLeft * scale)
        for path in skins.glob("*/xui/en/panel_navigation_bar.xml"):
            current = ET.parse(path).getroot()
            for element in current.iter():
                element.attrib.pop("tool_tip", None)
            skin = path.relative_to(skins).parts[0]
            self.assertEqual(digest(ET.tostring(current).decode()), PANEL_DIGESTS[skin], str(path))

    def test_upstream_location_layout_functions_restored(self):
        for signature, expected in FUNCTION_DIGESTS.items():
            self.assertEqual(digest(body(SOURCE, signature)), expected, signature)


if __name__ == "__main__":
    unittest.main()
