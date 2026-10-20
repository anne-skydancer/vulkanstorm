# vkstorm-rust: the Rust port of Vulkanstorm

`vkstorm-rust` is the modernisation track: the existing viewer ported to Rust
incrementally, in dependency order, on its current OpenGL pipeline. Each rung
replaces one subsystem behind a C ABI, keeps the C++ implementation as the
fallback and as the oracle in tests, and stays releasable. The shared crates
live in [vkstorm-core-rust](https://github.com/anne-skydancer/vkstorm-core-rust),
consumed from `indra/rust/Cargo.toml` as git dependencies pinned by commit.
The design study behind the port is in the vkstorm-aleph repository under
`docs/upstream-study/`.

Rung order: J2C decoding, LLSD and the message codec, mesh assets and the prim
mesher, audio, textures, inventory, the object store, the renderer, the UI,
then Vulkan.

## Build integration

- `indra/cmake/Rust.cmake` fetches [corrosion](https://github.com/corrosion-rs/corrosion)
  v0.6.1 at configure time and imports the `vkstorm_ffi` static library target (crate `vkstorm-ffi`) from
  `indra/rust`. Option `USE_RUST_J2C` (default ON) controls rung one.
- The cargo release profile uses fat LTO and one codegen unit, matching the
  viewer's Release link. With `USE_AVX2_OPTIMIZATION` the crate is built for
  `x86-64-v3` (AVX2), the Release CPU baseline.
- `indra/rust/.cargo/config.toml` makes cargo fetch git dependencies with the
  git CLI, so private repositories work with the developer's credential
  helper. CI needs the secret `VKSTORM_CORE_RUST_TOKEN`, a fine-grained token
  with read access to vkstorm-core-rust, consumed in `build.yaml`.
- Required Release configuration for this track: Inno Setup as the installer,
  AVX2, OpenJPEG, no OpenSim, LTO, WebRTC voice. Vivox is not part of the
  Release, since Second Life voice is WebRTC; it stays available in OpenSim
  builds as today. CI's Release configure in `build.yaml` already matches.

## Rung one: JPEG 2000 decoding

- `indra/rust/vkstorm-ffi`: the C ABI (`vks_j2c_*`), mirrored by
  `indra/llimagej2crust/vkstorm_ffi.h`. Panics are caught at the boundary.
- `indra/llimagej2crust`: `LLImageJ2CRust`, derived from `LLImageJ2COJ`. It
  decodes with the Rust decoder (hayro-jpeg2000 behind `vkstorm-j2c`) and
  falls back to OpenJPEG when the Rust decode fails; encoding stays on
  OpenJPEG. It supplies `fallbackCreateLLImageJ2CImpl()`; the OpenJPEG
  library's own factory is compiled out with `LL_J2C_RUST_FRONTEND`.
- Setting `J2CDecoderRust` (default on) switches decoding at runtime; the
  change applies to the next decode. The About window's J2C engine line names
  both decoders.
- Test `INTEGRATION_TEST_llimagej2crust` decodes every `.j2c` in the default
  skin with both decoders at discard 0, 1 and 2 and on 25%-truncated input,
  and requires identical dimensions and samples within one code value. CI
  builds it with the other focused tests.
- Why this decoder: the evaluation in vkstorm-aleph,
  `docs/upstream-study/07-j2c-decoder-evaluation.md`, measured on the viewer's
  decode pattern against the Vulkanstorm OpenJPEG fork.

## Local build

Follow the project's RelWithDebInfo procedure (see `scripts/validate_soloud.sh`
for the canonical variables), then:

```sh
autobuild configure -A 64 -c ReleaseFS_open -- --no-opensim --zink --avx2 --no-package \
  -DUSE_DISCORD:BOOL=OFF -DOPENSIM:BOOL=FALSE -DLL_TESTS:BOOL=FALSE
cmake --build build-vc170-64 --config RelWithDebInfo --target vulkanstorm-bin -- -m:4 -nologo
cmake -S indra -B build-vc170-64 -DLL_TESTS:BOOL=ON
cmake --build build-vc170-64 --config RelWithDebInfo --target INTEGRATION_TEST_llimagej2crust -- -m:4 -nologo
```

Under Git Bash pass MSBuild switches with a hyphen (`-m:4`), not a slash.
