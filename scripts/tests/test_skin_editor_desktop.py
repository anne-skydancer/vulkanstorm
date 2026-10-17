"""Standalone desktop packaging regression tests."""

import hashlib
import importlib.util
from io import BytesIO
import os
from pathlib import Path
import tarfile
import tempfile
import unittest
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "skin_editor_package", ROOT / "tools" / "skin-editor" / "desktop" / "package.py"
)
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


class DesktopPackageTests(unittest.TestCase):
    def test_python_dependency_notices_are_included(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "notices.tar.gz"
            with tarfile.open(source, "w:gz") as archive:
                entry = tarfile.TarInfo("python-build-standalone-test/LICENSE.zlib.txt")
                entry.size = 6
                archive.addfile(entry, BytesIO(b"notice"))
            package.copy_python_notices(source, root / "licenses", "test")
            self.assertEqual((root / "licenses" / "LICENSE.zlib.txt").read_bytes(), b"notice")

    def test_zip_accepts_reproducible_runtime_timestamps(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stage = root / "skin-studio"
            stage.mkdir()
            executable = stage / "python.exe"
            executable.write_bytes(b"runtime")
            os.utime(executable, (0, 0))
            output = root / "app.zip"
            package.create_archive(stage, output, "windows")
            with ZipFile(output) as archive:
                self.assertEqual(archive.read("skin-studio/python.exe"), b"runtime")
                self.assertEqual(archive.getinfo("skin-studio/python.exe").date_time[0], 1980)

    def test_runtime_checksum_is_required(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "python.tar.gz"
            archive.write_bytes(b"runtime")
            package.verify_runtime(archive, hashlib.sha256(b"runtime").hexdigest())
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                package.verify_runtime(archive, "0" * 64)

    def test_runtime_extraction_rejects_traversal_and_external_links(self):
        for name, link in (("../escape", None), ("python/../../escape", None),
                           ("python/link", "../../escape")):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / "runtime.tar.gz"
                with tarfile.open(archive, "w:gz") as output:
                    entry = tarfile.TarInfo(name)
                    if link:
                        entry.type = tarfile.SYMTYPE
                        entry.linkname = link
                        output.addfile(entry)
                    else:
                        entry.size = 1
                        output.addfile(entry, BytesIO(b"x"))
                with self.assertRaises((ValueError, tarfile.FilterError)):
                    package.unpack_runtime(archive, root / "bundle")
                self.assertFalse((root / "escape").exists())

    def test_runtime_preserves_internal_symlinks_and_executable_permissions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / "runtime.tar.gz"
            with tarfile.open(archive, "w:gz") as output:
                entry = tarfile.TarInfo("python/bin/python3.13")
                entry.size = 3
                entry.mode = 0o755
                output.addfile(entry, BytesIO(b"exe"))
                entry = tarfile.TarInfo("python/bin/python3")
                entry.type = tarfile.SYMTYPE
                entry.linkname = "python3.13"
                output.addfile(entry)
            package.unpack_runtime(archive, root / "bundle")
            self.assertEqual((root / "bundle" / "python" / "bin" / "python3").read_bytes(), b"exe")

    def test_staged_library_is_complete_and_independent_of_checkout(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory)
            package.stage_assets(destination)
            self.assertFalse(list(destination.rglob("*.pyc")))
            spec = importlib.util.spec_from_file_location(
                "bundled_skin_editor", destination / "bundle" / "editor" / "editor.py"
            )
            editor = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(editor)
            library = editor.SkinLibrary(destination / "bundle" / "library")
            for skin in library.catalog:
                for theme in skin["themes"]:
                    with self.subTest(skin=skin["folder"], theme=theme["folder"]):
                        library.validate(library.project(skin["folder"], theme["folder"]))
            self.assertTrue((destination / "licenses" / "LICENSE-logos.txt").is_file())
            self.assertTrue((destination / "source" / "desktop" / "Cargo.lock").is_file())


if __name__ == "__main__":
    unittest.main()
