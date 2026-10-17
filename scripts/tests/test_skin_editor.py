"""Native skin export and loopback API regression tests."""

from copy import deepcopy
from http.client import HTTPConnection
from html.parser import HTMLParser
import importlib.util
from io import BytesIO
import json
from pathlib import Path
import struct
import tempfile
from threading import Thread
import unittest
import xml.etree.ElementTree as ET
from zipfile import ZipFile
import zlib

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("skin_editor", ROOT / "tools" / "skin-editor" / "editor.py")
editor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(editor)


class SkinEditorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.library = editor.SkinLibrary(ROOT)

    def setUp(self):
        self.project = self.library.project("ansastorm_modern", "")

    def test_all_registered_themes_validate_and_export(self):
        class PreviewRoles(HTMLParser):
            roles = set()

            def handle_starttag(self, tag, attrs):
                self.roles.update(value for key, value in attrs
                                  if key in ("data-color", "data-background", "data-border"))

        preview = PreviewRoles()
        preview.feed((ROOT / "tools" / "skin-editor" / "index.html").read_text())
        for skin in self.library.catalog:
            for theme in skin["themes"]:
                with self.subTest(skin=skin["folder"], theme=theme["folder"]):
                    project = self.library.project(skin["folder"], theme["folder"])
                    self.assertFalse(preview.roles - project["colors"].keys())
                    self.library.validate(project)
                    with ZipFile(BytesIO(self.library.export(project))) as archive:
                        self.assertIn("skins/my_skin/colors.xml", archive.namelist())
                        self.assertTrue(archive.testzip() is None)

    def test_palette_and_commands_round_trip(self):
        self.project["colors"]["EmphasisColor"] = [0.12, 0.34, 0.56, 0.78]
        roles = ("HTMLLinkColor", "ScriptErrorColor", "NameTagSelf", "NameTagFriend",
                 "UserChatColor", "FriendsChatColor", "ButtonFlashBgColor")
        for index, role in enumerate(roles):
            self.project["colors"][role] = [index / 10, 0.23, 0.67, 0.45]
        for side in editor.SIDES:
            self.project["toolbars"][side]["commands"] = []
        self.project["toolbars"]["left"].update({
            "commands": ["inventory", "chat"], "button_alignment": "bottom",
            "button_display_mode": "text_only", "button_layout_style": "equalize",
        })
        self.project["name"] = 'Custom <Blue> & "Gold"'
        with ZipFile(BytesIO(self.library.export(self.project))) as archive:
            palette = ET.fromstring(archive.read("skins/my_skin/colors.xml"))
            color = palette.find("./color[@name='EmphasisColor']")
            self.assertEqual([float(v) for v in color.attrib["value"].split()],
                             self.project["colors"]["EmphasisColor"])
            for role in roles:
                node = palette.find(f"./color[@name='{role}']")
                self.assertEqual([float(v) for v in node.attrib["value"].split()],
                                 self.project["colors"][role])
            toolbar = ET.fromstring(archive.read("skins/my_skin/toolbars.xml")).find("left_toolbar")
            self.assertEqual([c.attrib["name"] for c in toolbar], ["inventory", "chat"])
            self.assertEqual(toolbar.attrib["button_alignment"], "bottom")
            self.assertEqual(toolbar.attrib["button_display_mode"], "text_only")
            self.assertEqual(toolbar.attrib["button_layout_style"], "equalize")
            registration = editor.llsd(ET.fromstring(archive.read("skin-registration.xml"))[0])
            self.assertEqual(registration[0]["name"], self.project["name"])
            self.assertEqual(json.loads(archive.read("project.json")), self.project)

    def test_custom_tab_highlight_exports_native_assets(self):
        for skin in ("default", "ansastorm_modern"):
            with self.subTest(skin=skin):
                project = self.library.project(skin, "")
                project["tab_highlight"] = [0.2, 0.4, 0.6, 0.8]
                widget_path = "xui/en/widgets/tab_container.xml"
                base, _ = self.library.selection(skin, "")
                source = base / widget_path
                if not source.exists():
                    source = self.library.skins / "default" / widget_path
                before = source.read_bytes()
                with ZipFile(BytesIO(self.library.export(project))) as archive:
                    prefix = "skins/my_skin/"
                    self.assertEqual(len(archive.namelist()), len(set(archive.namelist())))
                    widget = ET.fromstring(archive.read(prefix + widget_path))
                    original_widget = ET.fromstring(before)
                    self.assertEqual(widget.attrib, original_widget.attrib)
                    for position in ("first_tab", "middle_tab", "last_tab"):
                        for edge in ("top", "bottom", "left"):
                            self.assertEqual(widget.find(position).get(f"tab_{edge}_image_selected"),
                                             "skin_studio_selected_tab.png")
                            attribute = f"tab_{edge}_image_unselected"
                            self.assertEqual(widget.find(position).get(attribute),
                                             original_widget.find(position).get(attribute))
                    image = archive.read(prefix + "textures/skin_studio_selected_tab.png")
                    self.assertEqual(image[:8], b"\x89PNG\r\n\x1a\n")
                    offset, chunks = 8, {}
                    while offset < len(image):
                        length, = struct.unpack(">I", image[offset:offset + 4])
                        kind = image[offset + 4:offset + 8]
                        data = image[offset + 8:offset + 8 + length]
                        crc, = struct.unpack(">I", image[offset + 8 + length:offset + 12 + length])
                        self.assertEqual(crc, zlib.crc32(kind + data))
                        chunks[kind] = data
                        offset += length + 12
                    self.assertEqual(struct.unpack(">IIBBBBB", chunks[b"IHDR"]),
                                     (16, 16, 8, 6, 0, 0, 0))
                    self.assertEqual(zlib.decompress(chunks[b"IDAT"]),
                                     (b"\0" + bytes([51, 102, 153, 204]) * 16) * 16)
                    self.assertEqual(json.loads(archive.read("project.json")), project)
                self.assertEqual(source.read_bytes(), before)

    def test_old_projects_and_disabled_tab_highlight_keep_source_artwork(self):
        del self.project["tab_highlight"]
        for project in (self.project, {**self.project, "tab_highlight": None}):
            self.library.validate(project)
            with ZipFile(BytesIO(self.library.export(project))) as archive:
                self.assertNotIn("skins/my_skin/textures/skin_studio_selected_tab.png",
                                 archive.namelist())
                base, _ = self.library.selection(project["skin"], project["theme"])
                relative = "xui/en/widgets/tab_container.xml"
                self.assertEqual(archive.read("skins/my_skin/" + relative),
                                 (base / relative).read_bytes())

    def test_theme_assets_are_flattened_without_source_changes(self):
        project = self.library.project("firestorm", "dark")
        base, theme = self.library.selection("firestorm", "dark")
        before = (base / "colors.xml").read_bytes()
        with ZipFile(BytesIO(self.library.export(project))) as archive:
            self.assertFalse(any("/themes/" in name for name in archive.namelist()))
            for path in theme.rglob("*"):
                if path.is_file() and path.name not in ("colors.xml", "toolbars.xml"):
                    target = "skins/my_skin/" + path.relative_to(theme).as_posix()
                    self.assertEqual(archive.read(target), path.read_bytes())
        self.assertEqual((base / "colors.xml").read_bytes(), before)

    def test_source_warnings_are_explicit(self):
        self.assertTrue(any("facebook" in warning for warning in self.project["warnings"]))
        self.assertNotIn("facebook", self.project["toolbars"]["bottom"]["commands"])
        project = self.library.project("starlight", "original_orange")
        self.assertTrue(any("EmphasisColor35" in warning for warning in project["warnings"]))
        self.assertTrue(any("invalid color" in warning for warning in project["warnings"]))

    def test_missing_alignment_uses_native_center_default(self):
        project = self.library.project("default", "")
        self.assertEqual(project["toolbars"]["left"]["button_alignment"], "center")

    def test_palette_layer_order_and_invalid_references(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            for name in ("a", "b"):
                (base / name).mkdir()
            (base / "a" / "colors.xml").write_text(
                '<colors><color name="Base" value=".1 .2 .3 1"/>'
                '<color name="Alias" reference="Base"/></colors>')
            (base / "b" / "colors.xml").write_text(
                '<colors><color name="Base" value=".4 .5 .6 1"/>'
                '<color name="Chain" reference="Alias"/>'
                '<color name="Cycle1" reference="Cycle2"/>'
                '<color name="Cycle2" reference="Cycle1"/>'
                '<color name="Missing" reference="NoSuchColor"/></colors>')
            warnings = []
            colors = self.library.palette([base / "a", base / "b"], warnings)
            self.assertEqual(colors["Base"], [.4, .5, .6, 1])
            self.assertEqual(colors["Chain"], [.1, .2, .3, 1])
            self.assertEqual(len(warnings), 3)
            self.assertNotIn("Missing", colors)
            self.assertNotIn("Cycle1", colors)

    def test_reject_invalid_projects(self):
        cases = [
            ("folder", "../escape"), ("folder", "default"), ("folder", "con"),
            ("name", ""), ("skin", "../default"), ("theme", "../dark"),
            ("colors", {}), ("toolbars", []), ("warnings", "bad"),
            ("tab_highlight", [1, 0, 0]), ("tab_highlight", [1, 0, 0, 2]),
            ("tab_highlight", [True, 0, 0, 1]), ("tab_highlight", {}),
            ("tab_highlight", [float("nan"), 0, 0, 1]),
        ]
        for key, value in cases:
            with self.subTest(key=key, value=value):
                project = deepcopy(self.project)
                project[key] = value
                with self.assertRaises(ValueError):
                    self.library.validate(project)
        for value in ([0, 0, 0], [0, 0, 0, 2], [0, 0, 0, float("nan")], [True, 0, 0, 1]):
            with self.subTest(color=value):
                project = deepcopy(self.project)
                project["colors"]["White"] = value
                with self.assertRaises(ValueError):
                    self.library.validate(project)
        for key, value in (("commands", ["not_a_command"]),
                           ("commands", ["chat", "chat"]), ("button_display_mode", "bad")):
            project = deepcopy(self.project)
            project["toolbars"]["bottom"][key] = value
            with self.assertRaises(ValueError):
                self.library.validate(project)


class SkinEditorHTTPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.library = editor.SkinLibrary(ROOT)
        cls.server = editor.ThreadingHTTPServer(("127.0.0.1", 0), editor.make_handler(cls.library))
        cls.thread = Thread(target=cls.server.serve_forever)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def request(self, method, path, body=None, headers=None):
        connection = HTTPConnection("127.0.0.1", self.server.server_port, timeout=120)
        try:
            connection.request(method, path, body=body, headers=headers or {})
            response = connection.getresponse()
            return response.status, response.read(), dict(response.getheaders())
        finally:
            connection.close()

    def test_static_and_api(self):
        for path in ("/", "/editor.js", "/editor.css", "/api/catalog",
                     "/api/project?skin=firestorm&theme=dark"):
            with self.subTest(path=path):
                status, content, headers = self.request("GET", path)
                self.assertEqual(status, 200)
                self.assertTrue(content)
                self.assertIn("frame-ancestors 'none'", headers["Content-Security-Policy"])
        self.assertEqual(self.request("GET", "/../editor.py")[0], 404)
        self.assertEqual(self.request("GET", "/api/project?skin=../../secret")[0], 400)

    def test_export_and_validation(self):
        body = json.dumps(self.library.project("default", ""))
        status, content, _ = self.request("POST", "/api/export", body)
        self.assertEqual(status, 200)
        self.assertIn("INSTALL.txt", ZipFile(BytesIO(content)).namelist())
        self.assertEqual(self.request("POST", "/api/validate", body)[0], 200)
        for body in ("{", "null", '{"version":1}', "[]"):
            self.assertEqual(self.request("POST", "/api/export", body)[0], 400)

    def test_reject_cross_origin_and_host(self):
        self.assertEqual(self.request("GET", "/api/catalog", headers={"Host": "attacker.invalid"})[0], 403)
        self.assertEqual(self.request("POST", "/api/export", "{}",
                                      {"Origin": "https://attacker.invalid"})[0], 403)


if __name__ == "__main__":
    unittest.main()
