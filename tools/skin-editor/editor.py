"""Local, dependency-free skin and toolbar authoring tool."""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
import json
import math
from pathlib import Path
import re
import struct
from urllib.parse import parse_qs, urlsplit
import xml.etree.ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile
import zlib

STATIC = Path(__file__).resolve().parent
ROOT = STATIC / "library"
SIDES = ("left", "right", "bottom")
OPTIONS = {
    "button_display_mode": ("icons_only", "icons_with_text", "text_only"),
    "button_alignment": ("left", "top", "center", "right", "bottom"),
    "button_layout_style": ("none", "equalize", "fill"),
}


def xml_bytes(root: ET.Element) -> bytes:
    ET.indent(root)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def llsd(node: ET.Element):
    if node.tag == "map":
        children = list(node)
        return {children[i].text: llsd(children[i + 1])
                for i in range(0, len(children), 2)}
    if node.tag == "array":
        return [llsd(child) for child in node]
    return node.text or ""


def rgba(value: object) -> list[float]:
    if not isinstance(value, list) or len(value) != 4:
        raise ValueError("A color must contain four RGBA numbers.")
    if any(type(v) not in (int, float) or not math.isfinite(v) or not 0 <= v <= 1
           for v in value):
        raise ValueError("RGBA components must be finite numbers between 0 and 1.")
    return list(value)


def tab_highlight_png(value: list[float]) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data)))

    pixel = bytes(int(v * 255 + 0.5) for v in rgba(value))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", 16, 16, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress((b"\0" + pixel * 16) * 16))
            + chunk(b"IEND", b""))


