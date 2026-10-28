# SLua editor port qualification

## Reference and scope

Reference: Linden Lab Viewer **26.4.0.37384746592**, released October 13,
2026, tag `Second_Life_Release#e960123-26.4`, commit
`e960123230d521e8557a9582100bebcac36a7ca2`.

- [Release notes](https://releasenotes.secondlife.com/viewer/26.4.0.37384746592.html)
- [Pinned source](https://github.com/secondlife/viewer/tree/e960123230d521e8557a9582100bebcac36a7ca2)
- [VS Code extension setup](https://github.com/secondlife/sl-vscode-plugin/blob/develop/doc/installation.md)

The release notes continue to describe the server project as beta on selected
main-grid regions. The viewer's availability does not establish a date for
general simulator deployment. Use `LuaScriptsEnabled`, `ScriptDefinitions`,
and the script inventory subtype/runtime metadata advertised by the simulator.

The port includes native inventory/object script editing, four compile targets
(`lsl2`, `mono`, `lsl-luau`, and `luau`), Lua lexical rules and syntax definitions,
script creation, inventory icons, external-file workflows, and the released
WebSocket/JSON-RPC integration with VS Code. The bridge includes authenticated
local connection establishment, object/linkset exploration, script and notecard
operations, compiler diagnostics, and runtime/debug message forwarding.

The viewer does not execute simulator SLua scripts locally. The additional
Autobuild dependencies are the released `websocketpp` header package and
`lsl_definitions` version 0.6.12. Generated default syntax files are supplied by
that pinned package, rather than maintained as duplicate source assets.

Firestorm integration must retain the LSL preprocessor, OSSL/legacy keyword
extensions, script recovery, script templates, editor fonts/F1/Home behavior,
content reset/refresh controls, and applicable RLVa object restrictions. Lua
source must bypass the LSL preprocessor. IDE writes must obey object restrictions
as native editor writes do.

## Build and automated checks

Build the feature in `RelWithDebInfo`, with the release viewer's dependencies
and media plugins, `PACKAGE=OFF`, and no installer. Stage the complete runtime
using `viewer_manifest.py --actions=copy`; an executable-only build is insufficient.

Run `git diff --check`, parse all changed XML assets, build the viewer, and run
the `llinventory_runtime` integration test with `LL_TESTS=ON`. Its cases cover:

- SLua source metadata and inventory-cache round trips;
- LSL source running on the Luau VM;
- direct AIS and wrapped non-AIS metadata, including clearing stale runtime;
- copying inventory items and clearing runtime on an asset-type change;
- preserving unrelated inventory flags when changing the script subtype;
- legacy task-inventory serialization of source subtype and runtime.

Qualify Windows and Linux separately. Record actual results and failures; do
not infer a Linux pass from a Windows build. The existing release-hook check
must also pass on the eventual release integration branch.

The development baseline `f09355e409` has an unrelated full-suite limitation:
`llui/tests/llurlmatch_test.cpp` uses an obsolete `LLUrlMatch::setValues` call
signature. Building the complete viewer with `LL_TESTS=ON` reaches that compile
failure. Keep it separate from the three focused script/transport test results;
do not describe those results as a full-suite pass. CI builds the staged viewer
with the normal `LL_TESTS=OFF` configuration, then enables tests to build and run
the three focused targets.

## Viewer and simulator checks

Use a disposable editable object on a region advertising SLua and another
region without it. Repeat the native UI checks with the default, vintage,
Starlight, and Starlight CUI skins.

1. Create a Lua script and an LSL script. Verify inventory icons, permissions,
   source language, save/reopen behavior, and object inventory refresh.
2. Compile LSL with LSO, Mono, and Luau. Reopen and verify that LSL on Luau
   remains LSL source. Compile SLua with Luau and verify its metadata.
3. Verify Lua strings, escaped quotes, long-bracket strings/comments, namespace
   functions, syntax help, line numbering, compiler-error navigation, and Go To.
4. Enable the LSL preprocessor. Verify macros/includes and runtime directives,
   preprocessed error navigation, and existing script templates. Verify SLua
   bypasses preprocessing and cannot enter the preprocessed-source tab.
5. Edit externally using ordinary files, including `.lsl`, `.lua`, and `.luau`
   loads, repeated saves, temporary-file cleanup, and closing an editor during
   an outstanding operation. Verify script recovery after disconnection.
6. Verify font selection, F1 help, Home behavior, save indicators, compile errors,
   experience settings, running state, reset, and contents refresh.
7. Move between regions while an editor is open. Verify Lua options follow
   advertised support and LSL editing remains usable without SLua capabilities.
8. On OpenSim, verify the legacy LSL/OSSL editor and script creation path.
9. Verify locked attachment and applicable seated-object restrictions still
   prevent native and IDE mutations.

## VS Code checks

1. Keep the bridge disabled initially. Enable it through Scripting Settings and
   use the published Second Life extension. Verify local connection challenge,
   handshake, connection failure, reconnection, and shutdown.
2. Explore an editable linkset, add/remove a child, rename a prim, change its
   description/permissions, and change selection. Check that the IDE updates
   without duplicate subscriptions or stale published state.
3. Read, edit, save, create, and delete scripts/notecards in object contents.
   Test unavailable objects, removed items, missing capabilities, permission
   changes, no-copy/no-modify scripts, and invalid requests.
4. Compile valid and invalid LSL/SLua. Check structured diagnostics and correct
   script, line, and column associations. Check running state and reset actions.
5. Check owner messages, debug output, fragmented runtime errors, stack frames,
   and the forwarding preference. Close editors/objects and exit the viewer
   with active connections and pending operations.

## Integration order

The feature branch starts at `origin/vkstorm-devel`; no release/development or
legacy-master merge is part of this port. After qualification, submit the
feature to `vkstorm-devel` by PR. Prepare separate integration branches for
`vkstorm-canary` and `vkstorm-release`, carrying only the qualified port commits.
Verify each PR's source and destination before merging. Recheck the native UI,
skin overrides, and dependencies against each destination.

Do not advance `latest` as part of feature qualification. Only a successful CI
Release build supplying both Windows and Linux binaries may advance it.
