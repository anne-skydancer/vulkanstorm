# Lodestar grid test (stage 0)

Four uploads of one small cube tell us what the Second Life asset server keeps of extra data in a mesh asset.
Spec: `reports/lodestar-viewer-survey-2026-11-28/report.md`, section 6.

This is a **devel-only** test hook. It lives on `vkstorm-devel` and is never integrated into release
(`scripts/tests/check_release_hooks.py` rejects its names in release sources, such as `LodestarGridTest*`, `LLMeshGridDumpHandler`
and `mGridDumpDone`, and fails when the folder `scripts/lodestar_grid` exists).

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
- `LodestarGridTestDump` (TRUE or FALSE): when the header of one of your own uploads of the last 2 hours arrives (5 minutes of clock skew allowed; cached headers count too),
  the viewer fetches the whole asset once and writes `logs/lodestar-grid-<uuid>.bin`. Nothing else is written to disk.

## Before you start

- Uploads are free on your membership. Nothing here costs L$.
- Delete the old logs: in the viewer's `logs` folder, delete every `whole_model_*.xml` and every `lodestar-grid-*.bin`.
- Both debug settings are not persisted. Set them again after every viewer start.
- The Lowest slot must be loaded from the `_LOD0` file before you upload grid-2 (see step 12).

## What the owner does in-world

1. Delete `whole_model_*.xml` and `lodestar-grid-*.bin` from the logs folder (see above).
2. Start the devel build named in the hand-off.
3. Log in.
4. Go to a parcel where you can build.
5. Advanced > Show Debug Settings: set `MeshUploadLogXML` to TRUE.
6. Set `LodestarGridTestDump` to TRUE.
7. Set `LodestarGridTestVariant` to 0.
8. Build > Upload > Model.
9. Pick `lodestar-grid-cube.dae` from the folder named in the hand-off. The LOD and physics files load by themselves.
10. Click "Calculate weights & fee".
11. Wait for the numbers.
12. Check that the Lowest slot says `lodestar-grid-cube_LOD0.dae`.
13. Click Upload.
14. Name it `grid-0`.
15. Set `LodestarGridTestVariant` to 1.
16. Build > Upload > Model, pick `lodestar-grid-cube.dae`.
17. Click "Calculate weights & fee", and wait for the numbers.
18. Check the Lowest slot (as in step 12), then click Upload.
19. Name it `grid-1`.
20. Set `LodestarGridTestVariant` to 2.
21. Build > Upload > Model, pick `lodestar-grid-cube.dae`.
22. Click "Calculate weights & fee", and wait for the numbers.
23. Check the Lowest slot (as in step 12), then click Upload.
24. Name it `grid-2`.
25. Set `LodestarGridTestVariant` to 3.
26. Build > Upload > Model, pick `lodestar-grid-cube.dae`.
27. Click "Calculate weights & fee", and wait for the numbers.
28. Check the Lowest slot (as in step 12), then click Upload.
29. Name it `grid-3`.
30. Rez all four from your inventory, side by side.
31. Wait one minute, then quit.
32. Start stock Firestorm.
33. Go to the same place, and zoom in and out.
34. Take a snapshot to disk.
35. Install the Linden Lab viewer from secondlife.com (it is not on this PC).
36. Start it and go to the same place.
37. Zoom in and out, and take a snapshot to disk.
38. Optionally take the four objects back.
39. Say when you are done.

Then, from the checkout (the logs folder is the viewer's `logs` folder in its user settings):

    python -I scripts/lodestar_grid/diff_grid.py <logs folder>

The script pairs each stored asset with its upload by the order of the header date, not by the new asset id in the
upload response (the viewer reports the inventory object's asset there, which probably differs from the mesh asset).
If a variant was uploaded twice, it uses the highest numbered one that has an upload response, and says so.

## Pass criteria

For each variant 1, 2 and 3, against the control (grid-0):

- the quotes equal the control's (grid-2 may differ by its added bytes; that is reported, not a failure; a fee field missing on either side is a FAIL);
- the value is present in the stored asset, unchanged (the verdict is kept, dropped or altered);
- every other block equals the control's (the physics blocks are reported apart: a difference there is "possibly non-deterministic", not a failure);
- both stock viewers draw it like the control, with no mesh errors in their logs.

The script checks the first three, prints one verdict per variant and "Result: PASS" or "FAIL". Its exit code is 1 only when the script cannot do its job (no control upload found). The fourth criterion is read from the snapshots and logs.
It also reports, without judging them: header keys added or dropped, every block uploaded against stored byte for byte,
the block order, the asset length, and the fee fields (`upload_price`, `resource_cost`, `model_streaming_cost`,
`simulation_cost`, `physics_cost`, and `streaming_params` per LOD).

## Reading the result

- Only variant 2 passes: the values must ride inside the Lowest block.
- None passes: the server keeps nothing it does not know, and Lodestar must live in its own asset or store.