class SkinLibrary:
    def __init__(self, root: Path):
        self.skins = (root / "skins").resolve()
        self.catalog = llsd(ET.parse(self.skins / "skins.xml").getroot()[0])
        if not any(s["folder"] == "default" for s in self.catalog):
            self.catalog.append({"name": "Default", "folder": "default",
                                 "themes": [{"name": "Default", "folder": ""}]})
        commands = ET.parse(root / "commands.xml")
        strings = ET.parse(self.skins / "default" / "xui" / "en" / "strings.xml")
        labels = {n.get("name"): n.text for n in strings.getroot().findall("string")}
        self.commands = [{
            "name": n.attrib["name"],
            "label": labels.get(n.get("label_ref")) or n.attrib["name"],
            "available": n.get("available_in_toybox") == "true",
        } for n in commands.getroot().findall("command")]
        self.command_names = {c["name"] for c in self.commands}

    def selection(self, skin: str, theme: str) -> tuple[Path, Path]:
        entry = next((s for s in self.catalog if s["folder"] == skin), None)
        if entry is None or theme not in {t["folder"] for t in entry["themes"]}:
            raise ValueError("Select a registered skin and theme.")
        base = (self.skins / skin).resolve()
        selected = (base / "themes" / theme).resolve() if theme else base
        if not base.is_relative_to(self.skins.resolve()) or not selected.is_relative_to(base):
            raise ValueError("Skin path escapes the skin library.")
        return base, selected

    def palette(self, directories: list[Path], warnings: list[str]) -> dict[str, list[float]]:
        palette: dict[str, list[float]] = {}
        for directory in dict.fromkeys(directories):
            path = directory / "colors.xml"
            if not path.exists():
                continue
            references: dict[str, str] = {}
            for node in ET.parse(path).getroot().findall("color"):
                name = node.attrib["name"]
                if "value" in node.attrib:
                    try:
                        value = rgba([float(v) for v in node.attrib["value"].split()])
                    except ValueError:
                        warnings.append(f"{path.relative_to(self.skins)}: ignored invalid color "
                                        f"{name}={node.attrib['value']!r}; inherited value retained.")
                        continue
                    palette[name] = value
                elif "reference" in node.attrib:
                    references[name] = node.attrib["reference"]

            resolved: dict[str, list[float]] = {}

            def resolve(name: str, chain: set[str]) -> list[float]:
                if name in chain:
                    raise ValueError(f"Color reference cycle at {name} in {path}")
                if name in resolved:
                    return resolved[name]
                if name not in references:
                    if name not in palette:
                        raise ValueError(f"Missing color reference {name} in {path}")
                    return palette[name]
                value = resolve(references[name], chain | {name})
                resolved[name] = value.copy()
                return value

            for name in references:
                try:
                    resolve(name, set())
                except ValueError as exc:
                    warnings.append(f"{exc}; inherited value retained if present.")
            palette.update(resolved)
        return palette

    def project(self, skin: str, theme: str) -> dict:
        base, selected = self.selection(skin, theme)
        toolbar_path = base / "toolbars.xml"
        if not toolbar_path.exists():
            toolbar_path = self.skins / "default" / "toolbars.xml"
        toolbar_xml = ET.parse(toolbar_path).getroot()
        toolbars = {}
        warnings: list[str] = []
        used: set[str] = set()
        for side in SIDES:
            node = toolbar_xml.find(f"{side}_toolbar")
            toolbar = {key: values[0] for key, values in OPTIONS.items()}
            toolbar["button_alignment"] = "center"
            if node is not None:
                toolbar.update({k: node.attrib[k] for k in OPTIONS if k in node.attrib})
            toolbar["commands"] = ([] if node is None else
                                   [c.attrib["name"] for c in node.findall("command")])
            valid = []
            for command in toolbar["commands"]:
                if command not in self.command_names or command in used:
                    warnings.append(f"Ignored unavailable or duplicate source command: {command}")
                else:
                    valid.append(command)
                    used.add(command)
            toolbar["commands"] = valid
            toolbars[side] = toolbar
        return {
            "version": 1, "skin": skin, "theme": theme,
            "folder": "my_skin", "name": "My Skin",
            "tab_highlight": None,
            "colors": self.palette([self.skins / "default", base, selected], warnings),
            "toolbars": toolbars, "warnings": warnings,
        }

    def validate(self, project: object) -> dict:
        if not isinstance(project, dict) or project.get("version") != 1:
            raise ValueError("Expected a version 1 skin editor project.")
        if not isinstance(project.get("skin"), str) or not isinstance(project.get("theme"), str):
            raise ValueError("Skin and theme must be strings.")
        self.selection(project["skin"], project["theme"])
        warnings = project.get("warnings", [])
        if not isinstance(warnings, list) or any(not isinstance(w, str) for w in warnings):
            raise ValueError("Project warnings must be a list of strings.")
        folder = project.get("folder")
        if not isinstance(folder, str) or not re.fullmatch(r"[a-z][a-z0-9_]{0,63}", folder):
            raise ValueError("Folder must start with a lowercase letter and use a-z, 0-9, or _.")
        if folder.lower() in {p.name.lower() for p in self.skins.iterdir()}:
            raise ValueError("Choose a new folder name, not an existing skin.")
        if folder.lower() in {"con", "prn", "aux", "nul", *(f"com{i}" for i in range(10)),
                              *(f"lpt{i}" for i in range(10))}:
            raise ValueError("Folder name is reserved on Windows.")
        name = project.get("name")
        if not isinstance(name, str) or not name.strip() or len(name) > 100:
            raise ValueError("Skin name must contain 1 to 100 characters.")
        colors = project.get("colors")
        expected = self.project(project["skin"], project["theme"])["colors"]
        if not isinstance(colors, dict) or set(colors) != set(expected):
            raise ValueError("Project colors must match the selected skin's palette.")
        for value in colors.values():
            rgba(value)
        if project.get("tab_highlight") is not None:
            rgba(project["tab_highlight"])
        toolbars = project.get("toolbars")
        if not isinstance(toolbars, dict) or set(toolbars) != set(SIDES):
            raise ValueError("Project must contain left, right, and bottom toolbars.")
        used: set[str] = set()
        for toolbar in toolbars.values():
            if not isinstance(toolbar, dict):
                raise ValueError("Invalid toolbar.")
            for key, options in OPTIONS.items():
                if toolbar.get(key) not in options:
                    raise ValueError(f"Invalid {key}.")
            commands = toolbar.get("commands")
            if not isinstance(commands, list):
                raise ValueError("Toolbar commands must be a list.")
            for command in commands:
                if not isinstance(command, str) or command not in self.command_names:
                    raise ValueError(f"Unknown toolbar command: {command}")
                if command in used:
                    raise ValueError(f"Command appears more than once: {command}")
                used.add(command)
        return project

    def export(self, project: object) -> bytes:
        project = self.validate(project)
        base, selected = self.selection(project["skin"], project["theme"])
        files: dict[str, Path] = {}
        for directory in dict.fromkeys([base, selected]):
            for path in directory.rglob("*"):
                relative = path.relative_to(directory)
                if "themes" in relative.parts or not path.is_file():
                    continue
                if not path.resolve().is_relative_to(directory.resolve()):
                    raise ValueError(f"Asset escapes skin directory: {path}")
                files[relative.as_posix()] = path
        colors = ET.Element("colors")
        for name, value in sorted(project["colors"].items()):
            ET.SubElement(colors, "color", name=name, value=" ".join(format(v, ".9g") for v in value))
        toolbars = ET.Element("toolbars")
        for side in SIDES:
            toolbar = project["toolbars"][side]
            node = ET.SubElement(toolbars, f"{side}_toolbar",
                                 {key: toolbar[key] for key in OPTIONS})
            for command in toolbar["commands"]:
                ET.SubElement(node, "command", name=command)
        registration = ET.Element("llsd")
        entry = ET.SubElement(ET.SubElement(registration, "array"), "map")
        for key, value in (("name", project["name"]), ("folder", project["folder"])):
            ET.SubElement(entry, "key").text = key
            ET.SubElement(entry, "string").text = value
        ET.SubElement(entry, "key").text = "themes"
        theme = ET.SubElement(ET.SubElement(entry, "array"), "map")
        for key, value in (("name", "Custom"), ("folder", "")):
            ET.SubElement(theme, "key").text = key
            ET.SubElement(theme, "string").text = value
        output = BytesIO()
        generated: dict[str, bytes] = {}
        if project.get("tab_highlight") is not None:
            widget_path = "xui/en/widgets/tab_container.xml"
            widget = ET.parse(files.get(widget_path, self.skins / "default" / widget_path)).getroot()
            image = "skin_studio_selected_tab.png"
            for position in ("first_tab", "middle_tab", "last_tab"):
                node = widget.find(position)
                if node is None:
                    node = ET.SubElement(widget, position)
                for edge in ("top", "bottom", "left"):
                    node.set(f"tab_{edge}_image_selected", image)
            generated[widget_path] = xml_bytes(widget)
            generated[f"textures/{image}"] = tab_highlight_png(project["tab_highlight"])
        with ZipFile(output, "w", ZIP_DEFLATED) as archive:
            prefix = f"skins/{project['folder']}/"
            for relative, path in sorted(files.items()):
                if relative not in ("colors.xml", "toolbars.xml") and relative not in generated:
                    archive.write(path, prefix + relative)
            for relative, content in generated.items():
                archive.writestr(prefix + relative, content)
            archive.writestr(prefix + "colors.xml", xml_bytes(colors))
            archive.writestr(prefix + "toolbars.xml", xml_bytes(toolbars))
            archive.writestr("skin-registration.xml", xml_bytes(registration))
            archive.writestr("project.json", json.dumps(project, indent=2))
            archive.writestr("INSTALL.txt", INSTALL)
        return output.getvalue()


