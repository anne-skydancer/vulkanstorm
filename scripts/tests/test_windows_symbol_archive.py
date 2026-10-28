"""Verify Windows symbol packaging with the actual Vulkanstorm executable name."""
import os
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "indra/newview"))
from fs_viewer_manifest import FSViewerManifest


class WindowsSymbolArchiveTest(unittest.TestCase):
    def test_packages_built_executable_when_pdbcopy_is_unavailable(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            release = root / "release"
            release.mkdir()
            files = {"vulkanstorm-bin.exe": b"viewer executable",
                     "vulkanstorm-bin.pdb": b"debug symbols",
                     "build_data.json": b'{"Type":"viewer"}'}
            for name, data in files.items():
                (release / name).write_bytes(data)
            manifest = FSViewerManifest()
            manifest.args = {"configuration": "release", "version": ["7", "2", "4", "123"],
                             "viewer_flavor": "oss"}
            manifest.address_size = 64
            manifest.fs_save_symbols = Mock()
            manifest.fs_channel_legacy_oneword = lambda: "Vulkanstorm"
            previous = Path.cwd()
            try:
                os.chdir(root)
                with patch("fs_viewer_manifest.subprocess.check_call", side_effect=FileNotFoundError):
                    manifest.fs_save_windows_symbols()
            finally:
                os.chdir(previous)
            archives = list(release.glob("*.tar.xz"))
            self.assertEqual(len(archives), 1)
            with tarfile.open(archives[0]) as archive:
                self.assertEqual(set(archive.getnames()), set(files))
                for name, data in files.items():
                    self.assertEqual(archive.extractfile(name).read(), data)


if __name__ == "__main__":
    unittest.main()
