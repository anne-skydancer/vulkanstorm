# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Writes the five hand-made test files of the Lodestar grid test (stdlib only).

    lodestar-grid-cube.dae        High      1 m cube, 2 x 2 quads per face (48 triangles)
    lodestar-grid-cube_LOD2.dae   Medium    cube (12 triangles), UVs tweaked by 5 %
    lodestar-grid-cube_LOD1.dae   Low       cube (12 triangles), UVs tweaked by 10 %
    lodestar-grid-cube_LOD0.dae   Lowest    cube (12 triangles), UVs tweaked by 15 %
    lodestar-grid-cube_PHYS.dae   Physics   plain box (12 triangles)

The suffixes are the viewer's defaults (FSMeshLowestLodSuffix LOD0, FSMeshLowLodSuffix LOD1,
FSMeshMediumLodSuffix LOD2, FSMeshPhysicsSuffix PHYS, High has none); the uploader looks the
files up next to the High file by `<name>_<suffix>.dae` (LLModelPreview::lookupLODModelFiles).
The model name inside each file carries the same suffix and is stripped on load.

Usage: python make_assets.py [output_dir]   (default: the folder of this script)
"""
import sys
from pathlib import Path

# (normal, u axis, v axis) with u x v = normal, so the triangles wind counter-clockwise from outside.
FACES = (
    ((1, 0, 0), (0, 1, 0), (0, 0, 1)),
    ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
    ((0, 1, 0), (0, 0, 1), (1, 0, 0)),
    ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
    ((0, 0, -1), (0, 1, 0), (1, 0, 0)),
)


def cube(n, tweak=0):
    """Return (positions, normals, uvs, triangle vertex indices) of a unit cube, n x n quads a face.

    tweak > 0 shrinks the U coordinate by 5 % per step, so the Medium, Low and Lowest blocks differ
    from each other (a swapped or merged block then shows in the byte comparison)."""
    pos, nor, uv, idx = [], [], [], []
    for normal, u, v in FACES:
        base = len(pos)
        for j in range(n + 1):
            for i in range(n + 1):
                s, t = i / n, j / n
                pos.append(tuple(0.5 * normal[k] + (s - 0.5) * u[k] + (t - 0.5) * v[k] for k in range(3)))
                nor.append(normal)
                uv.append((s * (1 - 0.05 * tweak), t))
        for j in range(n):
            for i in range(n):
                a = base + j * (n + 1) + i
                b, c, d = a + 1, a + n + 2, a + n + 1
                idx.extend((a, b, c, a, c, d))
    return pos, nor, uv, idx


def floats(rows):
    return " ".join(f"{x:g}" for row in rows for x in row)


def dae(name, n, tweak=0):
    pos, nor, uv, idx = cube(n, tweak)
    count = len(idx) // 3
    tri = " ".join(f"{i} {i} {i}" for i in idx)
    return f'''<?xml version="1.0" encoding="utf-8"?>
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
  <asset>
    <contributor><authoring_tool>lodestar_grid/make_assets.py</authoring_tool></contributor>
    <unit name="meter" meter="1"/>
    <up_axis>Z_UP</up_axis>
  </asset>
  <library_effects>
    <effect id="grid_fx">
      <profile_COMMON>
        <technique sid="common">
          <lambert><diffuse><color>0.6 0.6 0.6 1</color></diffuse></lambert>
        </technique>
      </profile_COMMON>
    </effect>
  </library_effects>
  <library_materials>
    <material id="grid_mat" name="grid_mat"><instance_effect url="#grid_fx"/></material>
  </library_materials>
  <library_geometries>
    <geometry id="{name}-mesh" name="{name}">
      <mesh>
        <source id="{name}-pos">
          <float_array id="{name}-pos-array" count="{len(pos) * 3}">{floats(pos)}</float_array>
          <technique_common><accessor source="#{name}-pos-array" count="{len(pos)}" stride="3"><param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common>
        </source>
        <source id="{name}-nor">
          <float_array id="{name}-nor-array" count="{len(nor) * 3}">{floats(nor)}</float_array>
          <technique_common><accessor source="#{name}-nor-array" count="{len(nor)}" stride="3"><param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common>
        </source>
        <source id="{name}-uv">
          <float_array id="{name}-uv-array" count="{len(uv) * 2}">{floats(uv)}</float_array>
          <technique_common><accessor source="#{name}-uv-array" count="{len(uv)}" stride="2"><param name="S" type="float"/><param name="T" type="float"/></accessor></technique_common>
        </source>
        <vertices id="{name}-vtx"><input semantic="POSITION" source="#{name}-pos"/></vertices>
        <triangles material="grid_mat" count="{count}">
          <input semantic="VERTEX" source="#{name}-vtx" offset="0"/>
          <input semantic="NORMAL" source="#{name}-nor" offset="1"/>
          <input semantic="TEXCOORD" source="#{name}-uv" offset="2" set="0"/>
          <p>{tri}</p>
        </triangles>
      </mesh>
    </geometry>
  </library_geometries>
  <library_visual_scenes>
    <visual_scene id="Scene" name="Scene">
      <node id="{name}" name="{name}" type="NODE">
        <instance_geometry url="#{name}-mesh" name="{name}">
          <bind_material><technique_common><instance_material symbol="grid_mat" target="#grid_mat"/></technique_common></bind_material>
        </instance_geometry>
      </node>
    </visual_scene>
  </library_visual_scenes>
  <scene><instance_visual_scene url="#Scene"/></scene>
</COLLADA>
'''


FILES = (
    ("lodestar-grid-cube.dae", "cube", 2, 0),
    ("lodestar-grid-cube_LOD2.dae", "cube_LOD2", 1, 1),
    ("lodestar-grid-cube_LOD1.dae", "cube_LOD1", 1, 2),
    ("lodestar-grid-cube_LOD0.dae", "cube_LOD0", 1, 3),
    ("lodestar-grid-cube_PHYS.dae", "cube_PHYS", 1, 0),
)


def main(argv):
    out = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parent
    out.mkdir(parents=True, exist_ok=True)
    for filename, name, n, tweak in FILES:
        (out / filename).write_text(dae(name, n, tweak), encoding="utf-8", newline="\n")
        print("wrote", out / filename)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