INSTALL = """Close the viewer before installing. Back up your existing settings.
Copy the exported skins/<folder> directory into the viewer's skins directory.
Merge the single <map> from skin-registration.xml into the <array> in the
existing skins/skins.xml. Do NOT replace skins.xml with the registration fragment.
Select the new skin under Preferences > Skins and restart the viewer.

The chosen source theme is flattened into the new skin. Its colors are a
resolved palette snapshot, not reference aliases. Default-skin assets remain
inherited from the target viewer; use the same viewer version as the source.

Per-account toolbars.xml overrides skin defaults. To use this layout for an
existing account, close the viewer and back up its account-specific toolbars.xml,
then replace it with the exported skin's toolbars.xml. User-settings colors.xml
can also override skin colors. No account settings are changed by the editor.
Self and friend chat colors and overhead name-tag colors are separate roles.
Friend tag coloring requires NameTagShowFriends; contact sets and individual
avatar colors may override it. Existing user color overrides still take priority.
If enabled, tab_highlight replaces selected tab backgrounds with a flat RGBA
texture through the tab_container widget defaults. It does not recolor the
original artwork. Floaters specifying their own tab images can override it.
ButtonFlashBgColor controls the default tab attention flash, not selection.
The preview is schematic: no live viewer connection or exact XUI rendering.
"""


