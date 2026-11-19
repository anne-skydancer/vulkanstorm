# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The viewer reads raw J2C codestreams only: "jp2" must not map to the J2C codec.

indra/llimage/llimage.cpp maps file extensions to codecs. A .jp2 file is the JP2
box format, which the OPJ_CODEC_J2K decoder cannot read
(indra/llimagej2coj/tests/llj2c_jp2_file_openjpeg_test.cpp shows it with a real
file), so local textures and the upload preview must refuse it as an
unsupported format, which an unmapped extension (IMG_CODEC_INVALID) does.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / 'indra/llimage/llimage.cpp').read_text(encoding='utf-8')


def extension_table():
    body = re.search(r'file_extensions\[\]\s*=\s*\{(.*?)\n\};', SOURCE, re.S).group(1)
    return dict(re.findall(r'\{\s*"([^"]+)"\s*,\s*(IMG_CODEC_\w+)\s*\}', body))


class Jp2ExtensionTest(unittest.TestCase):
    def test_jp2_is_not_mapped(self):
        self.assertNotIn('jp2', extension_table())

    def test_raw_codestream_extensions_still_map_to_j2c(self):
        table = extension_table()
        self.assertEqual(table['j2c'], 'IMG_CODEC_J2C')
        self.assertEqual(table['texture'], 'IMG_CODEC_J2C')

    def test_other_codecs_unchanged(self):
        self.assertEqual(extension_table(), {
            'bmp': 'IMG_CODEC_BMP', 'tga': 'IMG_CODEC_TGA', 'j2c': 'IMG_CODEC_J2C',
            'texture': 'IMG_CODEC_J2C', 'jpg': 'IMG_CODEC_JPEG', 'jpeg': 'IMG_CODEC_JPEG',
            'mip': 'IMG_CODEC_DXT', 'dxt': 'IMG_CODEC_DXT', 'png': 'IMG_CODEC_PNG'})

    def test_local_bitmaps_do_not_take_jp2(self):
        text = (ROOT / 'indra/newview/lllocalbitmaps.cpp').read_text(encoding='utf-8')
        self.assertNotIn('"jp2"', text)
        self.assertIn('temp_exten == "j2c"', text)

    def test_unmapped_lookup_returns_invalid(self):
        # getCodecFromExtension falls through to IMG_CODEC_INVALID for any name not in the table.
        func = SOURCE[SOURCE.index('EImageCodec LLImageBase::getCodecFromExtension'):]
        func = func[:func.index('\n}\n')]
        self.assertIn('return IMG_CODEC_INVALID;', func)


if __name__ == '__main__':
    unittest.main()
