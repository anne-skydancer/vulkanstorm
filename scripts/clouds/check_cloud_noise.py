#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Check a cloud_noise.vkcn (the volumetric clouds' noise file, indra/newview/llcloudnoise.h).

  check_cloud_noise.py FILE [--expect SHA256 | --expect-from-cmake CloudNoise.cmake] [--licences DIR]

Checks the container structure and every entry's SHA-256 as the viewer does at start-up, the whole-file SHA-256
against the expected one (by default the CLOUD_NOISE_SHA256 of indra/cmake/CloudNoise.cmake), and with --licences
that the licence texts of the noise are in DIR. Exit code 0 when everything is right, 1 otherwise.
"""
import argparse
import hashlib
import re
import struct
import sys
from pathlib import Path

MAGIC = b'VKCLDNZ1'
FORMAT_VERSION = 1
HEADER_BYTES = 256
ENTRY_COUNT = 3
FIRST_ENTRY = 52
ENTRY_BYTES = 68
RGBA8_UNORM = 1
# kind, width, height, depth
ENTRIES = ((1, 128, 128, 128), (2, 32, 32, 32), (3, 128, 128, 1))
FILE_BYTES = HEADER_BYTES + sum(4 * w * h * d for _, w, h, d in ENTRIES)
LICENCES = ('TileableVolumeNoise.txt', 'glm.txt', 'cloudnoise.txt')
DEFAULT_CMAKE = Path(__file__).resolve().parents[2] / 'indra/cmake/CloudNoise.cmake'


class NoiseError(Exception):
    pass


def expected_from_cmake(path):
    """CLOUD_NOISE_SHA256 of indra/cmake/CloudNoise.cmake."""
    match = re.search(r'^set\(CLOUD_NOISE_SHA256 "([0-9a-f]{64})"\)', Path(path).read_text(encoding='utf-8'), re.M)
    if not match:
        raise NoiseError('no CLOUD_NOISE_SHA256 in %s' % path)
    return match.group(1)


def parse(data):
    """Raise NoiseError unless `data` is a valid container; the same checks as LLCloudNoise::parse."""
    if len(data) < HEADER_BYTES:
        raise NoiseError('file too small')
    if data[:8] != MAGIC:
        raise NoiseError('bad magic')
    version, header_bytes, count = struct.unpack_from('<III', data, 8)
    if version != FORMAT_VERSION:
        raise NoiseError('unsupported format version %d' % version)
    if header_bytes != HEADER_BYTES or count != ENTRY_COUNT:
        raise NoiseError('bad header')
    if len(data) != FILE_BYTES:
        raise NoiseError('wrong file size %d (expected %d)' % (len(data), FILE_BYTES))
    next_offset = HEADER_BYTES
    for index, (kind, w, h, d) in enumerate(ENTRIES):
        base = FIRST_ENTRY + index * ENTRY_BYTES
        e_kind, e_format, e_w, e_h, e_d, offset, length = struct.unpack_from('<IIIIIQQ', data, base)
        digest = data[base + 36:base + 68]
        if (e_kind, e_format, e_w, e_h, e_d) != (kind, RGBA8_UNORM, w, h, d):
            raise NoiseError('entry %d: unexpected kind, format or size' % index)
        size = 4 * w * h * d
        if offset != next_offset or length != size or offset + length > len(data):
            raise NoiseError('entry %d: bad offset or length' % index)
        if hashlib.sha256(data[offset:offset + length]).digest() != digest:
            raise NoiseError('entry %d: SHA-256 mismatch' % index)
        next_offset += size
    if next_offset != len(data):
        raise NoiseError('trailing bytes')


def check(path, expect, licences=None):
    data = Path(path).read_bytes()
    actual = hashlib.sha256(data).hexdigest()
    if actual != expect:
        raise NoiseError('SHA-256 %s is not the expected %s' % (actual, expect))
    parse(data)
    if licences is not None:
        for name in LICENCES:
            licence = Path(licences) / name
            if not licence.is_file() or licence.stat().st_size == 0:
                raise NoiseError('missing licence text %s' % licence)
    return actual


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('file')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--expect', help='the expected whole-file SHA-256')
    group.add_argument('--expect-from-cmake', default=str(DEFAULT_CMAKE),
                       help='read CLOUD_NOISE_SHA256 from this CloudNoise.cmake (default: the source tree\'s)')
    parser.add_argument('--licences', help='a directory that must hold the licence texts of the noise')
    args = parser.parse_args(argv)
    try:
        expect = args.expect or expected_from_cmake(args.expect_from_cmake)
        digest = check(args.file, expect.lower(), args.licences)
    except (NoiseError, OSError) as error:
        print('cloud noise check FAILED: %s' % error, file=sys.stderr)
        return 1
    print('cloud noise ok: %s %s' % (digest, args.file))
    return 0


if __name__ == '__main__':
    sys.exit(main())