def make_handler(library: SkinLibrary):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"
        timeout = 15

        def handle_one_request(self) -> None:
            try:
                super().handle_one_request()
            except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError) as exc:
                self.close_connection = True
                self.log_error("Client disconnected: %s", exc)

        def reply(self, status: int, content: bytes, content_type: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(content)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Content-Security-Policy",
                             "default-src 'self'; style-src 'self' 'unsafe-inline'; "
                             "img-src 'self' blob:; object-src 'none'; frame-ancestors 'none'")
            self.end_headers()
            self.wfile.write(content)

        def error(self, status: int, message: str) -> None:
            self.reply(status, json.dumps({"error": message}).encode(), "application/json")

        def trusted(self) -> bool:
            expected = f"127.0.0.1:{self.server.server_port}"
            if self.headers.get("Host") != expected:
                self.error(403, "Use the printed loopback URL.")
                return False
            origin = self.headers.get("Origin")
            if origin is not None and origin != f"http://{expected}":
                self.error(403, "Cross-origin requests are not allowed.")
                return False
            return True

        def do_GET(self) -> None:
            if not self.trusted():
                return
            request = urlsplit(self.path)
            try:
                if request.path == "/api/catalog":
                    data = {"skins": library.catalog, "commands": library.commands, "options": OPTIONS}
                    self.reply(200, json.dumps(data).encode(), "application/json")
                elif request.path == "/api/project":
                    query = parse_qs(request.query, keep_blank_values=True)
                    project = library.project(query.get("skin", ["default"])[0],
                                              query.get("theme", [""])[0])
                    self.reply(200, json.dumps(project).encode(), "application/json")
                elif request.path in ("/", "/editor.js", "/editor.css"):
                    name, mime = {"/": ("index.html", "text/html; charset=utf-8"),
                                  "/editor.js": ("editor.js", "text/javascript"),
                                  "/editor.css": ("editor.css", "text/css")}[request.path]
                    self.reply(200, (STATIC / name).read_bytes(), mime)
                else:
                    self.error(404, "Not found.")
            except (ValueError, OSError, ET.ParseError) as exc:
                self.error(400, str(exc))

        def do_POST(self) -> None:
            if not self.trusted():
                return
            if self.path not in ("/api/validate", "/api/export"):
                self.error(404, "Not found.")
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 2_000_000:
                    raise ValueError("Project request must be between 1 byte and 2 MB.")
                project = library.validate(json.loads(self.rfile.read(length)))
                if self.path == "/api/export":
                    self.reply(200, library.export(project), "application/zip")
                else:
                    self.reply(200, json.dumps(project).encode(), "application/json")
            except (ValueError, OSError, ET.ParseError) as exc:
                self.error(400, str(exc))

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--root", type=Path, default=ROOT,
                        help="Asset library directory containing skins/ and commands.xml")
    args = parser.parse_args()
    try:
        library = SkinLibrary(args.root)
        server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(library))
    except (OSError, ValueError, ET.ParseError) as exc:
        parser.exit(1, f"Cannot start skin editor: {exc}\n")
    print(f"Skin editor: http://127.0.0.1:{server.server_port} (Ctrl+C to stop)", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
