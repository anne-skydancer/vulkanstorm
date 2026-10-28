"""Headless XML/source contracts; not a viewer hover or native C++ execution test."""
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
SETTINGS = ("FSDisableLabeledChatLinks", "FSDisableLabeledChatLinksNearbyChat")


def source(path):
    return (ROOT / path).read_text(encoding="utf-8")


def method(text, begin, end):
    return text[text.index(begin):text.index(end, text.index(begin))]


class ChatLinkDefaultsTest(unittest.TestCase):
    def test_new_profile_defaults_are_enabled_and_persisted(self):
        settings = ET.parse(ROOT / "indra/newview/app_settings/settings.xml").getroot()
        children = list(settings.find("map"))
        entries = dict(zip((key.text for key in children[::2]), children[1::2]))
        for name in SETTINGS:
            with self.subTest(setting=name):
                fields = list(entries[name])
                values = dict(zip((key.text for key in fields[::2]),
                                  (value.text for value in fields[1::2])))
                self.assertEqual(values["Type"], "Boolean")
                self.assertEqual(values["Value"], "1")
                self.assertEqual(values["Persist"], "1")
                self.assertIn("supplied link label is displayed", values["Comment"])
                self.assertIn("real destination URL", values["Comment"])

    def test_label_and_destination_have_separate_rendering_paths(self):
        entry = source("indra/llui/llurlentry.cpp")
        label = method(entry, "std::string LLUrlEntryHTTPLabel::getLabel(",
                       "std::string LLUrlEntryHTTPLabel::getTooltip(")
        self.assertIn("getLabelFromWikiLink(url)", label)
        self.assertIn("? label : getUrl(url)", label)
        self.assertNotIn("FSDisableLabeledChatLinks", label)
        tooltip = method(entry, "std::string LLUrlEntryHTTPLabel::getTooltip(",
                         "// <FS:Ansariel> Allow URLs")
        self.assertIn("return getUrl(string);", tooltip)
        self.assertIn("return getUrlFromWikiLink(string);", tooltip)
        registry = source("indra/llui/llurlregistry.cpp")
        self.assertIn("match_entry->getLabel(url, cb)", registry)
        self.assertIn("match_entry->getTooltip(url)", registry)
        text = source("indra/llui/lltextbase.cpp")
        rendering = method(text, "            // output the styled Url",
                           "            // show query part of url")
        self.assertIn("appendAndHighlightTextImpl(match.getLabel(), part, link_params,", rendering)
        self.assertIn("setLastSegmentToolTip(match.getTooltip());", rendering)
        self.assertRegex(rendering, r"if \(match.getLabeledLinkMasked\(\)\)\s*\{\s*"
                         r"setLastSegmentProminentUrlTooltip\(match.getLabel\(\), "
                         r"match.getLabeledLinkTrusted\(\)\);")
        hover = method(text, "bool LLNormalTextSegment::handleToolTip(",
                       "void LLNormalTextSegment::setToolTip(")
        self.assertIn("if (mForceProminentUrlTooltip && !mTooltip.empty())", hover)
        self.assertIn("params.delay_time(0.f);", hover)
        self.assertIn(".text(mTooltip).style.color(", hover)
        self.assertIn("LLToolTipMgr::instance().show(params);", hover)

    def test_protection_honors_each_contexts_opt_out(self):
        registry = source("indra/llui/llurlregistry.cpp")
        for name in SETTINGS:
            self.assertRegex(registry, rf'LLUICachedControl<bool> \w+\("{name}", false\)')
        self.assertIn("(is_nearby_chat ? sDisableLabeledLinksNearby : sDisableLabeledLinks)", registry)
        self.assertRegex(registry, r"if \(!is_content_trusted && "
                         r"\(match_entry == mUrlEntryHTTPLabel\).*"
                         r"match.getLabel\(\) != match.getUrl\(\)\)\s*\{\s*"
                         r"match.setLabeledLinkMasked\(true\);")
        panel = ET.parse(ROOT / "indra/newview/skins/default/xui/en/panel_preferences_firestorm.xml")
        for name in SETTINGS:
            control = panel.find(f".//check_box[@name='{name}']")
            self.assertEqual(control.get("control_name"), name)

    def test_saved_values_are_loaded_separately_from_defaults(self):
        control = source("indra/llxml/llcontrol.cpp")
        loading = control[control.index("U32 LLControlGroup::loadFromFile("):]
        self.assertRegex(loading, r"if\(set_default_values\)\s*\{")
        self.assertIn('existing_control->setDefaultValue(control_map["Value"]);', loading)
        saved = loading[loading.index("else if(existing_control->isPersisted())"):]
        self.assertIn('existing_control->setValue(control_map["Value"], save_values);', saved)
        self.assertNotIn('control_map["Value"].asBoolean()', saved)

    def test_preference_descriptions_do_not_claim_protection_is_disabled_by_default(self):
        for locale, stale_claim in {
            "en": "Disabled by default",
            "de": "Standardmäßig deaktiviert",
            "fr": "Désactivée par défaut",
            "ja": "デフォルトでは無効になっています",
            "pl": "Domyślnie wyłączone",
            "zh": "預設關閉",
        }.items():
            with self.subTest(locale=locale):
                panel = ET.parse(ROOT / "indra/newview/skins/default/xui" / locale /
                                 "panel_preferences_firestorm.xml")
                for name in SETTINGS:
                    control = panel.find(f".//check_box[@name='{name}']")
                    self.assertNotIn(stale_claim, control.get("tool_tip"))


if __name__ == "__main__":
    unittest.main()
