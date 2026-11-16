# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""judge_o2 of scripts/inworld/projector_orbit_check.py: a camera-position size change (cause lowres, a new tile size) is
tolerated in a later lap; any other change still fails."""

import importlib.util
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("projector_orbit_check", HERE.parent / "inworld" / "projector_orbit_check.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

BASE = {"renders": 0, "causes": {}, "crcs": {"1": 5}}


def sample(lap, renders, crc, causes, size):
    return {"lap": lap, "tag": "s", "renders": renders, "without": 0, "crcs": {"1": crc}, "causes": causes, "sizes": {"1": size}}


def main():
    first = sample(1, 0, 5, {}, 512)
    # resized by the camera's position: one lowres redraw, another CRC at another size
    assert mod.judge_o2(BASE, [first, sample(2, 1, 9, {"lowres": 1}, 1024)]) == []
    # the same size and another CRC is a real difference
    assert mod.judge_o2(BASE, [first, sample(2, 1, 9, {"lowres": 1}, 512)]) != []
    # another cause is not tolerated
    assert mod.judge_o2(BASE, [first, sample(2, 1, 5, {"edit": 1}, 512)]) != []
    # a settling lap tolerates only the configured causes
    assert mod.judge_o2(BASE, [sample(1, 1, 5, {"lowres": 1}, 512)]) != []
    print("test_projector_orbit_check: all passed")


if __name__ == "__main__":
    main()
