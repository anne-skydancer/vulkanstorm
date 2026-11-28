# Lodestar grid test (stage 0)

Four uploads of one small cube tell us what the Second Life asset server keeps of extra data in a mesh asset.
Spec: `reports/lodestar-viewer-survey-2026-11-28/report.md`, section 6.

This is a **devel-only** test hook. It lives on `vkstorm-devel` and is never integrated into release
(`scripts/tests/check_release_hooks.py` rejects the names `LodestarGridTest*` in release sources).

## Files

- `lodestar-grid-cube.dae`: the High cube. The other four files load by themselves from the same folder.
- `lodestar-grid-cube_LOD2.dae`, `_LOD1.dae`, `_LOD0.dae`, `_PHYS.dae`: Medium, Low, Lowest and physics.
- `make_assets.py`: writes the five files again.
- `diff_grid.py`: compares uploads with the stored assets and the quotes. `--self-test` runs it on synthetic data.

## The two debug settings

- `LodestarGridTestVariant` (0 to 3) changes what the upload writes:
  - 0: nothing (the control).
  - 1: a top-level `lodestar` map in the header, and a `lodestar` key inside `high_lod`.
  - 2: a 64-byte `lodestar` binary in the first face map of the Lowest block.
  - 3: a 64-byte block named by the header key `lodestar_block`, plus 64 bytes at the end that nothing refers to.
- `LodestarGridTestDump` (TRUE or FALSE): when the header of one of your own uploads of the last 2 hours arrives,
  the viewer fetches the whole asset once and writes `logs/lodestar-grid-<uuid>.bin`. Nothing else is written to disk.

## What the owner does in-world

1. Start the devel build named in the hand-off.
2. Log in.
3. Go to a parcel where you can build.
4. Advanced > Show Debug Settings: set `MeshUploadLogXML` to TRUE.
5. Set `LodestarGridTestDump` to TRUE.
6. Set `LodestarGridTestVariant` to 0.
7. Build > Upload > Model. Pick `lodestar-grid-cube.dae` from the folder named in the hand-off. The LOD and physics files load by themselves.
8. Click "Calculate weights & fee", and wait for the numbers.
9. Click Upload. Name it `grid-0`.
10. Set the variant to 1. Repeat steps 7-9 and name it `grid-1`.
11. Set it to 2. Repeat, as `grid-2`.
12. Set it to 3. Repeat, as `grid-3`.
13. Rez all four from your inventory, side by side.
14. Wait one minute, then quit.
15. Start stock Firestorm. Go to the same place, zoom in and out, and take a snapshot to disk.
16. Install the Linden Lab viewer from secondlife.com (it is not on this PC). Repeat step 15 with it.
17. Optionally take the four objects back.
18. Say when you are done.

Then, from the checkout (the logs folder is the viewer's `logs` folder in its user settings):

    python -I scripts/lodestar_grid/diff_grid.py <logs folder>

## Pass criteria

For each variant 1, 2 and 3, against the control (grid-0):

- the quotes equal the control's (grid-2 may differ by its added bytes; that is reported, not a failure);
- the value is present in the stored asset;
- every other block equals the control's;
- both stock viewers draw it like the control, with no mesh errors in their logs.

The script checks the first three and prints "Result: PASS" or "FAIL". The fourth is read from the snapshots and logs.
It also reports, without judging them: header keys added or dropped, every block uploaded against stored byte for byte,
the block order, the asset length, and the fee fields (`upload_price`, `resource_cost`, `model_streaming_cost`,
`simulation_cost`, `physics_cost`, and `streaming_params` per LOD).

## Reading the result

- Only variant 2 passes: the values must ride inside the Lowest block.
- None passes: the server keeps nothing it does not know, and Lodestar must live in its own asset or store.
