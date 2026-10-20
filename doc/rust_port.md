# vkstorm-rust: the Rust port of Vulkanstorm

`vkstorm-rust` is the modernisation track: the existing viewer ported to Rust
incrementally, in dependency order, on its current OpenGL pipeline. Each rung
replaces one subsystem behind a C ABI, keeps the C++ implementation only as
the oracle in tests (never as a runtime fallback), and stays releasable. The shared crates
live in [vkstorm-core-rust](https://github.com/anne-skydancer/vkstorm-core-rust),
consumed from `indra/rust/Cargo.toml` as git dependencies pinned by commit.
The design study behind the port is in the vkstorm-aleph repository under
`docs/upstream-study/`.

Rung order: the JPEG 2000 codec, LLSD and the message codec, mesh assets and
the prim mesher, audio, textures, inventory, the object store, the renderer,
the UI, then Vulkan.

## Build integration

- `indra/cmake/Rust.cmake` fetches [corrosion](https://github.com/corrosion-rs/corrosion)
  v0.6.1 at configure time and imports the `vkstorm_ffi` static library target (crate `vkstorm-ffi`) from
  `indra/rust`. There is no option to select another codec.
- The cargo release profile uses fat LTO and one codegen unit, matching the
  viewer's Release link. With `USE_AVX2_OPTIMIZATION` the crate is built for
  `x86-64-v3` (AVX2), the Release CPU baseline.
- `indra/rust/.cargo/config.toml` makes cargo fetch git dependencies with the
  git CLI. vkstorm-core-rust is public, so CI needs only the Rust toolchain
  (`dtolnay/rust-toolchain@stable` in `build.yaml`) and no credentials.
- Required Release configuration for this track: Inno Setup as the installer,
  AVX2, OpenJPEG, no OpenSim, LTO, WebRTC voice. Vivox is not part of the
  Release, since Second Life voice is WebRTC; it stays available in OpenSim
  builds as today. CI's Release configure in `build.yaml` already matches.

## Rung one: JPEG 2000 codec

- The viewer's JPEG 2000 implementation is Rust, in both directions, with no
  runtime choice and no fallback: `vkstorm-j2c` on `vkstorm-openjp2`, the port
  of OpenJPEG owned in vkstorm-core-rust (see its `docs/j2c-port-plan.md`).
  OpenJPEG the C library is no longer part of this build.
- `indra/rust/vkstorm-ffi`: the C ABI (`vks_j2c_*`: metadata, decode, encode),
  mirrored by `indra/llimagej2coj/vkstorm_ffi.h`. Panics are caught at the
  boundary.
- `indra/llimagej2coj`: `LLImageJ2COJ` keeps its name (the rest of the tree is
  unchanged) but is now the thin C++ side of the Rust codec: header metadata,
  decode at the discard level from possibly truncated data with the viewer's
  channel selection and bottom-up rows, COM comment, and upload encoding with
  the viewer's layer, rate and small-image resolution rules. A failed decode
  reports failure as before.
- The engine string stays within `LLTextureCache`'s 32-byte header field; its
  change purges the texture cache once on first start.
- Test `INTEGRATION_TEST_llimagej2coj` decodes every `.j2c` in the default
  skin at discard 0, 1 and 2 and on 25%-truncated input, encodes the larger
  ones lossy and lossless and decodes them back (lossless exact, comment
  carried), checks the engine string length and rejects junk. Byte-level
  parity with the C codec is proven in vkstorm-core-rust's own tests.
- CI builds it with the other focused tests.

## Local build

Follow the project's RelWithDebInfo procedure (see `scripts/validate_soloud.sh`
for the canonical variables), then:

```sh
autobuild configure -A 64 -c ReleaseFS_open -- --no-opensim --zink --avx2 --no-package \
  -DUSE_DISCORD:BOOL=OFF -DOPENSIM:BOOL=FALSE -DLL_TESTS:BOOL=FALSE
cmake --build build-vc170-64 --config RelWithDebInfo --target vulkanstorm-bin -- -m:4 -nologo
cmake -S indra -B build-vc170-64 -DLL_TESTS:BOOL=ON
cmake --build build-vc170-64 --config RelWithDebInfo --target INTEGRATION_TEST_llimagej2coj -- -m:4 -nologo
```

Under Git Bash pass MSBuild switches with a hyphen (`-m:4`), not a slash.
