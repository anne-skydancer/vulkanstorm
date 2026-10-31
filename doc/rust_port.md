# vkstorm-rust: the Rust port of Vulkanstorm

`vkstorm-rust` is the modernisation track: the existing viewer ported to Rust
incrementally, in dependency order, on its current OpenGL pipeline. Each rung
replaces one subsystem behind a C ABI, keeps the C++ implementation only as
the oracle in tests (never as a runtime fallback), and stays releasable. The shared crates
live in [vkstorm-core-rust](https://github.com/anne-skydancer/vkstorm-core-rust),
with the C ABI over them (`vkstorm-ffi`), which the viewer links as the
`vkstorm-ffi` autobuild package.
The design study behind the port is in the vkstorm-aleph repository under
`docs/upstream-study/`.

Rung order: the JPEG 2000 codec, LLSD and the message codec, mesh assets and
the prim mesher, the HTTP layer, the asset pipeline, audio (rung six),
inventory (rung seven, in progress), the object store, the remaining texture
work, the renderer, the UI, then Vulkan. Inventory moved ahead of textures
because rung five already ported the texture cache and fetch; what is left of
textures is the VRAM budget and discard policy, per-texture priority and pixel
operations (`doc/survey/README.md`, "Recommended order after inventory").
Small self-contained pieces of the application core (SLURL, chat-log parsing,
RLVa command syntax, version and date parsing) can be scheduled alongside the
larger rungs.

## Build integration

- The C ABI is the crate `vkstorm-ffi` in vkstorm-core-rust (`crates/vkstorm-ffi`,
  header `include/vkstorm_ffi.h`), one Cargo feature per rung. vkstorm-core-rust's
  CI builds it as a static library with its pinned toolchain
  (`rust-toolchain.toml`), with fat LTO and one codegen unit for the
  `x86-64-v3` (AVX2) Release baseline, and publishes it as the `vkstorm-ffi`
  autobuild package per rung set (tag `ffi-v*`). The viewer needs no Rust
  toolchain.
- This line pins the `full` archives (every rung) in `autobuild.xml`.
  `indra/cmake/Rust.cmake` defines the `vkstorm_ffi` target from the package:
  the library, the header, the system libraries it leaves to the final link
  (`vkstorm_ffi.native-libs.txt`, as rustc reported them) and libcurl.
- A rung's change to the ABI therefore lands in vkstorm-core-rust first, is
  tagged, and this line moves its `autobuild.xml` entry to the new archives.
- Development only: `-DVKS_CORE_RUST_DIR=<vkstorm-core-rust checkout>` builds
  the library from that checkout with cargo through
  [corrosion](https://github.com/corrosion-rs/corrosion) (v0.6.1, fetched at
  configure time), so the port can iterate without publishing a package; it
  needs a local Rust toolchain. CI always uses the package. OpenSim builds and
  builds without AVX2 need this switch until such packages exist; the package
  path refuses them rather than link a mismatched library.
- Until October 2026 the crate lived here as `indra/rust/vkstorm-ffi` and was
  built by corrosion on every viewer build; the rung sections below name the
  files by those former paths (`indra/rust/vkstorm-ffi/src/*.rs` is now
  `crates/vkstorm-ffi/src/*.rs` in vkstorm-core-rust, the JPEG 2000 and LLSD
  parts of `lib.rs` in `j2c.rs` and `llsd.rs`).
- Required Release configuration for this track: Inno Setup as the installer,
  AVX2, OpenJPEG, no OpenSim, LTO, WebRTC voice. Release builds have WebRTC
  voice only: Vivox is compiled only in OpenSim builds, as upstream Firestorm
  does since 7.2.5 (`<FS:TJ> Keep Vivox only for OpenSim viewers` in
  `llvoiceclient.cpp`, and the CMake `if (OPENSIM)`). This line keeps
  `vkstorm-release`'s build defaults except where a rung has replaced a part
  (for example `USE_VKS_AUDIO` instead of SoLoud, and the `vkstorm-ffi`
  package). CI's Release configure in `build.yaml` already matches.

## Rung one: JPEG 2000 codec

- The viewer's JPEG 2000 implementation is Rust, in both directions, with no
  runtime choice and no fallback: `vkstorm-j2c` on `vkstorm-openjp2`, the port
  of OpenJPEG owned in vkstorm-core-rust (see its `docs/j2c-port-plan.md`).
  OpenJPEG the C library is no longer part of this build.
- `indra/rust/vkstorm-ffi`: the C ABI (`vks_j2c_*`: metadata, decode, encode),
  mirrored by `indra/llcommon/vkstorm_ffi.h` (moved there in rung two, since
  llcommon is the lowest library using it). Panics are caught at the
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

## Rung two: LLSD serialization

The notation, binary and XML parsers and formatters of `llsdserialize.cpp`
and `llsdserialize_xml.cpp` are Rust: `vkstorm-llsd` in vkstorm-core-rust,
reached through `vks_llsd_*` in `vkstorm-ffi` (`indra/llcommon/vkstorm_ffi.h`,
moved to llcommon since it is now the lowest library using the ABI). Expat is
no longer used by llcommon. The in-memory `LLSD` class is untouched, and the
C++ classes keep their names and public interface (`LLSDParser`,
`LLSDNotationParser`, `LLSDXMLParser`, `LLSDBinaryParser`, `LLSDFormatter`
and its three subclasses, `LLSDSerialize`, `LLSDOStreamer`), so no caller
changed. There is no runtime switch and no C++ fallback; the former C++
implementation lives on only as the oracle of the parity test
(`indra/llcommon/tests/llsdserialize_oracle.cpp`, namespace `llsd_oracle`).

### Keep-or-port judgement

| Part | Decision | Why |
|---|---|---|
| Notation parser and formatter | Rust | 2.7-6.5x faster parsing, 9-27x faster formatting, identical results (table below). |
| Binary parser and formatter | Rust | Parsing 1.0-1.4x (the work is dominated by `LLSD` construction, which is the same C++ either way), formatting 1.9-4.6x; identical bytes. |
| XML parser (expat) and formatter | Rust (own tokenizer, no crate dependency) | Parsing 1.9-3.8x, formatting 5.8-18x; the tokenizer enforces expat's well-formedness rules (UTF-8, XML characters, matched tags, entities, `]]>`, `--` in comments, declaration placement) so malformed input fails the same way. |
| `LLSDNotationFormatter::escapeString`, `LLSDXMLFormatter::escapeString` | Rust | Same tables; checked over every byte value. |
| `LLSDSerialize::deserialize` header sniffing | C++ | Stream plumbing around the parsers (a dozen lines); nothing to gain. |
| The istream/ostream adapters and the `LLSD` builder | C++ | They are the boundary itself (see below); their cost is inside the measurements. |
| `realFormat` (`printf`-style real override) | C++ (`llformat`) | Used by callers with arbitrary `printf` formats; the C++ pre-formats the text and the Rust formatter embeds it. Byte-identical by construction. |
| `zip_llsd`, `unzip_llsd`, `unzip_llsdNavMesh`, `strip_deprecated_header` | C++ | zlib-ng's SIMD deflate/inflate; the LLSD inside goes through the Rust binary parser. A compression crate is a later rung's question, not this one's. |
| The `LLSD` value class | C++ | Out of scope by design; everything in the viewer holds `LLSD`. |

### The boundary

- Parsing: the C++ side hands the Rust parser a byte source (two callbacks
  over the `std::streambuf`) and gets back a flat node buffer (16-byte tagged
  nodes plus a byte arena), from which `buildLLSD` constructs the `LLSD`
  tree with one lookup per map entry. Map keys repeat as before: the
  notation and binary parsers keep the first value (`LLSD::insert`), the
  XML parser the last (`operator[]`).
- The source is fed in one of three ways, chosen per stream: memory-backed
  buffers (`std::stringbuf`, `LLMemoryStreamBuf`, boost array source) in
  bulk with a seek back of whatever was not consumed; XML on any other stream
  line by line (what the expat loop consumed: the parse ends at `</llsd>`
  with the rest of that line); notation and binary on any other stream
  exactly, through `sgetc`/`sgetn`, so a file or pipe is left positioned
  right after the value as the C++ parsers left it (the mesh header reader
  relies on `tellg()` after `fromBinary`). Size limits and depth limits are
  applied where the C++ applied them, including the bytes the C++ never
  counted (its `>>` extractions).
- Formatting: the C++ walks the `LLSD` and calls one `vks_llsd_fmt_*` per
  node; the Rust formatter writes to the `std::ostream` in 64 KiB chunks.
  The stream's `precision()`, `floatfield` and `boolalpha` flags are passed
  through, as the C++ `operator<<` honoured them (XML forces precision 25
  as before). Other stream flags (`hex`, `showpos`, `width`) are not
  honoured; no caller sets them.
- Both parsers and formatters are iterative (explicit stacks), so document
  depth cannot overflow the Rust stack; the `LLSD` builder is iterative too.
  Panics are caught at the boundary and reported as parse failure.

### Measurements

`llsdserialize_tool bench 20` (built with `LL_TESTS=ON`): RelWithDebInfo
viewer build, cargo release profile with fat LTO and AVX2, single thread,
Windows 11, MSVC 17; median of 20 runs (400 for the two small documents; 10
for the two large synthetic ones). "stringstream" is the bulk path,
"opaque" a stream the adapter does not recognise (exact feeding for
notation and binary, line feeding for XML). The Rust column includes the
boundary: callbacks, node buffer, `LLSD` construction (parse) and the
`LLSD` walk and chunked `ostream` writes (format).

| document | operation | bytes | C++ ms | Rust ms | Rust/C++ |
|---|---|---:|---:|---:|---:|
| login response / notation | parse (stringstream) | 1218 | 0.049 | 0.008 | 6.28x |
| login response / notation | parse (opaque) | 1218 | 0.049 | 0.017 | 2.95x |
| login response / notation | format | 1218 | 0.027 | 0.002 | 14.05x |
| login response / binary | parse (stringstream) | 1102 | 0.009 | 0.006 | 1.44x |
| login response / binary | parse (opaque) | 1102 | 0.008 | 0.007 | 1.19x |
| login response / binary | format | 1102 | 0.004 | 0.001 | 3.42x |
| login response / xml | parse (stringstream) | 1951 | 0.034 | 0.010 | 3.25x |
| login response / xml | parse (opaque) | 1951 | 0.034 | 0.013 | 2.57x |
| login response / xml | format | 1951 | 0.036 | 0.002 | 18.20x |
| map all types / notation | parse (stringstream) | 798 | 0.013 | 0.003 | 4.17x |
| map all types / notation | parse (opaque) | 798 | 0.012 | 0.009 | 1.39x |
| map all types / notation | format | 798 | 0.040 | 0.002 | 26.73x |
| map all types / binary | parse (stringstream) | 497 | 0.002 | 0.002 | 1.28x |
| map all types / binary | parse (opaque) | 497 | 0.002 | 0.002 | 1.00x |
| map all types / binary | format | 497 | 0.001 | 0.001 | 1.86x |
| map all types / xml | parse (stringstream) | 843 | 0.020 | 0.005 | 3.83x |
| map all types / xml | parse (opaque) | 843 | 0.020 | 0.006 | 3.13x |
| map all types / xml | format | 843 | 0.009 | 0.002 | 5.81x |
| settings.xml (2397 settings) / notation | parse (stringstream) | 355033 | 15.801 | 2.554 | 6.19x |
| settings.xml / notation | parse (opaque) | 355033 | 15.599 | 5.603 | 2.78x |
| settings.xml / notation | format | 355033 | 10.227 | 1.118 | 9.15x |
| settings.xml / binary | parse (stringstream) | 410970 | 3.533 | 2.446 | 1.44x |
| settings.xml / binary | parse (opaque) | 410970 | 3.357 | 2.841 | 1.18x |
| settings.xml / binary | format | 410970 | 1.286 | 0.431 | 2.98x |
| settings.xml / xml | parse (stringstream) | 632931 | 9.722 | 3.934 | 2.47x |
| settings.xml / xml | parse (opaque) | 632931 | 9.639 | 4.763 | 2.02x |
| settings.xml / xml | format | 632931 | 13.696 | 0.948 | 14.45x |
| settings-like map (3000 entries) / notation | parse (stringstream) | 378239 | 17.226 | 2.643 | 6.52x |
| settings-like map / notation | parse (opaque) | 378239 | 16.957 | 5.813 | 2.92x |
| settings-like map / notation | format | 378239 | 11.244 | 1.041 | 10.80x |
| settings-like map / binary | parse (stringstream) | 451764 | 3.349 | 2.392 | 1.40x |
| settings-like map / binary | parse (opaque) | 451764 | 3.390 | 2.777 | 1.22x |
| settings-like map / binary | format | 451764 | 1.527 | 0.357 | 4.28x |
| settings-like map / xml | parse (stringstream) | 785566 | 13.051 | 4.336 | 3.01x |
| settings-like map / xml | parse (opaque) | 785566 | 13.380 | 5.567 | 2.40x |
| settings-like map / xml | format | 785566 | 16.141 | 2.099 | 7.69x |
| inventory-like (20000 items) / notation | parse (stringstream) | 9618917 | 413.270 | 74.326 | 5.56x |
| inventory-like / notation | parse (opaque) | 9618917 | 412.150 | 154.020 | 2.68x |
| inventory-like / notation | format | 9618917 | 228.979 | 23.047 | 9.94x |
| inventory-like / binary | parse (stringstream) | 8148933 | 75.703 | 53.169 | 1.42x |
| inventory-like / binary | parse (opaque) | 8148933 | 74.160 | 65.993 | 1.12x |
| inventory-like / binary | format | 8148933 | 36.288 | 7.955 | 4.56x |
| inventory-like / xml | parse (stringstream) | 17418976 | 294.757 | 100.178 | 2.94x |
| inventory-like / xml | parse (opaque) | 17418976 | 278.882 | 147.606 | 1.89x |
| inventory-like / xml | format | 17418976 | 340.220 | 34.862 | 9.76x |

Before the last round of boundary work the binary parser measured 0.82-1.01x
(a temporary vector per fixed-size read, a 16 KiB zeroed buffer per parse and
a double map lookup in the builder); those are gone and every row is at or
above 1.00x.

### Behaviour kept, and what is documented as different

Every quirk that affects results is kept and covered by tests: whitespace
and junk skipping inside notation maps, keys without values, `t`/`f` as
booleans, numbers stopping at the first non-numeric byte (`istream >>`
semantics, including MSVC's hex floats and its stored value on overflow),
36-byte UUIDs read with whitespace skipped, `b64""` failing, zero-padded
short binary integers/reals/UUIDs being accepted, negative binary sizes as
empty blobs, the XML element state machine (skipping, dangling keys, empty
keys, values in scalars, unknown elements, `encoding` other than base64,
`sscanf("%d")` with the UCRT's saturation, `asReal()` whole-string
extraction, whitespace-stripped base64), `LLDate` text forms
(hundredths, pre-epoch parse failure, offsets) and `printf("%.{p}g")` reals
(NaN spelled as the platform's C runtime does: `nan`/`-nan(ind)` on
Windows).

Differences, all outside what any caller exercises:

- After an XML error the C++ parser had consumed up to the end of the
  current line or 1024-byte chunk, whichever came first; the Rust parser
  consumes to the end of the line. The same granularity applies to the text
  that follows `</llsd>` on its line.
- An unterminated `b16"` blob made the C++ notation parser loop forever; it
  now fails. An odd trailing hex digit (undefined behaviour in C++) yields
  `hi << 4`.
- A negative `s(len)`/`b(len)` crashed the C++ (huge `resize`); it fails.
- Where the two C runtimes disagreed, the product now has one behaviour on
  both platforms, stated in the corpus as "runtime edges" (the parity test
  checks these against the stated expectation, not the platform's oracle,
  and the fixtures carry the same expectation):
  - an underflowing real (`r1e-999`, `<real>1e-999</real>`) is accepted
    with the value `strtod` gives, 0 or a denormal, as glibc did; the MSVC
    runtime rejected the whole value, failing the document, which is the
    worse outcome and one no formatter can trigger;
  - an overflowing real in XML (`<real>1e999</real>`) is +/-infinity, as
    MSVC stored it (glibc stored `DBL_MAX`); in notation (`r1e999`) it
    fails on both, as before;
  - a hexadecimal real (`r0x1p3`) is read as MSVC's extraction did (glibc
    stopped at the `x`, leaving junk in the stream);
  - `sscanf("%d")` overflow saturates through a 64-bit value on both (the
    UCRT's and glibc's `long` behaviour agree once `long` is 64-bit);
  - NaN is spelled as the platform's C runtime spells it (`-nan(ind)` on
    Windows, `-nan` elsewhere), so each platform's output is unchanged.
  The fixtures were generated on Windows.

### Tests

- vkstorm-core-rust `crates/vkstorm-llsd`: 8 unit tests, the 27 cases of
  `llsdserialize_test.cpp` transcribed (`tests/cpp_suite.rs`), and
  `tests/fixtures.rs` over 3,638 cases generated from the C++ oracle (536
  parse cases with counts and values, 3,102 formatter cases over every
  option, boolalpha and realFormat), regenerated with
  `tools/llsd-fixtures/regenerate.py` from the viewer's `llsdserialize_tool dump`.
- `INTEGRATION_TEST_llsdserialize_parity`: 10 groups against the oracle:
  every formatter option over the corpus, the escape helpers over all 256
  bytes, 400+ parse cases (valid and malformed, size and depth limited,
  parse and parseLines) on both stream kinds with the remaining stream
  compared, round trips with and without byte limits, trailing-data stream
  position, `deserialize()` with every header and sniffed, every LLSD XML
  file in `app_settings` (file streams, text mode, pretty re-serialization),
  the large documents, `zip_llsd`/`unzip_llsd`, parser reuse and
  multi-document streams. Run by CI with the other focused tests.
- `INTEGRATION_TEST_llsdserialize`: the original suite, unchanged, 71 tests
  now exercising the Rust implementation (including the Python `llsd`
  interoperability tests).
- `llsdserialize_tool` (built with `LL_TESTS=ON`, not run automatically):
  `bench [iterations]` and `dump <dir>`.
## Rung: the parametric prim mesher

- The mesher of `indra/llmath/llvolume.cpp` is Rust: `vkstorm-volume` in
  vkstorm-core-rust (`crates/vkstorm-volume`), reached through `vks_volume_*`
  in `indra/rust/vkstorm-ffi/src/volume.rs` (header
  `indra/rust/include/vkstorm_ffi.h`, now shared by every library that calls
  into the crate; `llmath` links `vkstorm_ffi`). No runtime choice, no
  fallback: the C++ bodies of `LLProfile::generate`/`genNGon`/`addHole`,
  `LLPath::generate`/`genNGon`, the mesh sweep of `LLVolume::generate`,
  `LLVolumeFace::createSide`/`createCap`/`createUnCutCubeCap`,
  `createTangents` and `LLCalculateTangentArray` are gone; the methods that
  remain are thin wrappers. The 881-case corpus recorded from the original
  C++ (`indra/llmath/tests/fixtures/llvolume_corpus.vksv`) is the
  oracle on Windows (8.4 MB); its Linux counterpart
  `llvolume_corpus.linux.vksv` was recorded by the same test from the Linux
  build (see below).
- `LLVolume`, `LLProfile`, `LLPath` and `LLVolumeFace` stay the in-memory
  representation. Profile and path generation return pointers into
  per-thread scratch storage that the C++ copies out (a few hundred bytes);
  the mesh sweep, the face builders and the tangents write straight into
  the caller's 16-byte aligned buffers (`mMesh`, `mPositions`, `mNormals`,
  `mTexCoords`, `mIndices`, `mTangents`), so nothing the renderer consumes
  is copied. The sweep writes through `MaybeUninit` (a freshly resized
  `mMesh` is not initialised); the face builders and the tangents form
  slices over their buffers, so `LLVolumeFace::create` and `createTangents`
  zero freshly allocated storage (texture-coordinate padding included)
  before the call, and a partial rebuild keeps the data it has. `LLPath::PathPt` and `LLProfile::Face` keep their layout, shared
  with the crate (`static_assert`ed).
- Keep-or-port judgement. Ported: everything above. The SSE code it replaced
  (`LLVector4a` cross products, `_mm_min_ps`/`_mm_max_ps` extents,
  `normalize3fast`, `LLMatrix4a::rotate`) is written lane for lane in Rust
  with the same IEEE operations; `_mm_rsqrt_ps` is called through
  `core::arch` because its result is a hardware approximation. Measured
  (table below), the Rust path is faster than the C++ on every shape and
  LOD class, so no part was kept for performance. Kept C++: the sculpt
  map-to-mesh step (`sculptGenerateMapVertices`, next rung; its faces
  already go through the Rust builder with the sculpt stitching rules), mesh
  assets (`unpackVolumeFaces`, next rung), `cacheOptimize` (out of scope),
  silhouettes, line intersection, octrees, `remap`/`optimize`, and the
  `LLVolumeFace` memory management (`resizeVertices`/`resizeIndices`,
  partial rebuilds), which is the representation itself.
- Floating-point identity. The C++ operation order is kept. The C++ itself
  does not produce the same mesh on Windows and Linux (10793 of the 27158
  corpus blocks differ between the two builds, two of them index blocks):
  with MSVC the unqualified `sin`/`cos`/`floor`/`fabs` of `llvolume.cpp`
  bind the global float overloads and `std::lerp` is an `fma` at its core;
  with GCC and libstdc++ they bind the C double functions (the surrounding
  expression is then evaluated in double before the store to `F32`, and the
  test path's `lerp` promotes to the double overload) and `std::lerp` is the
  unfused `a + t * (b - a)`; GCC also folds the cap normal's `* -1.0f` into
  a negation, which differs from a multiply only for the NaN a zero-area cap
  produces. The crate follows each platform's resolution (`params.rs`,
  `face.rs`), so there is one oracle corpus per toolchain:
  `llvolume_corpus.vksv` recorded on Windows (MSVC 19.44, UCRT) and
  `llvolume_corpus.linux.vksv` recorded on Ubuntu 26.04 (GCC 15, glibc)
  from the pre-port C++ at the `vkstorm-rust` base commit with the CI Linux
  configuration, both on an AMD Ryzen 9 9950X3D. `_mm_rsqrt_ps` results are
  CPU-specific (Intel and AMD tables differ), so each fixture carries a
  signature of the recording CPU and the tests compare the rsqrt-derived
  arrays (cap normals, tangents, normalised normals) with a tolerance only
  when it differs; the recorder always stores those arrays in full (the
  other arrays only for the smaller cases, hashes otherwise), so such a
  comparison is never skipped: a recording platform allows no unverifiable
  block, and the tolerance treats a NaN or an infinity against anything but
  an identical value (or another NaN) as a difference. Index blocks and the
  integer fields of profile-face records are exact everywhere. A platform
  with no recording (macOS) gets the Windows corpus, exact integer data and
  a tolerance on the float arrays, and says so. The two corpora are 8.4 MB
  each (fixture format 2).
- Parity: `cargo test -p vkstorm-volume` (crate) and
  `INTEGRATION_TEST_llvolume` (viewer, through the FFI) rebuild all 881
  cases, 3939 faces, 27158 array blocks (positions, normals, texture
  coordinates, indices, tangents, normalised normals, profile points and
  faces, path points, mesh) plus counts, extents, centres, texture
  extents, face masks and LOD triangle estimates against the platform's
  corpus: all bit-identical on Windows and on Linux (0 mismatches, 0
  tolerance comparisons on both). The corpus covers all six profile curves,
  four hole shapes, line/circle/circle2/test and flexible paths, hollow,
  both cuts, twist, taper, shear, radius offset, revolutions, skew and
  ratio, detail 0, 1, 1.5, 2.5 and 4, and sculpt stitching (sphere, torus,
  plane, cylinder, invert, mirror). Recording a corpus means building the
  test at the pre-rung commit with `VKS_VOLUME_DUMP=<file>`.
- Measurements (single thread, AMD Ryzen 9 9950X3D, RelWithDebInfo viewer
  with AVX2, the crate at `opt-level 3`, fat LTO, `x86-64-v3`): the corpus
  through `LLVolume` construction plus `genTangents` for every face, with
  the C++ build of `INTEGRATION_TEST_llvolume` from the pre-rung commit and
  the Rust-backed build run back to back; minimum of 30 runs per case over
  two rounds, summed per shape and LOD, in microseconds. The Rust column is
  the viewer-side path, so it includes the FFI boundary (profile, path and
  sweep calls, two calls per face, one per face for tangents) and the C++
  wrappers. Total over the 881 cases: C++ 5459.9 us, Rust 4100.1 us
  (0.75); 879 cases are faster, the two slower ones are LOD-4 tori with
  square holes (28.0 vs 33.9 us and 28.3 vs 33.7 us). The crate's own
  harness (`cargo bench --bench mesher`, same flags) takes 4311.6 us for
  the same cases, so the boundary cost is below what the two harnesses
  resolve (the crate harness allocates more than the C++ wrappers do); the
  sculpt rows compare the whole C++ path, map step included, against the
  Rust face build on the recorded mesh.

| shape | LOD | cases | vertices | triangles | C++ us | Rust us | Rust/C++ |
|---|---|---|---|---|---|---|---|
| box | 0 | 1 | 24 | 12 | 1.7 | 1.1 | 0.65 |
| box | 1 | 45 | 1500 | 944 | 79.4 | 56.4 | 0.71 |
| box | 1.5 | 2 | 52 | 28 | 3.1 | 2.2 | 0.71 |
| box | 2.5 | 5 | 627 | 698 | 18.0 | 12.8 | 0.71 |
| box | 4 | 44 | 6176 | 7560 | 185.6 | 123.3 | 0.66 |
| box_flex | 1 | 2 | 257 | 248 | 9.0 | 7.7 | 0.86 |
| box_flex | 1.5 | 1 | 84 | 72 | 3.2 | 2.8 | 0.87 |
| box_flex | 2.5 | 1 | 128 | 144 | 4.3 | 3.6 | 0.84 |
| box_flex | 4 | 2 | 565 | 752 | 17.6 | 13.4 | 0.76 |
| circle_circle2 | 1 | 5 | 542 | 704 | 16.6 | 12.0 | 0.72 |
| circle_circle2 | 1.5 | 5 | 486 | 566 | 15.1 | 11.5 | 0.76 |
| circle_circle2 | 2.5 | 5 | 1664 | 2648 | 41.4 | 30.3 | 0.73 |
| circle_circle2 | 4 | 3 | 2374 | 4148 | 55.5 | 40.1 | 0.72 |
| circle_test | 1 | 3 | 192 | 216 | 6.2 | 4.6 | 0.74 |
| circle_test | 1.5 | 3 | 275 | 338 | 7.8 | 5.6 | 0.72 |
| circle_test | 2.5 | 5 | 667 | 852 | 17.6 | 12.7 | 0.72 |
| circle_test | 4 | 4 | 926 | 1242 | 22.4 | 15.4 | 0.69 |
| cylinder | 1 | 47 | 1805 | 1510 | 61.2 | 47.8 | 0.78 |
| cylinder | 1.5 | 2 | 92 | 74 | 3.3 | 2.4 | 0.73 |
| cylinder | 2.5 | 4 | 510 | 642 | 13.2 | 9.9 | 0.75 |
| cylinder | 4 | 46 | 9479 | 12988 | 224.8 | 158.8 | 0.71 |
| cylinder_flex | 1 | 2 | 161 | 194 | 6.0 | 5.0 | 0.83 |
| cylinder_flex | 1.5 | 1 | 112 | 162 | 3.6 | 2.9 | 0.81 |
| cylinder_flex | 2.5 | 1 | 178 | 270 | 5.4 | 4.2 | 0.78 |
| cylinder_flex | 4 | 2 | 458 | 680 | 12.8 | 9.8 | 0.77 |
| equaltri_line | 1 | 1 | 22 | 12 | 1.2 | 1.0 | 0.83 |
| equaltri_line | 1.5 | 4 | 174 | 136 | 6.7 | 5.4 | 0.81 |
| equaltri_line | 2.5 | 6 | 475 | 514 | 14.2 | 11.1 | 0.78 |
| equaltri_line | 4 | 4 | 482 | 558 | 12.7 | 9.7 | 0.76 |
| halfcircle_circle2 | 1 | 2 | 85 | 88 | 3.5 | 2.6 | 0.74 |
| halfcircle_circle2 | 1.5 | 6 | 397 | 416 | 14.3 | 10.8 | 0.76 |
| halfcircle_circle2 | 2.5 | 4 | 834 | 1192 | 22.3 | 16.7 | 0.75 |
| halfcircle_circle2 | 4 | 5 | 2135 | 3394 | 52.8 | 39.3 | 0.74 |
| halfcircle_line | 1 | 2 | 68 | 50 | 2.7 | 2.1 | 0.78 |
| halfcircle_line | 1.5 | 4 | 134 | 104 | 5.3 | 4.1 | 0.77 |
| halfcircle_line | 2.5 | 3 | 270 | 322 | 7.5 | 5.7 | 0.76 |
| halfcircle_line | 4 | 2 | 263 | 330 | 6.9 | 5.1 | 0.74 |
| isotri_circle | 1 | 3 | 230 | 224 | 7.7 | 6.0 | 0.78 |
| isotri_circle | 1.5 | 6 | 644 | 668 | 20.4 | 15.1 | 0.74 |
| isotri_circle | 2.5 | 5 | 2812 | 4278 | 64.6 | 48.8 | 0.76 |
| isotri_circle | 4 | 5 | 1216 | 1640 | 31.7 | 24.0 | 0.76 |
| prism | 1 | 46 | 1382 | 886 | 62.9 | 50.1 | 0.80 |
| prism | 1.5 | 7 | 258 | 186 | 10.7 | 8.6 | 0.80 |
| prism | 2.5 | 5 | 499 | 562 | 14.2 | 10.5 | 0.74 |
| prism | 4 | 46 | 5164 | 6126 | 141.0 | 104.2 | 0.74 |
| righttri_circle | 1 | 4 | 191 | 152 | 7.7 | 6.0 | 0.78 |
| righttri_circle | 1.5 | 4 | 406 | 432 | 12.6 | 9.3 | 0.74 |
| righttri_circle | 2.5 | 3 | 1390 | 2054 | 32.0 | 24.4 | 0.76 |
| righttri_circle | 4 | 2 | 454 | 630 | 12.3 | 9.2 | 0.75 |
| righttri_line | 1 | 5 | 186 | 124 | 7.8 | 6.5 | 0.83 |
| righttri_line | 1.5 | 2 | 76 | 56 | 3.1 | 2.6 | 0.84 |
| righttri_line | 2.5 | 6 | 615 | 670 | 17.5 | 13.2 | 0.75 |
| righttri_line | 4 | 2 | 184 | 198 | 5.2 | 4.1 | 0.79 |
| ring | 1 | 45 | 3261 | 3228 | 109.1 | 82.3 | 0.75 |
| ring | 1.5 | 3 | 218 | 198 | 7.9 | 5.6 | 0.71 |
| ring | 2.5 | 4 | 1729 | 2526 | 40.9 | 30.9 | 0.76 |
| ring | 4 | 48 | 25535 | 39184 | 621.8 | 460.5 | 0.74 |
| sculpt_cylinder | 1 | 3 | 147 | 216 | 7.0 | 5.7 | 0.81 |
| sculpt_cylinder | 4 | 3 | 867 | 1536 | 34.5 | 27.6 | 0.80 |
| sculpt_plane | 1 | 3 | 147 | 216 | 6.9 | 5.6 | 0.81 |
| sculpt_plane | 4 | 3 | 867 | 1536 | 34.6 | 27.8 | 0.80 |
| sculpt_sphere | 1 | 3 | 147 | 216 | 7.2 | 5.7 | 0.79 |
| sculpt_sphere | 4 | 4 | 1164 | 2048 | 46.1 | 36.7 | 0.80 |
| sculpt_torus | 1 | 3 | 147 | 216 | 7.2 | 5.7 | 0.79 |
| sculpt_torus | 2.5 | 1 | 297 | 512 | 11.1 | 9.2 | 0.83 |
| sculpt_torus | 4 | 3 | 867 | 1536 | 34.6 | 27.9 | 0.81 |
| sphere | 1 | 47 | 2772 | 3194 | 100.6 | 75.4 | 0.75 |
| sphere | 1.5 | 4 | 244 | 304 | 9.0 | 6.6 | 0.73 |
| sphere | 2.5 | 4 | 1304 | 1958 | 32.5 | 24.7 | 0.76 |
| sphere | 4 | 49 | 23691 | 39516 | 619.1 | 467.9 | 0.76 |
| square_circle2 | 1 | 5 | 332 | 302 | 11.6 | 9.0 | 0.78 |
| square_circle2 | 1.5 | 4 | 254 | 208 | 9.3 | 7.1 | 0.76 |
| square_circle2 | 2.5 | 5 | 1315 | 1734 | 33.5 | 25.4 | 0.76 |
| square_circle2 | 4 | 5 | 1822 | 2478 | 44.5 | 33.8 | 0.76 |
| torus | 1 | 46 | 3618 | 4934 | 117.7 | 87.3 | 0.74 |
| torus | 1.5 | 5 | 867 | 1190 | 24.2 | 17.9 | 0.74 |
| torus | 2.5 | 4 | 2660 | 4322 | 62.8 | 46.7 | 0.74 |
| torus | 4 | 45 | 37983 | 68186 | 1051.0 | 805.3 | 0.77 |
| tube | 1 | 45 | 3845 | 3766 | 120.7 | 92.5 | 0.77 |
| tube | 1.5 | 1 | 80 | 72 | 2.9 | 2.0 | 0.69 |
| tube | 2.5 | 3 | 1598 | 2334 | 36.3 | 27.3 | 0.75 |
| tube | 4 | 48 | 30783 | 46596 | 779.0 | 587.5 | 0.75 |

TOTAL: C++ 5459.9 us, Rust 4100.1 us, Rust/C++ 0.751 (sum of per-case minima over 881 cases)
slowest relative cases:
  1.21 torus/hollow70_squarehole/lod4 C++=28.0 Rust=33.9
  1.19 torus/hollow50_squarehole/lod4 C++=28.3 Rust=33.7
  0.91 righttri_line/base/lod1.5 C++=1.1 Rust=1.0
  0.91 righttri_line/base/lod1 C++=1.1 Rust=1.0
  0.91 equaltri_line/base/lod1.5 C++=1.1 Rust=1.0
  0.88 box/hollow_cutS/lod1 C++=1.7 Rust=1.5
## Rung two: PNG, TGA, BMP, DXT (and the JPEG verdict)

The raster codecs other than JPEG 2000 in `indra/llimage`, measured one by
one against the C++ they replace and ported only where the port is at least
as fast and reproduces the viewer's output. The crate is `vkstorm-image` in
vkstorm-core-rust; the oracle (the viewer's C++ codecs standalone on its
prebuilt libraries) and the harness are in its `tools/image-eval`.

### Keep-or-port judgement

| Codec | Decode | Encode | Judgement |
|---|---|---|---|
| PNG (`LLImagePNG`, was `LLPngWrapper` on libpng 1.6.44 + zlib-ng 2.2.3) | Rust | Rust | Ported. Decode byte-identical and 0.3 to 0.8 of libpng's time; encode decoded-identical, 0.3 to 0.9 of libpng's time, files within a stated size margin (below). The viewer's own code no longer uses libpng; the library stays in the build only because the prebuilt freetype decodes colour bitmap fonts with it (`FreeType.cmake` now carries that dependency). |
| TGA (`LLImageTGA`) | Rust | Rust | Ported: a 1:1 port, byte-identical, including `decodeAndProcess` for avatar masks; equal or faster. |
| BMP (`LLImageBMP`) | Rust | Rust | Ported: 1:1, byte-identical, faster. |
| DXT (`LLImageDXT`, with `LLImageBase::generateMip`) | Rust | Rust | Ported: 1:1, byte-identical (padding bytes zero where the C++ left them uninitialised), faster. |
| JPEG (`LLImageJPEG` on libjpeg-turbo 3.0.4) | C++ | C++ | Kept (owner's decision on the measurements). The Rust candidates (zune-jpeg 0.5 decode with its x86 SIMD, jpeg-encoder 0.7 with AVX2) do not beat libjpeg-turbo at 2048x2048: decode 0.98x (grey) to 1.20x (RGB) and encode 2.4x to 4.7x the time (table below), and not bit-exact (decode up to 9 code values at the bottom image row of 4:2:0 images, interior up to 3; encode up to 59 on images smaller than one MCU). The candidate stays in the crate behind the `jpeg` feature with its measurements; `LLImageJPEG` and `LLImageDimensionsInfo`'s JPEG probe are unchanged and libjpeg-turbo stays in the build. |

`image`'s TGA and BMP readers were not candidates: they cannot reproduce the
viewer's output (its 5-bit expansion table, the opaque-alpha compaction to 3
components, the colour-map clamping, the BMP mask handling), so the viewer's
own code was ported instead.

### What the port reproduces

- PNG decode: libpng's transformations as the wrapper requested them
  (palette and low-depth grey expansion, grey to RGB so every decode is 3 or
  4 components, tRNS to alpha, 16-bit strip to the high byte, gamma
  correction to a 2.2 screen when the gAMA chunk makes it significant by
  libpng's 5% fixed-point rule, with libpng's 8-bit table and its 16-to-8
  boundary table including the sBIT shift). Interlaced files decode. The
  `png` crate does chunks, unfiltering and inflate; the transformations are
  in the crate so order and rounding match. Differences only on malformed
  input: the `png` crate rejects a CRC error in an ancillary chunk and a
  tRNS chunk on an alpha colour type, where libpng warned and continued.
- PNG encode: 8-bit samples, colour type by component count, libpng's
  minimum-sum-of-absolute-differences filter choice (identical on 150/150
  real textures), deflate level 6 filtered with libpng's reduced window for
  small images and 8192-byte IDAT chunks, through zlib-rs. The settings are
  pinned by name in the crate (`png::DEFLATE_LEVEL`, `DEFLATE_STRATEGY`,
  `DEFLATE_MEM_LEVEL`, `FILTER_HEURISTIC`) and `png`/`zlib-rs` are pinned to
  exact versions; an upgrade re-runs `tools/image-eval` and re-records its
  results.

  **Deliberate exception to byte identity (owner's decision).** zlib-rs and
  the viewer's zlib-ng snapshot (2.2.3-dev) choose different matches on some
  inputs, so the files are not always byte-identical and matching that
  snapshot's deflate is not worth doing: no consumer compares PNG bytes. The
  gate is therefore decoded-identical output, each file within 20% (or 128
  bytes, for tiny files where a deflate block boundary is a large fraction
  of the file) of libpng's size for the same raster, and no growth of the
  corpus total. Measured: over 500 real skin textures 4.4% byte-identical,
  the rest 0.86 to 1.16 of libpng's size (median 0.996, total -0.5%); over
  the 1014 PNGs of the default skin 0.85 to 1.21 (one 499-byte texture is
  103 bytes larger; the next largest is 1.16), median 0.996, total -0.14%;
  the synthetic fixtures 25 of 26 byte-identical. zlib-ng 2.2.4 differs
  from the viewer's snapshot the same way. The gate is asserted by the
  crate's `tests/parity.rs` (61 PNG encodes) and by
  `INTEGRATION_TEST_llimagecodecs` over the default skin.
- TGA, BMP, DXT: the viewer's behaviour including its quirks (run-length
  colour-mapped TGA always flipped, opaque 32-bit TGA compacted to 3
  components, 8-bit BMP needing a 256-entry table, the BMP V4 colour-table
  offset, `getMipData` returning one pixel). Where the C++ read or wrote
  outside its buffers on malformed input, or aborted with `LL_ERRS`, the port
  reports an error through `setLastError`. `LLImageDXT::encodeDXT`'s unused
  explicit-mips path (`extractMip`) was not ported.
- Error messages: the strings the C++ passed to `setLastError` are kept.

### Measurements

Ryzen 9 9950X3D, Windows, single thread, best of N runs (N = 200 at 64 px,
5 at 2048 px), same inputs for both sides: C++ is the oracle built with MSVC
`/O2 /arch:AVX2` on the viewer's prebuilt libraries, Rust is the cargo
release profile with `target-cpu=x86-64-v3`. `decode` includes the header
parse and the output allocation on both sides. Full CSV:
vkstorm-core-rust `tools/image-eval/results/`.

| Image | Codec | Op | C++ ms | Rust ms | Rust/C++ |
|---|---|---|---:|---:|---:|
| 64x64 RGB | PNG | decode | 0.0650 | 0.0329 | 0.51 |
| 64x64 RGB | PNG | encode | 0.2288 | 0.1507 | 0.66 |
| 64x64 RGB | JPEG | decode | 0.0130 | 0.0128 | 0.98 |
| 64x64 RGB | JPEG | encode | 0.0101 | 0.0221 | 2.19 |
| 64x64 RGB | TGA | decode | 0.0016 | 0.0012 | 0.75 |
| 64x64 RGB | TGA | encode | 0.0014 | 0.0002 | 0.14 |
| 64x64 RGB | BMP | decode | 0.0016 | 0.0002 | 0.12 |
| 64x64 RGB | BMP | encode | 0.0021 | 0.0013 | 0.62 |
| 64x64 RGB | DXT | decode | 0.0001 | 0.0002 | 2.00 |
| 64x64 RGB | DXT | encode | 0.0031 | 0.0024 | 0.77 |
| 64x64 RGBA | PNG | decode | 0.0811 | 0.0439 | 0.54 |
| 64x64 RGBA | PNG | encode | 0.3044 | 0.1992 | 0.65 |
| 64x64 RGBA | TGA | decode | 0.0022 | 0.0018 | 0.82 |
| 64x64 RGBA | TGA | encode | 0.0103 | 0.0007 | 0.07 |
| 64x64 RGBA | BMP | decode | 0.0016 | 0.0002 | 0.12 |
| 64x64 RGBA | BMP | encode | 0.0024 | 0.0014 | 0.58 |
| 64x64 RGBA | DXT | decode | 0.0002 | 0.0002 | 1.00 |
| 64x64 RGBA | DXT | encode | 0.0039 | 0.0029 | 0.74 |
| 64x64 gray | PNG | decode | 0.0255 | 0.0108 | 0.42 |
| 64x64 gray | PNG | encode | 0.1058 | 0.0769 | 0.73 |
| 64x64 gray | JPEG | decode | 0.0096 | 0.0071 | 0.74 |
| 64x64 gray | JPEG | encode | 0.0083 | 0.0088 | 1.06 |
| 64x64 gray | TGA | decode | 0.0001 | 0.0001 | 1.00 |
| 64x64 gray | TGA | encode | 0.0002 | 0.0001 | 0.50 |
| 64x64 gray | BMP | decode | 0.0024 | 0.0018 | 0.75 |
| 64x64 gray | BMP | encode | 0.0012 | 0.0002 | 0.17 |
| 64x64 gray | DXT | decode | 0.0001 | 0.0001 | 1.00 |
| 64x64 gray | DXT | encode | 0.0014 | 0.0013 | 0.93 |
| 2048x2048 RGB | PNG | decode | 41.7218 | 29.6378 | 0.71 |
| 2048x2048 RGB | PNG | encode | 259.3951 | 193.9272 | 0.75 |
| 2048x2048 RGB | JPEG | decode | 6.5370 | 7.8529 | 1.20 |
| 2048x2048 RGB | JPEG | encode | 5.2671 | 24.4775 | 4.65 |
| 2048x2048 RGB | TGA | decode | 3.2395 | 2.5690 | 0.79 |
| 2048x2048 RGB | TGA | encode | 5.0649 | 1.9423 | 0.38 |
| 2048x2048 RGB | BMP | decode | 3.3742 | 1.7533 | 0.52 |
| 2048x2048 RGB | BMP | encode | 5.4968 | 2.9417 | 0.54 |
| 2048x2048 RGB | DXT | decode | 1.9957 | 1.7389 | 0.87 |
| 2048x2048 RGB | DXT | encode | 8.6551 | 4.8993 | 0.57 |
| 2048x2048 RGBA | PNG | decode | 51.4445 | 41.6253 | 0.81 |
| 2048x2048 RGBA | PNG | encode | 349.6714 | 267.5622 | 0.77 |
| 2048x2048 RGBA | TGA | decode | 3.7370 | 2.9045 | 0.78 |
| 2048x2048 RGBA | TGA | encode | 5.5314 | 2.3958 | 0.43 |
| 2048x2048 RGBA | BMP | decode | 3.2403 | 1.7374 | 0.54 |
| 2048x2048 RGBA | BMP | encode | 6.0901 | 2.8554 | 0.47 |
| 2048x2048 RGBA | DXT | decode | 3.0874 | 2.4910 | 0.81 |
| 2048x2048 RGBA | DXT | encode | 11.1819 | 6.4492 | 0.58 |
| 2048x2048 gray | PNG | decode | 21.7546 | 14.9053 | 0.69 |
| 2048x2048 gray | PNG | encode | 90.3657 | 65.8580 | 0.73 |
| 2048x2048 gray | JPEG | decode | 6.6616 | 6.5144 | 0.98 |
| 2048x2048 gray | JPEG | encode | 4.5907 | 10.9619 | 2.39 |
| 2048x2048 gray | TGA | decode | 0.7257 | 0.5724 | 0.79 |
| 2048x2048 gray | TGA | encode | 1.3457 | 0.7290 | 0.54 |
| 2048x2048 gray | BMP | decode | 4.0630 | 3.0186 | 0.74 |
| 2048x2048 gray | BMP | encode | 2.1220 | 0.6545 | 0.31 |
| 2048x2048 gray | DXT | decode | 0.6109 | 0.4867 | 0.80 |
| 2048x2048 gray | DXT | encode | 2.5043 | 1.7788 | 0.71 |

Header reads (`updateData`) are microseconds on both sides and not listed.
The DXT 64 px decode is 0.1 vs 0.2 microseconds, below the timer's
resolution. The JPEG rows are the candidate that was not adopted.

### Tests

- vkstorm-core-rust `crates/vkstorm-image/tests/parity.rs` over 800
  oracle fixtures (every PNG colour type and depth with tRNS, gAMA and sBIT,
  every TGA type, pixel size, origin and colour map, the BMP variants, DXT
  files from the C++ encoder, and a small real file of every variant class
  in the viewer): decodes, headers and the TGA, BMP and DXT encodes are
  byte-identical; the PNG encodes are byte-identical or decoded-identical
  within 5%; the viewer's error messages match. All 13800 PNG and TGA files
  under `newview/skins` decode identically to the C++ (CRC comparison).
- `INTEGRATION_TEST_llimagecodecs` decodes every PNG and TGA of the default
  skin through `LLImagePNG`/`LLImageTGA` against a CRC manifest of the C++
  output (`llimage/tests/llimagecodecs_manifest.txt`), round-trips the four
  encoders, runs `decodeAndProcess` on the character masks and checks the
  error messages.

## Rung three: the message template system

The data side of the viewer's UDP message system is Rust: `vkstorm-messages`
in vkstorm-core-rust (`crates/vkstorm-messages`), reached through `vks_msg_*`
and `vks_dp_*` in `indra/rust/vkstorm-ffi/src/messages.rs` (header
`indra/rust/include/vkstorm_ffi.h`; `llmessage` now links `vkstorm_ffi`). No
runtime choice and no fallback. The C++ classes keep their names and public
interface (`LLTemplateTokenizer`, `LLTemplateParser`, `LLMessageTemplate`,
`LLMessageBlock`, `LLMessageVariable`, `LLTemplateMessageBuilder`,
`LLTemplateMessageReader`, `LLDataPacker` and its three subclasses,
`LLMessageSystem::zeroCodeExpand`), so no caller changed; the former C++
bodies are gone and live on only in the recorded corpus.

Scope decision. The grid profile (Second Life or OpenSim) is chosen at login
and fixed for the session; nothing is negotiated per region. The crate
carries the `opensim` Cargo feature, matching the CMake `OPENSIM` option, as
the place for OpenSim-only legacy paths; a Second Life-only build contains
none of them. The template language, the packing rules and the data packers
are the same on both grids, so in this rung the feature marks the contract
and gates no code: the first OpenSim-only path to be ported will live behind
it. No feature was cut.

### Keep-or-port judgement

| Part | Decision | Why |
|---|---|---|
| `LLTemplateTokenizer`, `LLTemplateParser` (`message_template.msg`) | Rust (`parser.rs`) | Pure parsing; the tokenizer keeps the C++ cursor semantics the viewer's parser tests drive directly. The C++ classes turn the parsed descriptions into `LLMessageTemplate` objects. |
| `LLMessageTemplate`, `LLMessageBlock`, `LLMessageVariable` | C++ (unchanged), plus a compiled handle | They are the viewer's representation: `message.cpp`, the dispatcher and every handler registration hold them. Each compiles itself on first use into the crate's template with its canonical name pointers as keys; adding a block drops the handle. |
| `LLTemplateMessageBuilder` (block repeats, every `add*`, `buildMessage`, `isMessageFull`, zero-coding) | Rust (`builder.rs`, `zerocode.rs`) | 1.9-3.8x on `AgentUpdate` (table below) with no allocation after warm-up; the C++ allocated a map node and a buffer per variable. The typed `add*` keep their conversions (`htons`, `packToVector3`, bool) in C++ and hand bytes over. |
| `LLTemplateMessageReader` (`decodeTemplate`, `decodeData`, every getter, `getNumberOfBlocks`, `getSize`) | Rust (`reader.rs`) | 39-57x to decode a 1.2 KB `ObjectUpdate`, 5.0-7.2x with the handler's getters read (table below); the C++ built a map of maps with a heap copy per variable, which the profile shows is where its time went. The reader records offsets into one copy of the packet. Trust and ban checks, the handler dispatch with its timing, the finite-float and quaternion checks of the getters stay in C++ around it. |
| `zeroCodeExpand`, `zeroCodeAdjustCurrentSendTotal` | Rust (`zerocode.rs`) | Byte transformation; `message.cpp` keeps the statistics and the exception callbacks. |
| `LLDataPackerAsciiBuffer` | Rust (`datapacker.rs`, `ascii.rs`) | The C++ object keeps its buffer state (pointer, cursor, size, write and name flags) and hands it to the crate per call; the text conversions are where the work is. |
| `LLDataPackerBinaryBuffer` | Rust for strings, sized binary data and the fixed-point conversion; **inline C++ for the fixed-size copies** (every scalar, vector, colour, UUID, `S16` and fixed binary) | Measured (table below): a call into the crate per 4-byte field costs more than the bounds check and copy it replaces (the binary packer row was 2.7x slower on Linux through the crate), so the copies stay the inline C++ they were. The crate's `BinaryBuffer` remains the reference, checked by the crate's corpus test; the C++ class is checked by the integration test. |
| `LLDataPackerAsciiFile` | Rust text, C++ I/O | The `FILE*`/stream reads and writes stay in C++ (I/O); the value text is formatted and parsed by the crate. Unused by the viewer; covered by its tests. |
| `LLDataPacker::packFixed`/`unpackFixed` | Rust | The float-to-fixed arithmetic, including the C++ cast's wrap at the type's width. |
| `LLMsgData`, `LLMsgBlkData`, `LLMsgVarData` | C++ (unchanged) | The exchange format of `copyFromMessageData` and of `LLSDMessageBuilder`'s conversion, and what the viewer's tests build by hand. The template reader assembles one on request (`copyToBuilder`, forwarding) and the builder on `getCurrentMessage()`. |
| `LLSDMessageBuilder`, `LLSDMessageReader` | C++ | They build and read the C++ `LLSD` class, which is out of scope by design (rung two); the only transformations in them (`ll_sd_from_U32/U64/ipaddr`, vector and quaternion to LLSD arrays) are a few lines of `llsdutil`. There is no `llsdmessage.cpp` in this tree. |
| `LLMessageSystem` send and receive loops, `LLCircuit`, `LLPacketRing`, reliability, resends, acks | C++ | Sockets and timing: I/O, not data transformation. |
| The dispatch table and handlers in newview, `LLTemplateMessageDispatcher` | C++ | Application logic on top of the reader's getters. |

### The boundary

- Keys: blocks and variables are identified by 64-bit keys, the canonical
  string pointers from `LLMessageStringTable` that the viewer already
  compares by address. A compiled template carries them in a small
  open-addressed table per message and per block, in place of the
  `std::map` lookups by pointer the C++ did.
- Builder: `newMessage` hands over the compiled template (shared by
  reference count, so a template rebuilt by `addBlock` while a message is
  open keeps the old definition for that message); `nextBlock` and `add*`
  return codes the C++ turns into the `LL_ERRS`/`LL_WARNS` it always gave;
  `buildMessage` packs into the caller's buffer; `compressMessage` codes
  into the static send buffer as before.
- Reader: `validateMessage` asks the crate for the message number and keeps
  the number map, the trust and ban checks in C++; `readMessage` decodes
  into the reader's copy of the packet and returns the reads past the end
  (logged and reported through `callExceptionFunc` as before, after the
  decode rather than during it); getters copy from the recorded slots.
- Data packers: each call passes `{data, size, cur, write_enabled,
  include_names}` and takes the cursor back. The text form's warnings keep
  their wording; the keyword found on a mismatch is no longer quoted.
- Panics are caught at the boundary and reported as `VKS_MSG_ERR_INTERNAL`
  with the text in `vks_msg_last_error()`. The crate has no `unsafe`
  (`#![forbid(unsafe_code)]`) and no dependencies.

### Behaviour kept, settled, and documented as different

Every quirk that affects the wire bytes or a caller's result is kept and
covered by the corpus: the tokenizer's line numbering (each `\r` and `\n`
counts, so CRLF files number lines by two), `strtoul(…, 0)` message numbers,
the `Variable 1` clamp to 255 bytes with the last byte zeroed, trailing
variable blocks absent from short packets reading as zero repeats, fixed
fields past the end reading as zeros with a ran-off-end report, the text
packers' line limit of 511 bytes and their cursor arithmetic, `snprintf`
truncation in the text buffer, the measuring packer's estimates (a UUID
counts 65 bytes), `packS16` advancing on failure, `unpackBinaryData`
delivering the size field even when the data cannot follow, `packFixed`
reporting success regardless and its cast's wrap (`2^(int+frac)` becomes 0
in 8 and 16 bits), and the stream file form cutting every `%f` to 19
characters (its 20-byte buffer).

Settled as one product behaviour on both platforms (the fixture carries the
stated expectation, recorded as such by the oracle test, and the C++
oracle on Windows and Linux agreed everywhere else: the two recordings are
byte-identical):

- A variable-length field whose declared size runs past the packet is empty
  and reported as a read past the end, and the decode position still
  advances by the declared size. The C++ copied the declared number of
  bytes from beyond the packet without a check (up to 65535 bytes past an
  8 KB buffer for a `Variable 2` field); that memory bug is closed.
- A packet shorter than the minimum valid size has no message number (the
  C++ read whatever followed it in its buffer).
- The text packers' `%d` and `%u` saturate at the 32-bit range (the MSVC
  runtime's result; glibc went through a 64-bit `long` and truncated), a
  conversion that finds no number leaves the caller's value unchanged (the
  C++ assigned an uninitialised local for `U8`, `U16`, `S16`, the colour
  components, the hex bytes and `unpackFixed`), a NaN read back is the
  canonical quiet NaN of its sign (0x7FC00000 / 0xFFC00000; the runtimes
  gave different payloads), and non-finite floats are written `inf`,
  `-inf`, `nan`, `-nan` (the MSVC runtime wrote `-nan(ind)`). Hexadecimal
  float input is not read.
- The text packers never read beyond the buffer size they were given; past
  its end every read sees an empty line (the C++ read on through memory).
  `unpackBinaryData` in the text forms writes at most the bytes present in
  the line (167), never the declared count.
- `LLDataPackerAsciiFile` writes the indent it is given (the C++ filled a
  string past its size, and the indent came out empty); the reader skips
  it, so files written before read the same.

Differences on the error path, all `LL_ERRS` either way: a `Multiple` block
gets exactly its number of repeats (the C++ accepted one extra and failed
when building); `removeLastBlock` on a block with a single repeat leaves it
in place and returns false (the C++ deleted it and then reported that it
had not; nothing in the viewer calls it); the `MAX_BUFFER_SIZE` overflow
checks of `buildMessage` are made against the buffer given.

### Measurements

`INTEGRATION_TEST_llmessagetemplate` with `VKS_MSG_BENCH=20000`: the C++ at
the pre-port commit and the port, each built in the same tree and run three
times back to back on an idle machine (AMD Ryzen 9 9950X3D; Windows 11, MSVC
19.44, the RelWithDebInfo configuration; Ubuntu 26.04 under WSL, GCC 15, the
CI Release configuration); the crate at `opt-level 3`, fat LTO, `x86-64-v3`.
Each cell is the median of the three runs of the median of 7 rounds of 20000
operations, in microseconds. The Rust column is the viewer-side path: the
C++ classes, the C ABI and the crate. The harness runs at the viewer's log
level (`LLError::LEVEL_INFO`; the test runner's default is `LEVEL_DEBUG`,
see below).

The packets are constructed, not captured. Nothing crosses the message
system before a login: the fresh-profile launch to the login screen and
back out (the launch note below) ends with the message log summary at
0 packets sent and 0 received, and no account was logged in to record
one. The harness builds them with `LLTemplateMessageBuilder` from
representative values: an `ObjectUpdate` with `RegionData` and four
`ObjectData` repeats, each a full update of a prim (local and full ids, CRC,
`PCODE_VOLUME`, material, scale, the 60-byte `ObjectData` motion field, the
path and profile parameters of a box or a hollow cylinder, a texture entry
with one or two faces, a 16-byte texture animation on one, a `NameValue` and
hover text with colour on one, an empty `ExtraParams` list, null sound and
owner): 1274 bytes, 600 zero-coded. An `AgentUpdate` with agent and session
ids, body and head rotation, camera position and axes, draw distance,
control flags: 121 bytes. The builder's bytes are the same on both sides
(the corpus test checks that over 1912 messages). The handler getters are
the sequence `LLViewerObjectList::processObjectUpdate`,
`LLViewerObject::processUpdateMessage`, `unpackVolumeParams` and
`unpackTEMessage` issue for such a prim: `getNumberOfBlocks`, two region
fields and 46 to 49 getters per object (every typed getter, `getSize`,
`getBinaryData`, `getString`).

| operation | Windows C++ us | Windows Rust us | Rust/C++ | Linux C++ us | Linux Rust us | Rust/C++ |
|---|---:|---:|---:|---:|---:|---:|
| validateMessage of the ObjectUpdate (1274 B) | 0.010 | 0.016 | 1.57 | 0.008 | 0.014 | 1.88 |
| validateMessage + readMessage | 11.660 | 0.203 | 0.02 | 7.777 | 0.198 | 0.03 |
| validateMessage + readMessage + the handler's getters | 14.140 | 1.958 | 0.14 | 9.561 | 1.905 | 0.20 |
| the handler's getters alone (after a readMessage) | 2.417 | 1.699 | 0.70 | 1.560 | 1.601 | 1.03 |
| zeroCodeExpand of its zero-coded form (600 B) | 0.290 | 0.248 | 0.85 | 0.279 | 0.286 | 1.03 |
| pack AgentUpdate (121 B): newMessage, nextBlock, 12 add*, buildMessage, compressMessage | 1.115 | 0.294 | 0.26 | 0.428 | 0.231 | 0.54 |
| LLDataPackerBinaryBuffer: 50 packs and 50 unpacks of UUID, colour, float, U8, S16 | 0.407 | 0.305 | 0.75 | 0.214 | 0.073 | 0.34 |

| the C++ data structure alone (same code in both builds) | Windows pre-port us | Windows port build us | Linux pre-port us | Linux port build us |
|---|---:|---:|---:|---:|
| build and delete the LLMsgData of the ObjectUpdate, alternating | 11.618 | 12.042 | 7.484 | 7.183 |
| build only, 1000 kept alive per round | 13.368 | 13.408 | 4.770 | 4.814 |
| delete only, the same 1000 | 5.497 | 5.754 | 2.733 | 2.645 |

Every row is at or below the C++ time on both platforms except the three
noted here. `zeroCodeExpand` and the handler's getters on Linux are at
parity (7 ns and 40 ns slower, within 3 %, consistent across runs; the
Windows figures are 15 % and 30 % faster). `validateMessage` alone is 6 ns
slower: it is one call into
the crate (message number and the template's compiled handle) against an
inline switch; it is paid once per packet and is 0.05 % of the pre-port
decode it precedes.

Where the pre-port microseconds go. The second table is the cost of the
C++ reader's own output, `LLMsgData` (a `std::map` of block objects, each an
`LLIndexedVector` of variables with a `std::map<const char*, size_t>` index
and a heap copy of every variable's bytes), built by the same code in both
executables: building and freeing it for this packet is 11.6 us on Windows
and 7.5 us on Linux, which is the whole of the pre-port
`validateMessage + readMessage` row (11.66 and 7.78 us). The
breakdown on Windows (`rdtsc` probes in a scratch build of the pre-port
`decodeData`, 280000 calls): deleting the previous `LLMsgData` 3.2 us, the
template walk 24.7 us at the runner's log level, of which `addVariable` (map
insert and vector growth, copying the entries) 5.1 us, `addData` (map
lookup, `new U8[]`, copy) 3.2 us, block allocation 0.4 us, the handler call
0.01 us. On Linux, `perf` over the decode loop alone (validate and read,
20000 x 5 iterations at `LEVEL_INFO`, resolved with `addr2line`):
libc 51 % (`malloc` and `free` internals: `_int_malloc`, `_int_free_create_chunk`,
`_int_free_merge_chunk`, the tcache, `unlink_chunk`), libstdc++ 12 %
(red-black tree insert and rebalance, `operator new` and `delete`), the
executable 38 %, of which the `std::map<const char*, size_t>` index of
`LLIndexedVector` 12 %, `decodeData` itself 3.7 %, `LLMsgVarData::addData`
2.7 %, `addVariable` 1 %, `htolememcpy` 1 %.
The port's decode loop profiles as 88 % of its samples in the executable (48 % in
`vks_msg_reader_decode`, 12 % `decode_data`, 7 % the slot vector, 3.5 %
each for `clear` and `set_message`) and 9 % in libc.

The runner's log level, and the earlier figure. An earlier version of this
table reported 22.8 us (Windows) and 9.1 us (Linux) for the pre-port decode
of a smaller packet. The pre-port `decodeData` has an `LL_DEBUGS("LLMessage")`
line per template block; the test runner (`indra/test/test.cpp`) sets
`LLError::setDefaultLevel(LEVEL_DEBUG)` with no output, so in the test
executable each of those lines formatted its message and went through
`LLError::Log::flush` with nothing to write it to, which is 6.5 us per line
on MSVC (an `ostringstream`, the log mutex, the timestamp and location
strings) and about 1.7 us on glibc. A replica of the pre-port loop compiled
into the test, bisected: 25.1 us with the `LL_DEBUGS` line, 24.7 us without
the fast-timer block, 11.7 us without the `LL_DEBUGS` line, 12.1 us with
neither. In the viewer the line is a cached `shouldLog()` check (false at
the default level) and costs nothing measurable; the port has no per-block
log line. The harness now sets `LEVEL_INFO` before timing, and the table
above is at that level. The corrected ratio of the decode row is 57x on
Windows and 39x on Linux, from 130x and 58x.

What the port changed that explains the remaining difference: the reader
keeps one copy of the packet and a slot table of (offset, size) per
variable occurrence, filled by one pass over the template with no
allocation after the first packet of a template (the vectors are reused);
block and variable names are the canonical `LLMessageStringTable` pointers
the viewer already passes, used as 64-bit keys in a small open-addressed
index built once per template. The pre-port reader's per-packet work was
dominated by `malloc`/`free` and red-black tree nodes, so removing them
removes the time; what remains per packet is the template walk itself. The
getters are of the same order on both sides (0.70x on Windows, 1.03x
on Linux): each is a key lookup and a copy in both implementations, with
one C ABI call in the port's.

The binary data packer row is the inline C++ copies kept after the
measurement described above (the same code before and after apart from the
bounds-checked reads; its first port through the crate measured 0.28 us on
Linux against the 0.10 us it replaced).

### Tests

- vkstorm-core-rust `crates/vkstorm-messages`: 19 unit tests (the
  tokenizer and parser on the viewer's own test inputs, the builder and
  reader round trip and truncation, zero-coding runs, wraps and overruns,
  the text conversions, fixed point) and `tests/corpus.rs` over the
  recorded corpus (`tests/fixtures/llmessage_corpus.vksm`, 10 MB, with
  `message_template.msg`): the parsed template (478 messages), 1912 packed
  messages with their zero-coded forms, 7393 reads of complete, cut and
  corrupted packets (every variable's bytes, size and repeat count), 1080
  zero-code expansions and 70 data packer cases (40 from the packer
  scripts in four forms with cut and short-buffer variants, 30 with the
  last text value cut to its name, where every read must leave the caller's
  value untouched). `cargo bench --bench
  hot_paths` is the crate-side harness.
- `INTEGRATION_TEST_llmessagetemplate` (viewer): the same corpus through
  the real C++ classes into Rust and back (the builder's typed `add*`, the
  reader's typed getters, `zeroCodeExpand` with its exception callbacks,
  the three packer classes), with `VKS_MSG_DUMP=<file>` to record and
  `VKS_MSG_BENCH=<iterations>` to time (`VKS_MSG_BENCH_ONLY=decode` runs
  only the decode loop for a profiler, `VKS_MSG_PACKETS_OUT=<dir>` writes
  the constructed packets, which are the crate's bench fixtures). Passes on Windows and on Linux
  (the comparison runs against the oracle fail on the stated entries, by
  design, and are skipped while recording). Run by CI with the other
  focused tests.
- `lltest` (the legacy suite in `indra/test`): the `LLTemplateMessageBuilder`
  (45 tests), `LLSDMessageBuilder`, `LLSDMessageReader`, `LLMessageSystem`
  and `LLMessageConfig` groups pass unchanged, and `LLMessageTemplateParser`
  (8 tests) on Linux, where alone it is built; with the other groups of the
  executable, 295 tests on Windows, 302 passed and 1 known skip (the
  `httpdate` locale test) on Linux. `lldatapacker_tut.cpp` is not compiled
  into `lltest` in this tree (nor upstream); the packer classes are covered
  by the integration test's corpus.
- The viewer builds, stages and reaches the login screen on Windows with
  the local welcome page (`skins/default/html/en/vulkanstorm/welcome.html`);
  the message template loads through the port ("Read 478 messages"). A
  session on the main grid received and decoded the region's object updates
  through the Rust reader with no message warnings in the log (an
  unintended login through credentials the machine's profile had
  remembered; closed at once, see the rung report), and a fresh profile
  with an empty login form fails cleanly.

## Rung four: the HTTP layer

The HTTP service of `indra/llcorehttp` is Rust: `vkstorm-http` in
vkstorm-core-rust (`crates/vkstorm-http`), reached through `vks_http_*` in
`indra/rust/vkstorm-ffi/src/http.rs` (header `indra/rust/include/vkstorm_ffi.h`;
`llcorehttp` links `vkstorm_ffi`). No runtime choice and no fallback. The
C++ classes keep their names and public interface (`HttpRequest`,
`HttpOptions`, `HttpHeaders`, `HttpResponse`, `HttpHandler`, `HttpStatus`,
`BufferArray`, `BufferArrayStream`), so no caller changed; the former C++
service (`HttpService`, `HttpRequestQueue`, `HttpReplyQueue`, `HttpPolicy`,
`HttpPolicyClass`, `HttpPolicyGlobal`, `HttpRetryQueue`, `HttpReadyQueue`,
`HttpOperation` and its subclasses, `HttpLibcurl`) is gone; `_httpservice.h`
keeps the state query (`HttpService::getState`, `isStopped`) the tests use.

### Transport judgement: libcurl bindings

The viewer's requirements decide it, before any measurement:

| requirement | libcurl through bindings | pure Rust (hyper/reqwest on rustls) |
|---|---|---|
| TLS stack identical to today's (OpenSSL 1.1.1w, the staged `ca-bundle.crt` through `CURLOPT_CAINFO`, the viewer's own certificate verification installed on the `SSL_CTX` by `LLAppCoreHttp::sslVerify` against `LLSecAPIHandler`'s store) | the same library, the same calls | a different TLS implementation and certificate path; the `SSL_CTX` callback has no counterpart |
| proxy settings as the viewer exposes them (`LLProxy::applyProxySettings`: HTTP and SOCKS5 with user/password, applied per handle) | the same function, called on each handle | re-implemented per client |
| HTTP/1.1 keep-alive and pipelining as today (libcurl 7.54.1 `CURLMOPT_PIPELINING` per policy class, the stall when a class is reconfigured) | identical | no HTTP/1.1 pipelining exists in hyper |
| the status and error codes callers switch on (`CURLE_*` through `HttpStatus::EXT_CURL_EASY`, `isRetryable`'s list, `CURLE_SSL_CACERT` from the certificate callback) | identical | an approximate mapping |
| per-request timeouts, byte ranges, chunked bodies, cookies (the per-handle cookie engine, carried across the handle cache), `If-Modified-Since`, `Accept-Encoding` | identical | reproducible with care, cookies and the handle-cache carry-over not |
| wire bytes where the viewer controls them | identical (same option setup, same order) | different request heads |

A pure-Rust client would therefore change TLS, proxy and error behaviour by
construction, which the rung forbids. The bindings are the crate's own
(`sys.rs`: the functions, option numbers and codes the transport uses,
declared against the stable libcurl ABI), not `curl-sys`: its build
script finds a system libcurl through vcpkg or pkg-config or builds its
bundled copy with the platform TLS stack, none of which is the libcurl the
viewer stages (OpenSSL, nghttp2, zlib-ng, prebuilt by autobuild). The
symbols resolve at the viewer's link (`vkstorm_ffi` carries `ll::libcurl`
as an interface dependency); the crate's own tests link a libcurl through
the `link-curl` feature, pointed at the viewer's prebuilt one with
`VKS_HTTP_CURL_LIB_DIR` and `VKS_HTTP_CURL_LIBS`. All `unsafe` in the crate
is in `sys.rs` and `transport.rs` (the libcurl calls and the callbacks that
get their request record back from libcurl's user pointer).

Measured (tables below), the port is at the C++ on small requests on both
platforms and faster on 1 MiB range downloads.

### Keep-or-port judgement

| Part | Decision | Why |
|---|---|---|
| `HttpService`: the request queue, the service thread loop, state, start and stop | Rust (`service.rs`) | The service itself. |
| `HttpPolicy`, `HttpPolicyClass`, `HttpPolicyGlobal`, `HttpReadyQueue`, `HttpRetryQueue`: per-class ready and retry queues, connection limits, pipelining depth, request-rate throttle, retry decision and backoff, the stall | Rust (`policy.rs`, `options.rs`) | The scheduling logic; one implementation of `isRetryable` (the C++ `HttpStatus::isRetryable` now calls it). |
| `HttpOperation` and its subclasses (`HttpOpRequest`, `HttpOpCancel`, `HttpOpSetGet`, `HttpOpStop`, `HttpOpNull`, `HttpOpSpin`), the option validation table | Rust (`request.rs`, `service.rs`) | The operations are the service's; the C++ `HttpRequest` only builds the request description and routes the reply. |
| `HttpLibcurl`: the multi handles, the easy-handle cache, `prepareRequest`'s option setup, the write/read/seek/header/debug callbacks, completion, cancellation, the pipelining sanity checks | Rust (`transport.rs`, `headers.rs`) | The transport; the option setup is reproduced call for call in the C++ order so the request heads are byte-identical. |
| `HttpReplyQueue` | Rust (`request.rs`) | One per `HttpRequest` instance, drained by `update()` through `vks_http_reply_fetch`. |
| `HttpStatus` strings and `isRetryable` | Rust (`status.rs`); the C++ keeps `toString`/`toTerseString`/`toHex` | The C++ strings call `curl_easy_strerror` directly, which the viewer's other users of `LLHttp` need anyway. |
| `HttpRequest` (public API) | C++ over the C ABI | The caller-facing class: request descriptions in, replies out, the handler map (the `HttpHandler::ptr_t` rides along the request as opaque user data). |
| `HttpOptions`, `HttpHeaders`, `HttpResponse`, `HttpHandler` | C++ (unchanged) | Value classes callers construct and read; their values cross the boundary by value. `HttpHeaders::appendNormal`'s rules are applied in the crate to received headers. |
| `BufferArray`, `BufferArrayStream` | C++ (unchanged) | The viewer's scatter/gather body representation, held by callers; bodies are copied in and out at the boundary (see below). |
| `LLHttp` (`initialize`, `cleanup`, `createEasyHandle`, `getCURLVersion`, `check_curl_code`) | C++ | Process-wide libcurl setup and the easy-handle template used by the XML-RPC login transaction, the crash logger and the media plugin: consumers of libcurl, not the HTTP core. |
| `HTTPStats` | C++ | A viewer statistics singleton; the service records into it through a hook. |
| `LLProxy::applyProxySettings` | C++ | The viewer's proxy settings; the service calls it on every handle through a hook, as `prepareRequest` did. |
| `LLAppCoreHttp::sslVerify` and the `SSL_CTX` callback | C++ | OpenSSL certificate verification against the viewer's store; the service installs the host's `CURLOPT_SSL_CTX_FUNCTION` body through a hook and reads the decision back. |
| `LLCoreHttpUtil` (`HttpCoroutineAdapter`, `HttpCoroHandler` and its LLSD, raw and JSON handlers, `requestPostWithLLSD` and friends, `responseToLLSD`, status mapping) | C++ | Coroutine plumbing on `LLCoros`, `LLEventPump` and the C++ `LLSD` class (out of scope by design, rung two). The data conversions in it already run in Rust: the LLSD bodies go through `LLSDSerialize` (rung two). What remains is the suspend/resume state machine and `LLSD` map building, which has no transformation to port. |
| `LLCoprocedureManager` pools | C++ | A pool of `LLCoros` coroutines with `LLEventMailDrop` queues feeding `HttpCoroutineAdapter`; coroutine scheduling, not HTTP. |
| The XML-RPC login transaction (`LLXMLRPCTransaction`) | C++ | It uses `HttpRequest` as a consumer and keeps its own body handling; nothing to fold in. |
| The capability name table and `LLViewerRegion`'s capability requests, the media plugin's HTTP | C++ | Consumers. |
| `llwebsocketmgr`, `lljsonrpcws` | C++ | WebSocket, a separate transport on websocketpp. |

### The boundary

- A request crosses as a description by value (`VksHttpRequestDesc`:
  policy class, method, URL, body bytes, byte range, headers, options)
  and is copied into the crate's request record; the `HttpHandler::ptr_t`
  rides along as opaque user data (`new HttpHandler::ptr_t(handler)`),
  handed back with the reply or released through a hook if the operation
  is never delivered. Handles are the crate's 64-bit counter cast to
  `HttpHandle`; they are never recycled (the C++ used the operation's
  address, which was reused as soon as it was freed).
- `update()` fetches replies one at a time from the instance's queue and
  builds the `HttpResponse` (status, body into a `BufferArray`, headers,
  content type, range, retries, URL, method, transfer statistics) for the
  handler, as `visitNotifier` did. Bodies are copied twice at the
  boundary (request `BufferArray` to the crate, reply bytes to a
  `BufferArray`); the C++ appended libcurl's chunks into `BufferArray`
  blocks, one copy, so a 1 MiB download costs one extra 1 MiB copy, which
  the measurements include.
- The host's facilities are a table of C function pointers
  (`VksHttpHooks`): log lines to `LL_DEBUGS/INFOS/WARNS("CoreHttp")`,
  `HTTPStats` records, `LLProxy::applyProxySettings` on each handle when
  `PO_LLPROXY` is set, the `SSL_CTX` setup and the OpenSSL verification
  callback (which writes its decision into the request's `VksHttpSslInfo`
  and keeps the full `HttpStatus`, message and certificate data included,
  for the reply), `LLThread::registerThreadID` on the service thread, and
  the release of user data.
- Policy options are validated in the crate with the C++ descriptor table
  (long or string, dynamic, global or class); the static path refuses
  everything while the thread runs with `HE_OPT_NOT_DYNAMIC`, the dynamic
  path goes through the service thread as before. The certificate
  callback itself stays a C++ `std::function`; the crate only records
  that one is set.
- Panics are caught at the boundary and reported as failures; the service
  loop catches panics per iteration, logs them and continues (the C++
  logged `LLContinueError` and crashed on anything else).

### Behaviour kept, settled, and documented as different

Kept, and covered by the recorded scenarios: the libcurl option setup of
every request in the C++ order (so the request heads are byte-identical:
`User-Agent: Vulkanstorm`, `Accept-Encoding: deflate, gzip`, `Connection:
keep-alive`, `Keep-alive: 300`, no `Pragma`, no `Expect`, the `Range:`
line of byte-range requests, `If-Modified-Since` from
`setLastModified`, the caller's headers last so they override), the
`HTTP/1.1` keep-alive on one connection per policy class slot, the
handle cache (the first freed handle is the template the rest are
duplicated from; a handle's cookie engine is kept across
`curl_easy_reset`, so a cookie set on one request is sent on the next
request that gets the same handle), `Content-Range` parsing with its
`sscanf` leniency and its rejection of inconsistent ranges as
`HE_INV_CONTENT_RANGE_HDR` (which is retried, as before), `Retry-After`
in delta seconds only and only under 30 seconds, the exponential backoff
`min * 2^retries` capped at `max` (the default maximum is 50 s, as the
constant reads, not the 5 s of its comment), the retry queue served from
its latest entry (the C++ `priority_queue` comparator), the per-class
concurrency limit (per-host limit times depth when pipelining), the
one-second throttle window, the stall while a class's multi handle is
reconfigured, the pipelining sanity checks (`X-LL-URL`, the range of a
206 against the range asked) that fail a request with a retryable 503
and turn pipelining off everywhere, cancellation of queued and active
requests with `HE_OP_CANCELED` to the request and success to the cancel,
`HE_HANDLE_NOT_FOUND` for an unknown handle, the option validation table
(type, dynamic, global or class, class range) with the same codes, and
every status code the callers switch on.

Settled as one product behaviour (the fixture carries the stated
expectation, recorded from the port and checked by hand):

- A retry starts from a clean status. The C++ kept the failed status of
  the previous attempt in the operation and only the header callback's
  status-line branch reset it; that callback was installed only when
  headers were wanted, a byte range was asked or `Retry-After` was in use
  (the default when an `HttpOptions` is passed). A request without
  options, or with `setUseRetryAfter(false)` and no headers wanted,
  therefore kept its first failure through every retry and was delivered
  as failed however the server answered (scenario `retry_503`,
  `retry_after_ignored`: six 503 attempts and a 503 delivered with the
  200 body of the last answer; now one retry and the 200).
- The `Retry-After` field of a request is zero until a header sets it.
  The C++ never initialised it, and read it in `retryOp` after any
  transport failure (no headers arrive on a timeout or a connection
  failure), so the first retry waited for whatever the heap held: 15 s
  on Windows in every recording, 1 s on Linux (scenario `timeout`; now
  the configured 1 s backoff on both).
- Handles are unique for the life of the service (a 64-bit counter); the
  C++ handle was the operation's address, reused as soon as the
  operation was freed. Callers were told handles become invalid on
  delivery; none compares them afterwards.
- `HttpResponse::getTransferStats` carries libcurl's figures (bytes,
  time, speed). The C++ read them after it had detached and recycled the
  handle, so they were always zero; `LLXMLRPCTransaction::transferRate`
  logged that zero.
- A request that cannot be prepared (no libcurl handle) or added to its
  multi handle is delivered as failed (`HE_BAD_ALLOC`, or the multi
  code); the C++ dropped it without a reply.
- `destroyService` stops the service before deleting `HTTPStats`; the
  C++ deleted the statistics singleton first and then waited up to a
  second for a thread that could still record into it.
- The service loop catches a panic in one pass, logs it and goes on; the
  C++ logged `LLContinueError` and crashed on any other exception.

### Parity scenarios

`INTEGRATION_TEST_llcorehttp_parity` drives `HttpRequest` through the
scenarios below against `tests/vkshttp_mock_server.py` (a raw HTTP/1.1
server on loopback that scripts its answers by path and records every
request it receives: arrival time, connection, exact request head, body)
and compares, per scenario, the request sequence on the wire (method,
target, HTTP version, the exact request head with the port normalised,
the body), the connection each request used, the gaps between the
attempts of one request (the retry timing, within 300 ms or 30 %), the
largest number of requests of each policy class in flight at once, and
the replies delivered to the handlers (status type and code, body, content
type, range, retry counts, headers when wanted, URL and method) with the
record made from the C++ before the port (`tests/fixtures/llcorehttp_parity.xml`,
recorded on Windows; the Linux C++ was compared against it before the
port and agreed on everything but the two settled scenarios and the
platform's own request spacing). Sequential scenarios compare in order;
the concurrent ones compare the request multiset and the per-class
overlap. `VKS_HTTP_RECORD=<file>` records, `VKS_HTTP_SCENARIOS=a,b`
selects, `VKS_HTTP_TRACE=<level>` traces, `VKS_HTTP_BENCH=<n>` times.

| scenario | what it exercises | result |
|---|---|---|
| `get_ok` | GET with and without headers wanted, the normalised reply headers, a no-op operation | identical |
| `methods` | POST, PUT, PATCH, DELETE, COPY, MOVE with and without bodies (`Content-Length`, no `Expect`), HEAD through `setHeadersOnly`, 300 KB POST and PUT bodies | identical |
| `range_small` | `requestGetByteRange` inside, past the end, unsatisfiable (416), open-ended and whole: the `Range:` line and the reply range triple | identical |
| `range_large` | a 1 MiB range, a 512 KiB range in the middle, a plain 1 MiB GET: bodies by hash | identical |
| `errors_4xx` | 404, 403, 400, 204, an unknown path: delivered as errors, never retried | identical |
| `retry_5xx` | 500 with two retries (1 s, 2 s), 502 with one, 499 with none: attempt count, timing, retry counts | identical |
| `retry_503` | 503 recovering after two attempts, `Retry-After: 2` honoured, `Retry-After` ignored when asked, a custom 200/500 ms backoff | settled: the C++ never recovered when `Retry-After` was unused (above) |
| `timeout` | connect/transfer timeout of 2 s with one retry, transfer timeout alone | settled: the C++ retry delay was uninitialised (above) |
| `partial_body` | the body closed at half its `Content-Length` (`CURLE_PARTIAL_FILE`, retried once), the connection reset at half (`CURLE_RECV_ERROR`) | identical |
| `redirects` | two 302 hops followed (with `Referer`), a 302 delivered when not followed, a POST redirected | identical |
| `concurrency` | three classes with connection limits 2 and 4 and a throttle of 5/s, eight 300 ms requests each: overlap 2, 4 and 5, the sixth throttled request a second later | identical |
| `cancel` | cancel of a queued request (never sent), of an active one (sent, discarded), of an unknown handle | identical |
| `headers_cookies` | caller headers reflected, `Connection`/`User-Agent` overridden by the caller, a cookie set then sent on the recycled handle, `If-Modified-Since` | identical |
| `bodies` | chunked, gzip (decoded), no `Content-Length`, an inconsistent `Content-Range` (`HE_INV_CONTENT_RANGE_HDR`, retried once) | identical |
| `policy_options` | static sets before start with clamping and every refusal code, the refusals while running, dynamic sets through the service thread, an unknown class | identical |
| `pipelining` | a class with pipelining depth 5 on one host connection, six requests | identical |

### Tests

- vkstorm-core-rust `crates/vkstorm-http`: 15 unit tests (status codes and
  strings, header normalisation, `Content-Range` and `Retry-After`
  parsing, option clamping and validation, the retry backoff, the retry
  queue's order, the class limit) and `tests/scenarios.rs`, 16 scenarios
  of the viewer's parity set against the crate's own loopback mock server
  (`tests/mock`, the same paths as the Python one, less gzip and the
  reset), run on Windows and Linux against the viewer's prebuilt libcurl
  7.54.1 with OpenSSL (`VKS_HTTP_CURL_LIB_DIR` at the build's
  `packages/lib/release`). `cargo bench --bench loopback` is the
  crate-side timing harness.
- `INTEGRATION_TEST_llcorehttp_parity` (viewer): the 16 scenarios above
  through the real C++ classes into Rust and back, against the record;
  passes on Windows and on Linux. Run by CI with the other focused tests.
- `INTEGRATION_TEST_llcorehttp`, the original suite with its own Python
  peer (`test_llcorehttp_peer.py`), disabled upstream in 2024 "until it
  can be debugged again", builds and runs again: `HttpStatus`,
  `RefCounted`, `BufferArray`, `BufferStream`, `HttpHeaders` and the
  `HttpRequest` group (23 tests: construction, no-op, stop, two
  instances, spin, dead port, GET, ranged GET, PUT, POST, tracing,
  returned headers, timeout, content type, header generation for GET,
  POST and PUT) pass: 52 of 57 on Windows with its 3 known skips, 50 of
  57 on Linux with its 5 (the two spin tests are skipped there as well).
  Two tests fail, and the unchanged suite run on the pre-port C++ on
  Linux (63 tests, 56 passed, the same 5 skips) fails the same two with
  the same values: `BUG-2295`
  expects a 206 for a 25-byte range answered with a 76-byte
  `Content-Range`, which the viewer's pipelining sanity check has turned
  into a retryable 503 since Firestorm added it, and `GET 503s with
  Retry-After` runs on the service `BUG-2295`'s failure leaves stopped
  (the test's own cleanup throws again from the pending replies, so its
  `destroyService` never runs). The two groups that tested the removed
  `HttpOperation` and `HttpRequestQueue` classes are gone; the
  `HttpRequest` group lost one assertion on the request queue's reference
  count. It is not in CI's focused list, since its post-build run fails
  on those two tests by the suite's own construction; the parity test is.
- `lltest`: 295 tests on Windows, 302 passed and 1 known skip on Linux,
  unchanged.
- The viewer builds, stages and reaches the login screen on Windows with
  empty settings and user directories (`APPDATA` and `LOCALAPPDATA`
  redirected to scratch directories, no remembered credentials: the log
  shows the login fields set to empty, `STATE_LOGIN_SHOW` then
  `STATE_LOGIN_WAIT`, three launches). The login panel's grid-list fetch
  (`GridListRequest: got new list`), the Firestorm data files
  (`data.xml`, `client_list_v2.xml`, saved) and the local welcome page
  all went through the port; the one HTTP failure in the log is the
  server's own 403 for `defaults.1.0.0.xml`, delivered as `Http_403` with
  no retry, as a 4xx should be. No `Core_`, `Easy_` or `Multi_` error and
  no HTTP-layer warning appear. The window (the viewer's title, full
  screen) was captured from the launching session; its Zink-rendered
  client area is not reachable by a GDI screen copy or `PrintWindow`
  from there, so the capture shows the frame, not the rendered login
  form.

### Measurements

`INTEGRATION_TEST_llcorehttp_parity` with `VKS_HTTP_BENCH=2000`: the C++ at
the pre-port commit and the port, each built in the same tree and run
three times back to back on an idle machine (AMD Ryzen 9 9950X3D; Windows
11, MSVC 19.44, the RelWithDebInfo configuration; Ubuntu 26.04 under WSL,
GCC 15, the CI Release configuration), libcurl 7.54.1 with OpenSSL
1.1.1w on both; the crate at `opt-level 3`, fat LTO, `x86-64-v3`. The
mock server is the Python one on loopback; one client, one request in
flight at a time, `update()` polled continuously; the handler latency is
submit to `onCompleted`. On Windows the harness sets the 1 ms scheduler
period the viewer runs with (`timeBeginPeriod`), without which the
service loop's 2 ms sleeps round to 15.6 ms on both sides. The Rust
column is the viewer-side path: the C++ `HttpRequest`, the C ABI, the
crate and libcurl. Memory per in-flight request is the RSS growth with
20000 POSTs of a 1 KiB body queued behind the default class's eight
connections, divided by 20000. Median of the three runs.

| operation | Windows C++ | Windows Rust | Linux C++ | Linux Rust |
|---|---:|---:|---:|---:|
| GET small LLSD (125 B): requests/s | 199 | 199 | 236 | 237 |
| GET small LLSD: mean / median / p95 ms | 5.04 / 5.00 / 5.51 | 5.03 / 5.01 / 5.07 | 4.24 / 4.23 / 4.32 | 4.22 / 4.22 / 4.29 |
| POST small LLSD (191 B, echoed): requests/s | 198 | 199 | 235 | 236 |
| POST small LLSD: mean / median / p95 ms | 5.05 / 5.00 / 5.51 | 5.03 / 5.01 / 5.07 | 4.26 / 4.24 / 4.36 | 4.23 / 4.22 / 4.30 |
| GET 1 MiB range: requests/s | 4.41 | 4.36 | 5.59 | 5.63 |
| GET 1 MiB range: mean / median / p95 ms | 226.5 / 226.3 / 229.4 | 229.5 / 225.8 / 248.8 | 178.6 / 178.2 / 180.3 | 177.6 / 177.5 / 177.9 |
| memory per in-flight request (1 KiB POST) | 1039 B | 1999 B | 669 B | 1707 B |

The request rate is the service loop's: a request costs two passes with
the 2 ms sleep between them (submit, send, then receive on the next
pass), and a 1 MiB body is read 16 KiB per pass by this libcurl, which
puts both implementations at the same figures; the port is within the
run-to-run noise of the C++ on every row on both platforms (the Windows
1 MiB p95 of one run, 263 ms, is the noise). The crate alone
(`cargo bench --bench loopback`, Rust mock server) gives the same rates
on Linux (234 and 235 requests/s, 139 ms for 1 MiB) and on Windows (198
and 199 requests/s, 172 ms for 1 MiB): the C ABI and the `BufferArray`
copies are below what the harness resolves. The extra kilobyte per
in-flight request is the body copy: the C++ shared the caller's
`BufferArray` by reference count, the crate holds its own copy of the
bytes (the harness queues one shared 1 KiB body 20000 times, which no
caller does); the record itself is smaller than the C++ operation.

### Platform edges

- Windows and Linux agree on everything the record compares; the
  Linux C++ before the port differed from the Windows record only on the
  two settled scenarios and on the platform's own spacing between
  requests, which the comparison does not measure.
- On Windows loopback a server closing a connection without a
  `Content-Length` reaches libcurl as a reset in roughly one run in five
  (`curl.exe` 8.21 against the Python server reproduces it), which this
  libcurl retries; the length-less body is therefore covered by the
  crate's own test, where the Rust server's close is clean on both
  platforms, and a body cut short is accepted as either
  `CURLE_PARTIAL_FILE` or `CURLE_RECV_ERROR` in the record.
- Linux links the Rust static library before `libcurl.a`: `vkstorm_ffi`
  carries `ll::libcurl` as an interface dependency so the order holds for
  every consumer.
- A service stopped with `requestStopThread` can be started again with
  `startThread` (the original suite does it); its request queue stays
  closed, as the C++ queue did, so the service must be destroyed and
  created again to accept requests, as `LLAppCoreHttp` does.

## Rung five: the asset pipeline

The data side of the asset pipeline is Rust: `vkstorm-assets` in
vkstorm-core-rust (`crates/vkstorm-assets`, no `unsafe`), reached through
the `vks_texcache_*`, `vks_file_*`, `vks_asset_file_*`, `vks_lfs_*`,
`vks_tfetch_*`, `vks_j2c_calc_*`, `vks_retry_*`, `vks_diskcache_*`,
`vks_mesh_*`, `vks_asset_*` and `vks_toxic_*` functions of
`indra/rust/vkstorm-ffi/src/assets.rs` (header `indra/rust/include/vkstorm_ffi.h`;
`llfilesystem`, `llmath`, `llprimitive`, `llimage`, `llmessage` and the
viewer link `vkstorm_ffi`). No runtime choice and no fallback. The C++
classes keep their names and public interfaces (`LLTextureCache`,
`LLTextureFetch`, `LLAdaptiveRetryPolicy`, `LLImageJ2C`, `LLDiskCache`,
`LLFileSystem`, `LLLFSThread`, `LLVolume`, `LLMeshSkinInfo`,
`LLModel::Decomposition`, `LLMeshRepoThread`, `LLMeshHeader`,
`LLAssetStorage`, `LLViewerAssetStorage`), so no caller changed.

### Keep-or-port judgement

| Part | Decision | Why |
|---|---|---|
| `LLTextureCache`: the entries file, the header-data file, the body files, the fast cache, the LRU and free list, the updated-entry map, the lazy and full purges, corruption handling, the version and engine-string check | Rust (`texcache`) | The index and file logic. The on-disk format is unchanged and byte-compatible (below), so the port reads and writes the cache the C++ left behind. |
| `LLTextureCacheWorker`, `LLTextureCacheRemoteWorker`, `LLTextureCacheLocalFileWorker`, the responders, `LLTextureCache::update` | C++ | The `LLWorkerThread` plumbing that serialises reads and writes on the cache thread and delivers completions on the main thread. A worker's read fills one buffer the responder takes over (`vks_texcache_read`), a write hands the bytes and the raw image scaled to the fast cache level (`vks_texcache_write`, `vks_texcache_fast_level`): one call per operation, the C++ states (header, body, fast cache) folded inside. |
| `LLTextureFetchWorker::doWork` and its states, `onCompleted`/`callbackHttpGet`, `callbackCacheRead`, `callbackCacheWrite`, `callbackDecoded`, `deleteOK`'s state part, `setDesiredDiscard`, the HTTP semaphore and water marks, the resource waiters and their release order, the cumulative counts, the state timers | Rust (`fetch`) | The state machine. `Worker::do_work` is the C++ `doWork` with its `return doWork(param)` recursions as iterations; the C++ worker keeps the objects the machine does not own and drives it through a table of host callbacks (`VksTexFetchHost`), each one line of the old `doWork` (a cache read, an HTTP GET, a decode post, a cache write, the assembly of the formatted image from a reply, the statistics). `LLTextureFetch` holds the crate's fetcher under its own lock (deleteOK, the worker destructors and the statistics run on other threads). |
| `LLTextureFetch`'s request map, the HTTP queue accounting, the command queue (region and metrics), `createRequest`'s plumbing, `getRequestFinished` and the state accessors, `releaseHttpWaiters`' walk over the workers | C++ | Thread, map and lock plumbing over the C++ worker objects; the decisions in it (`createRequest`'s desired size and discard, the "already done" check, the release order of waiters by priority, the data progress and requested priority an accessor reports) come from the crate. |
| The UDP texture path (`LOAD_FROM_SIMULATOR`, `sendRequestListToSimulators`, `receiveImageHeader`/`receiveImagePacket`, the packet assembly) | Rust behind the `opensim` feature; the C++ callers compiled with `OPENSIM` | Legacy transport Second Life no longer uses; CMake forwards `OPENSIM` to the crate feature, so a Second Life build carries neither side. Without it the `ImageData`/`ImagePacket` handlers count the packet as bad and no request list is sent. |
| `LLAdaptiveRetryPolicy` | Rust (`fetch::retry`); the C++ class is a thin wrapper that still reads and parses the `Retry-After` header (from `LLSD` or `HttpHeaders`; seconds or an RFC 1123 date through `curl_getdate`) | The backoff, the retry counting, the 4xx/5xx decision. The header parsing needs libcurl's date parser, which the C++ already links. |
| `LLImageJ2C::calcDataSizeJ2C` | Rust (`fetch::budget`) | The byte budget per discard level the fetcher asks the server for (the FIRE-35987 layer walk). `calcDiscardLevelBytes` stays the C++ loop over the per-instance cached budgets (it reads the image's own rate). |
| `LLWorkerThread`, `LLQueuedThread`, `LLImageDecodeThread` (the thread pools) | C++, driving the Rust machine | Measured and judged: the texture cache thread, the fetch thread and the decode pool are the viewer's `LLQueuedThread`/`LL::ThreadPool` instances whose pause, wake, priority and shutdown semantics the main loop depends on (`LLAppViewer::idle`, `getPending`, `runCondition`); the work they run is where the time goes and that is Rust. Rust threads would only add a second scheduler to coordinate with the first for no measured gain: the hot paths are at the C++ on the same threads. |
| The GL upload side (`LLViewerTexture`, `LLViewerFetchedTexture`, `LLImageGL`) | C++ | Rendering. |
| `LLImageRaw`, `LLImageFormatted`, `LLImageJ2C` as containers and codecs | C++ (the J2C decode is rung one) | The buffers the decode thread, the GL upload and the cache share by `LLPointer`; the fetcher's machine sees their sizes, discard levels and codec through the environment snapshot and the host copies bytes into them. |
| `LLDiskCache`: naming, the purge with the high and low water marks and the static-asset skip list, the directory scans, clear, the old VFS file removal | Rust (`diskcache`) | File logic. The singleton, the API and the static-asset prepopulation (it walks the viewer's `fs_static_assets` directory) stay C++; the purge thread runs on a copy of the configuration and hands the scan result back, so the main thread never waits for the file system. |
| `LLFileSystem` (asset files: exists, size, read, write in its modes, seek, rename, remove, access-time touch), `LLLFSThread`'s reads and writes | Rust (`diskcache::file`) | The file operations; the C++ keeps the position bookkeeping and the request thread. |
| Mesh LOD unpacking (`LLVolume::unpackVolumeFaces` from bytes), skin info and convex decomposition parsing (`LLMeshSkinInfo::fromZippedData`, `LLModel::Decomposition::fromZippedData`) | Rust (`mesh::inflate`, `mesh::llsd`, `mesh::unpack`, `mesh::skin`) | Inflate, binary LLSD, dequantisation lane for lane as the SSE code did (the fourth lane of positions 0, of normals -1, the pairwise texture coordinates and their padding entry, the `_mm_min_ps` operand order of the extents), the joint weights, the sculpt flags. `cacheOptimize` (meshoptimizer) and the tangents stay C++; the importer's `fromLLSD` paths stay C++. |
| `sculptGenerateMapVertices` | C++ (left by the mesher rung) | Sculpt maps are textures, not mesh assets; the judgement of the mesher rung stands. |
| `LLMeshRepoThread`: `headerReceived`'s parsing and cache bookkeeping, `constructUrl`, the policy class of a byte range, the parts' byte ranges and cache state, the cache preamble, the all-zeros check of cached blocks, the response range check of `LLMeshHandlerBase::onCompleted`, `getActualMeshLOD`, the reserved cache file size | Rust (`mesh::header`, `mesh::plan`) | The data decisions of the repository; `LLMeshHeader` stays the C++ record the repository keeps per mesh (the cost and streaming code reads it), converted at the boundary. |
| `LLMeshRepoThread`'s queues, threads, HTTP handlers, `LLFileSystem` reads and writes, the physics shape's triangle soup, the upload thread, `LLPhysicsDecomp` | C++ | Thread and queue plumbing, the uploader (an importer concern) and the convex decomposition library. The physics shape still goes through `LLVolume::unpackVolumeFaces` (Rust) and `cacheOptimize` (C++) as before. |
| `LLAssetStorage`: the error strings, the toxic asset map, `getAssetData`'s duplicate verdict, the completion and time-out orders of the pending lists; `LLViewerAssetStorage::getAssetURL` | Rust (`asset`) | The bookkeeping policies. The request lists, the coroutine HTTP path (`assetRequestCoro` on `LLCoros` and `HttpCoroutineAdapter`, out of scope by rung four's judgement) and the legacy UDP transfers stay C++. |

### The boundary

- The texture cache is one opaque object (`VksTexCache`) created with the
  codec's engine string and the address size, stamped by a clock the host
  supplies (`LLTextureCache::sClock`, `time(NULL)` in the viewer, fixed in
  the tests). `initCache`, `purgeCache`, `setReadOnly`, `isInCache`,
  `removeFromCache`, the usage figures and the periodic
  `writeUpdatedEntries` map one to one; the cache validation counter the
  C++ kept in `CacheValidateCounter` is read and advanced by the C++
  around `vks_texcache_init` as before (when a purge ran). Panics are
  caught at every entry and reported as the operation's failure.
- The fetch worker's machine (`VksTexFetchWorker`) runs under the C++
  worker's work mutex, as `doWork` did; each `do_work` call takes a
  snapshot of the environment (`VksTexFetchEnv`: quitting, pause, the
  formatted image's size, discard level and codec, the raw image's
  dimensions, the settings `HttpRangeRequestsDisable` and
  `TextureDecodeDisabled`, `gDisconnected`, the bandwidth figures) and
  acts through the callback table. Events arrive as calls:
  `cache_read_done`, `cache_write_done`, `decode_done`, `http_completed`
  (the response status, its type for the policy, the reason, the 206
  flag, the body size, the `Content-Range` triple and a parsed
  `Retry-After`). The machine says whether it kept the reply body
  (`http_buffer_present`) and the host holds the `BufferArray` for it;
  the assembly of the formatted image from the reply (the buffer, the
  copy of the bytes held, the reply bytes from the overlap offset) is one
  callback, so the bytes never cross the boundary. The fetcher
  (`VksTexFetcher`) holds the HTTP semaphore, the water marks, the
  waiters and the counts under a mutex in the crate.
- The accessors the viewer reads (`getFetchState`, `getLastFetchState`,
  `getRequestFinished`, the texture console) take a snapshot of the
  machine's fields (`VksTexFetchWorkerInfo`) under the work mutex.
- The disk cache (`VksDiskCache`) and the toxic map (`VksToxicMap`) are
  opaque objects; the file operations are plain functions over paths.
- Mesh parsing hands back opaque results (`VksMeshFaces`, `VksMeshSkin`,
  `VksMeshDecomp`) whose buffers the C++ copies into its own
  `LLVolumeFace`, `LLMeshSkinInfo` and `LLModel::Decomposition` and
  frees; `headerReceived` returns a receipt (`VksMeshHeaderReceipt`): the
  header, the combined header size, and which skin and LOD blocks the
  received bytes already hold.
- C++ exceptions never cross: every callback body is exception-free by
  construction (allocation through `ll_aligned_malloc_16` returns null,
  the cache and HTTP calls are `noexcept` in effect); a Rust panic is
  caught at the boundary (`catch_unwind`) and reported as the call's
  failure value.

### Cache format decision

The texture cache's on-disk format is kept: `texture.entries` (a 44-byte
`EntriesInfo` of version, address size, a 32-byte engine string and the
entry count, then 28-byte entries of id, image size, body size and time),
`texture.cache` (600-byte header records), `texturecache/<0-f>/<uuid>.texture`
bodies and `FastCache.cache` (1040-byte records: width, height,
components, discard level and up to 1024 pixel bytes). No migration: the
port opens the cache the C++ left and the C++ could open the port's. The
engine string is still `LLImageJ2C::getEngineInfo()`, so the one-time
purge the codec's `2.5.4` string triggers happens as before. The one
difference is documented below (the unused tail of a fast cache record).
The asset disk cache (`sl_cache_<uuid>_0.asset` under `<0-f>/`) and the
mesh cache's 12-byte preamble are likewise unchanged.

### Behaviour kept, settled, and documented as different

Kept, and covered by the recorded corpora: the cache's index (LRU of the
10 % oldest, the free list, the 20 % purge above the limit by age, the
one-in-256 validation of bodies per start, the version and engine
mismatch purge, the entry time stamps once 75 % of the entries are in
use, the corrupted-cache reset), the fast cache level search, the
texture fetch decisions (the discard-level byte budget, the cache read
before the network, the `file://` local reads, the HTTP range with the
one-byte overlap of a resumed fetch and the open-ended range past
20 MB, the 404/503/416 handling with the region check, the capability
retry limit of 720 s, the `Content-Range` alignment of partial replies,
the decode and cache write states, the return to `INIT` when more data
is wanted, the resource semaphore with its pipelined and non-pipelined
water marks, the waiters released by priority), the server-bake retry
policy (10 s doubling to an hour, ten retries, `Retry-After` honoured),
the mesh header's cache flags, the part ranges, the all-zeros check, the
URL preference (ViewerAsset, GetMesh2, GetMesh), the policy class by
size, the mesh LOD, skin and decomposition unpacking bit for bit, the
disk cache's purge order and skip list, the asset storage's request
policies and strings.

Documented as different:

- A fast cache record's unused tail (the bytes after `16 + w*h*c`) is
  written as zeros; the C++ wrote whatever its pad buffer held from the
  previous record (uninitialised for the first), which is why the parity
  test masks those bytes. No reader looks at them.
- The inflate error codes of `LLUZipHelper` (`ZR_MEM_ERROR`,
  `ZR_DATA_ERROR`, `ZR_SIZE_ERROR`, `ZR_PARSE_ERROR`) map to the crate's
  `ZipResult`; the mesh repository only ever tested for `ZR_OK`, and the
  logged code is the crate's.
- `LLAssetStorage::getAssetData`'s duplicate check compared two
  `LLGetAssetCallback` `std::function`s through
  `target<LLGetAssetCallback>()`, which never matches a stored function
  pointer or lambda, so two callbacks always compared equal and the
  verdict was decided on the asset and the user data alone. The port
  passes the same identity (zero) for every callback and keeps that
  decision rather than change what the viewer discards.
- The texture fetcher's debugging aids (`mDebugID`, `mDebugPause`) are
  copied into the crate's fetcher once per main-loop update; the
  breakpoint counter the console increments stays C++.
- `LLTextureFetch::getStateString` is the crate's. The `Retry-After`
  header stays C++, as before the port: a number of seconds through
  `strtod`, an RFC 1123 date through libcurl's `curl_getdate`; the
  crate's policy takes the parsed seconds. `INTEGRATION_TEST_llhttpretrypolicy`,
  disabled upstream since the policy moved to the viewer, builds and
  runs again and checks both forms.

### Parity corpora

All four were recorded from the C++ before the port, on Windows, and are
stored under `indra/newview/tests/fixtures` and
`indra/llprimitive/tests/fixtures` in the viewer and
`crates/vkstorm-assets/tests/fixtures` in vkstorm-core-rust (git
attributes mark them binary).

| corpus | what it holds | test | result |
|---|---|---|---|
| `lltexturecache_parity.bin` (723 KB) and `lltexturecache_parity_events.xml` (16 KB) | three snapshots of every cache file after a scripted sequence on a scratch directory with a fixed clock: writes, reads, fast-cache reads, re-writes, removals, an over-limit purge, a reopen with body validation, a version-mismatch purge; and the script's outcomes: every write's and removal's result, every read's success, size and data checksum, every fast-cache read's dimensions, level and pixel checksum, the usage, entry counts and validation counter after each phase (the outcomes recorded from the pre-port C++ on Windows after the files, when review found them only logged) | `INTEGRATION_TEST_lltexturecache_parity` (viewer, the port's `LLTextureCache` through the real worker thread); `tests/texcache_replay.rs` (the crate alone, the files) | files byte-identical and outcomes identical on Windows and Linux, the engine string checked against the running codec, the fast cache tails masked (above) |
| `lltexturefetch_parity.xml` + `textures/*.j2c` (36 KB + 969 KB) | fifteen scripted textures (plain, lossless, tiny, RGBA, 404, 503, one-off 503, truncated and overlapping ranges, a full 200 to a range request, 416 on ranges) fetched over three passes: per pass and texture the outcome (state, discard, raw dimensions, HTTP status, from cache) and the server's request log (ranges and statuses), then the cache's contents | `INTEGRATION_TEST_lltexturefetch_parity` (the real cache, decode thread and HTTP layer against `tests/vksassets_mock_server.py`) | 15 textures over 3 passes, 0 differences, on Windows and Linux |
| `llmeshunpack_corpus.vksa` (6.3 MB) | 73 assets built with the viewer's own packer (`LLModel::writeModel`): twelve prim shapes at every LOD, with and without skin weights, physics shapes, convex hulls and base hulls, plus a 9216-triangle torus; for each the faces as `unpackVolumeFaces` left them and after `cacheOptimize`, the skin info with its bind pose, the decomposition | `INTEGRATION_TEST_llmeshunpack_parity` (viewer: the raw faces, the skin and the decomposition against the corpus bit for bit; the optimised faces against an oracle built in the same binary, the same C++ `cacheOptimize` and tangent generation run on the raw faces; the corpus' optimised faces are kept in the file but not compared, see Platform edges); `tests/mesh_corpus.rs` (the crate, the raw faces) | 73 cases: 1361 raw faces against the corpus and 1361 optimised faces against the oracle, 0 differences (viewer); 73 cases, 1361 raw faces, 0 differences (crate); on Windows (MSVC 19.44) and Linux (Ubuntu 26.04 with GCC 15 and with GCC 14.3) |
| asset fetch request/response sequences | not recorded: the HTTP asset fetch is `assetRequestCoro`, a coroutine on `LLCoros` and `HttpCoroutineAdapter` that stays C++ (rung four's judgement); what the rung ports of `LLAssetStorage` is the request bookkeeping, covered by the crate's unit tests of the policies and strings, and `getAssetURL`, whose output the mock-server fetch test would only echo | crate unit tests | the policies agree with the C++ by construction (one implementation of each, tested) |

### Tests

- vkstorm-core-rust `crates/vkstorm-assets`: 44 unit tests (the cache
  record encodings and purge rules, the fetcher's budget, retry policy,
  waiters and state transitions, the disk cache purge and naming, the
  mesh header, URL and plan rules, the LLSD accessors, the inflate
  round trip, the asset policies), `tests/texcache_replay.rs` and
  `tests/mesh_corpus.rs` against the corpora; the same with
  `--features opensim`. `cargo bench --bench hot_paths` is the
  crate-side timing harness.
- Viewer: `INTEGRATION_TEST_lltexturecache_parity`,
  `INTEGRATION_TEST_lltexturefetch_parity`,
  `INTEGRATION_TEST_llmeshunpack_parity` (above) and
  `INTEGRATION_TEST_llhttpretrypolicy` (the retry policy's original
  suite, seconds and RFC 1123 dates), in CI's focused list;
  `INTEGRATION_TEST_llimagej2coj`, `INTEGRATION_TEST_llimagecodecs`,
  `INTEGRATION_TEST_llvolume`, `INTEGRATION_TEST_llmessagetemplate` and
  `INTEGRATION_TEST_llcorehttp_parity` still pass. The llprimitive unit
  tests (`llgltfmaterial`, `llprimitive`), which every test linking
  `llprimitive` builds, were fixed to build again (two float constants,
  a stub).
- The viewer builds, stages and reaches the login screen on Windows with
  empty settings and user directories (`APPDATA` and `LOCALAPPDATA`
  redirected to scratch directories, no remembered credentials): the log
  shows the login fields set to empty, `STATE_LOGIN_SHOW` then
  `STATE_LOGIN_WAIT`, the J2C engine string `vkstorm-openjp2 2.5.4
  (Rust)`, the texture cache initialised through the crate (`Headers:
  942797 Textures size: 2621 MB`, the fresh cache cleared and purged)
  and the asset disk cache created and prepopulated; the login panel's
  textures and the Firestorm data fetches went through the port with no
  error from it. The window was captured at the login screen with the
  fields empty. Two of the three launches on the development desktop
  were taken over by the person at the machine, who logged in on them
  (the second with a different account) to look around; no credentials
  were entered by the automation, and one of those sessions, 60 seconds
  in-world, loaded 17,214 mesh fetches to quiescence and its textures
  through the ported cache, fetcher and repository without a warning
  from them beyond the CDN's own 403 replies, handled as before.

### Measurements

The viewer's three bench modes, the C++ at the recording commits and the
port, each built in the same tree and run three times back to back on an
otherwise idle machine (AMD Ryzen 9 9950X3D; Windows 11, MSVC 19.44, the
RelWithDebInfo configuration; Ubuntu 26.04 under WSL, GCC 15, the CI
configuration at RelWithDebInfo); the crate at `opt-level 3`, fat LTO,
`x86-64-v3`. Minimum over the runs. The pre-port figures come from the
same test sources with the pre-port C++ of the cache, the fetcher, the
file layer and the mesh unpackers swapped in from the recording commits.

| hot path | what is timed | Windows C++ | Windows Rust | Linux C++ | Linux Rust |
|---|---|---:|---:|---:|---:|
| mesh LOD unpack (`VKS_MESH_BENCH=5`) | the raw `unpackVolumeFaces` of all five parts of `torus_big/skin_hulls` (9216 triangles in the high LOD), before `cacheOptimize` | 0.2525 ms | 0.2372 ms | 0.1868 ms | 0.1831 ms |
| texture cache index lookup (`VKS_TEXCACHE_BENCH=200`) | `isInCache` over 200 cached textures, 200 passes | 0.0415 µs | 0.0307 µs | 0.0133 µs | 0.0196 µs |
| texture cache write / read (same harness) | a new 4000-byte texture through the worker thread, and a cached one read back, completion polled | 46.9 / 46.1 ms | 46.3 / 46.3 ms | 6.33 / 6.30 ms | 6.31 / 6.30 ms |
| texture assembly (`VKS_TEXFETCH_BENCH=3`) | one 1,195,278-byte lossless 1024x1024 JPEG 2000 fetched at discard 5 down to 0 through the real cache, decode thread and HTTP layer against the mock server, per full sequence | 723.7 ms | 726.5 ms | 687.1 ms | 686.7 ms |

The port is at the C++ on every row on both platforms: the mesh unpack
within run-to-run noise (the Windows unpack 0.2525 / 0.2760 / 0.2943 ms
before, 0.2372 / 0.2396 / 0.2511 ms after; Linux 0.1868 / 0.1875 /
0.1887 before, 0.1831 / 0.1838 / 0.1847 after), the texture assembly
dominated by the six decodes of a lossless megabyte and the mock
server's replies (Windows before 723.7 / 1068.2 / 1146.8 ms, after
726.5 / 729.4 / 764.0 ms; Linux 687.1 / 687.3 / 687.6 before, 686.7 /
687.2 / 687.2 after). The index lookup is a hash lookup under a lock
either way; on Linux the port's crossing of the C ABI and its mutex add
6 ns to a 13 ns lookup, on Windows it is 11 ns faster than the C++
`std::map` under `LLMutex`. The cache write and read rows are
bound by the harness, which drives the real worker thread and polls its
completion through the main-loop `update` in 2 ms steps; the figure is
the same with either implementation and does not resolve the cache's own
cost.

The crate alone (`cargo bench --bench hot_paths -- 20` in
vkstorm-core-rust, minimum of three runs; the default target CPU, no
LTO) gives the cache's own cost and the fetch machine's: on Windows the
index lookup 0.013 µs, a write 0.58 ms and a read 0.10 ms (NTFS file
creation and reads), unpacking all parts of the same mesh including the
skin and the decomposition 0.56 ms, and the state machine's assembly of
1 MiB over six discard levels with the cache, decode and HTTP mocked
0.29 ms (30 machine steps); on Linux the lookup 0.013 µs, a write
27 µs, a read 9.7 µs, the mesh 0.17 ms and the assembly 0.04 ms. The
difference between the crate's mesh figure and the viewer harness's is
the viewer build's `x86-64-v3` code generation and LTO, which the crate
bench does not use.

### Platform edges

- Windows and Linux agree on every corpus: the cache files byte for
  byte, the fetch decisions and request sequences, the 73 mesh cases.
- The cache's directory names are built as the C++ built them; the C++
  joined `getExpandedFilename(location, "")`, which ends in a delimiter,
  with another delimiter (`cache\\texturecache`), which both platforms'
  file systems collapse. The port strips the trailing delimiter before
  joining, so the paths it logs are the plain ones.
- On Windows the texture cache's full purge renames the old directory
  aside and warns when the directory is not there yet (a fresh profile),
  as the C++ did.
- The faces after `cacheOptimize` depend on the build and run
  environment, the raw faces do not. CI's Linux job (Ubuntu 24.04, GCC
  14.2, glibc 2.39) unpacked every raw face of the mesh corpus bit for
  bit but differed from the MSVC record in all 1361 optimised faces
  (1285 in the normals and tangents, 76 in the tangents alone). MSVC
  19.44 and, on Ubuntu 26.04 (glibc 2.43), GCC 15 and GCC 14.3 reproduce
  the record, so the difference did not reproduce locally; the tangents
  come from the prebuilt MikkTSpace library, which calls the C library's
  `acosf`, whose implementation changed between those glibc releases, the
  likely but unproven cause. Either way it is the C++ vertex cache
  optimisation and tangent generation the port hands its faces to, not
  the port: the viewer test compares the optimised faces with an oracle
  run in the same process (the same C++ applied to the raw faces, which
  match the corpus) instead of with the record, which checks that the
  port hands the optimisation exactly the faces the C++ unpacker did in
  any environment. The corpus file still carries the recorded optimised
  faces; they are not compared. A port that skipped the optimisation
  fails all 1361 comparisons.
- GCC's `-Werror=class-memaccess` refuses `memcpy` into `LLVector4a`
  and `LLVector2` arrays; the face copies go through the element float
  pointers.
- The Linux C++ before the port agrees with the Windows records on
  everything the three corpora compare (the three parity tests pass with
  the pre-port sources swapped in on Linux); the recorded fixtures were
  made on Windows.
- The fetch parity test's executable needs the SDL the window layer
  links on Linux, which is not among the staged shared libraries; the
  prebuilt library directory is on its library path. The test's raw
  `gAgent` storage takes the object's symbol through an asm label there
  (the Windows build uses the linker's alternate name), which LTO
  reports as a type mismatch warning.

## Rung six: the audio engine

The viewer's default audio backend is Rust: `vkstorm-audio` in
vkstorm-core-rust (`crates/vkstorm-audio`; Ogg Vorbis in its own crate,
`crates/vkstorm-vorbis`), reached through the `vks_audio_*` and
`vks_music_*` functions of `indra/rust/vkstorm-ffi/src/audio.rs` (header
`indra/rust/include/vkstorm_ffi.h`; `llaudio` links `vkstorm_ffi`). It owns
the output device and mixes sound effects, UI sounds, wind, attached and
object sounds and parcel music. SoLoud is no longer part of the build and
there is no runtime switch and no fallback engine. OpenAL (`--openal`) and
FMOD Studio (`--fmodstudio`) remain compile-time options, untouched.

The C++ classes keep their names and interfaces: `LLAudioEngine`,
`LLAudioSource`, `LLAudioChannel`, `LLAudioBuffer`, `LLAudioData`,
`LLListener`, `LLAudioDecodeMgr` and `LLStreamingAudioInterface`. The
backend subclasses that replaced the SoLoud ones are `LLAudioEngine_VKS`,
`LLAudioChannelVKS`, `LLAudioBufferVKS`, `LLListener_VKS` and the music
slot `LLStreamingAudio_VKS` (`indra/llaudio/*_vks.*`). They keep the call
sequences of the SoLoud adapter they replace, one mixer call per SoLoud call.

Build selection: `USE_VKS_AUDIO` (`indra/cmake/VKSAudio.cmake`; default
unless OpenAL or FMOD Studio is selected; `--vksaudio` in
`scripts/configure_firestorm.sh`) defines `LL_VKS_AUDIO` and pulls the
`ffmpeg` package. `indra/llaudio/tests/audio_backend_selection.cmake`
checks the selection rules.

### Scope

1. Effects: the mixer is a port of the parts of SoLoud e82fd32c1f (with the
   3p-soloud patches) the viewer used, operation for operation: voices that
   start paused and fully configured, faders and scheduled stops (the 20 ms
   declick), looping and loop counts, synced starts (seek into the master's
   loop, 20 ms fade in), 64 active voices with SoLoud's active-voice
   selection (protected wind, inaudible voices that keep ticking), the
   linear resampler, the listener and source parameters with
   inverse-distance attenuation (1 m to 512 m), rolloff and Doppler, the
   panning for 1, 2, 4, 6 and 8 output channels with the viewer's 7.1
   back/side order, the round-off clipper, the 1024-frame bounded mix, the
   baked 64-sample head and tail ramp of every sound, and the wind
   (`LLWindGen` ported, its targets lock-free). Output devices are chosen by
   endpoint identity with the same UUIDs as before (MD5 of "WASAPI" and the
   endpoint id), so saved selections stay valid; the 1 s device mutex shared
   with voice and its retry, the 5 s device list refresh, restore-previous on
   a failed switch and "no other engine" are kept.
2. Sound assets: `LLVorbisDecodeState`'s decode and the decoded file it
   writes (the 44-byte header that always says mono 44.1 kHz, the `ov_read`
   bytes, the loop-point fade) are Rust, bit for bit.
3. Parcel music fills the engine's own streaming slot
   (`createDefaultStreamingAudioImpl`), as FMOD's streamer does upstream:
   decoded in-process and mixed on the device clock. MP3, MP2, Ogg Vorbis,
   AAC-LC and FLAC are pure Rust (symphonia 0.6.1 for MPEG audio, AAC-LC and
   FLAC; vkstorm-vorbis for Vorbis), HE-AAC v1/v2 and Opus go through the
   decoder-only LGPL FFmpeg 9.0.2 (below). Kept: the fade policy of
   `llvieweraudio.cpp` (FSFadeAudioStream, 3.0 s in, 2.0 s out, the login
   hold at zero until STATE_STARTED, completion when the final faded sample
   has passed the device clock, failure or cancel is a hard stop), the ICY
   handshake including the non-HTTP `ICY 200 OK` status line and
   interleaved titles split into artist and title, Ogg comments on every
   chain link, the Icecast status-json poller (5 s, then every 20 s, three
   strikes, the mount path walk, the side channel wins), M3U and PLS
   playlists, reconnects, the refusal of video and web pages, the metadata
   reset on start and stop.
4. Speaker fill of stereo music is the mixer's, on Windows and Linux:
   stereo to all mains at 0.7071, the centre at 0.3536 x (L+R), LFE silent,
   native multichannel passed through, mono on the front pair, gated by the device's
   channel mask, the headphone form factor and FSMusicSpatialSound
   (0/21/41/51/71) with the rear-else-side rule. The setting is shown on both
   platforms (hidden only in OpenAL and FMOD builds) and applies immediately.
5. `UseMediaPluginsForStreamingAudio` (default off) still selects the media
   plugin path for music, now the stock upstream VLC path (below).
6. Not in scope: voice, the audio of video media (VLC's own output), and
   positional object media audio.

### Keep-or-port judgement

| Part | Decision | Why |
|---|---|---|
| SoLoud's mixing core (voices, faders, resampler, 3D, panning, clipper, active-voice selection) | Rust (`mixer/`) | The mixing. Bit-identical to SoLoud on every recorded render and 0.73-0.86 of its time (below). |
| The SoLoud adapter (`LLAudioEngine_SoLoud`, channel, buffer, listener) | C++ (`*_vks.*`), same call sequences | It is the `LLAudioEngine` subclass the rest of the viewer drives; each method is a few mixer calls. |
| `LLAudioEngine`'s sources, channels, priorities, queued and synced sounds, buffers, transfers, sound history | C++ (unchanged) | Application logic over `LLUUID`, `LLAudioSource` and the asset system; no data transformation; the data paths under it (mix, decode, WAV load) are Rust. |
| `LLWindGen` and the wind voice | Rust (`mixer/wind.rs`) | It runs on the audio thread inside the mix. `updateWind`'s parameter mapping (three lines) stays C++. |
| The output device (miniaudio inside the SoLoud package) and endpoint enumeration | Rust (`device/`: WASAPI, PulseAudio then ALSA loaded at run time, a null device) | Owning the device is what makes the music clock and speaker fill possible. |
| WAV loading (SoLoud `Wav::load` on dr_wav) | Rust (`wav.rs`) | Identical samples on 550 files; malformed files that crashed or read uninitialised memory in dr_wav now fail cleanly. |
| `LLVorbisDecodeState` decode and file assembly | Rust (`asset.rs` on `vkstorm-vorbis`) | Bit-identical and 0.66 of the time. The work queues, the asset cache read and the `LLLFSThread` write stay C++ (rung five's thread-plumbing judgement). |
| `LLVorbisEncode` (uploads) | C++ (libvorbisenc) | No Rust Vorbis encoder reproduces libvorbisenc's psychoacoustic model; porting it is large for an upload-only path and lacks an oracle beyond the C itself. libvorbis stays linked, which also gives the parity test its in-process decode oracle. |
| Parcel music (the VLC media plugin) | Rust (`stream/`) | In-process decoding mixed on the device clock: sample-timed fades with completion on the device clock, no rate change at start, metadata without a side process. |
| HE-AAC (SBR, PS) and Opus decoding | C (FFmpeg's native decoders, dynamically loaded) | Owner's decision: no pure-Rust decoder of production quality for SBR/PS or Opus. LGPL, decoder-only, loaded at run time. |
| MP3, MP2, AAC-LC, FLAC decoding | Rust (symphonia 0.6.1) | Pure Rust, measured at or below today's decoders. ADTS streams go to FFmpeg only when its first frames show SBR or PS. |
| HTTP for streams | libcurl (the viewer's prebuilt, through `vkstorm-http`'s bindings) | The same TLS stack and CA bundle as the rest of the viewer (rung four's judgement). |
| The fade policy state machine (`llvieweraudio.cpp`) | C++ (unchanged) | Viewer startup and parcel policy; the sample-timed envelope it drives is the Rust music bus. |
| Speaker fill | Rust (music bus) | Was a patched VLC filter and output module. |
| An off-the-shelf Rust audio engine (Kira 0.12.5) | Not adopted | Evaluated below: it renders stereo only (silence on channels 3-8), has no Doppler, a different attenuation and panning law, no endpoint selection, and costs 55x the mix time. |

### The boundary

- `VksAudio` is one engine object: the output device and its mixer behind
  one mutex (the device callback takes it per chunk of at most 1024 frames,
  as SoLoud's callback did), plus the music player whose reader feeds the
  mixer's music bus. Restarts (device switch) keep the object, stop the
  device thread, retire every voice (handles never alias: the play index
  continues), keep the buffers, and restore the global volume and the wind.
- Voices are the mixer's `u32` handles; buffers are reference-counted
  `Wav`s, and releasing one stops the voices still playing it, as
  `SoLoud::Wav`'s destructor did.
- Sound assets cross as bytes in, the decoded file's bytes out
  (`vks_audio_decode_asset`), with the error kinds the C++ logs.
- Music: `vks_music_*` start, stop and pause the player and drive the bus
  (gain, hard mute, envelope, transitions by serial, the speaker fill
  layout); metadata comes back through a callback as artist and title or a
  reset, which `LLStreamingAudio_VKS` turns into the LLSD map
  `{ARTIST, TITLE}` the stream title consumers read.
- Strings come back through callbacks that do not throw; panics are caught
  at the boundary and reported as failure values. No C++ exception crosses
  into Rust.

### Behaviour kept, settled, and documented as different

Kept, and covered by the corpora: everything in the scope list. Settled or
different:

- More than 64 voices that keep ticking while inaudible crashed SoLoud
  (`calcActiveVoices` returned before assigning resample buffers, and a newly
  active voice read through a null buffer); 30 channels plus their 20 ms
  declick tails were enough. The mixer assigns the buffers in exactly that
  case; every run SoLoud survived is unchanged. The mixed-scene corpus could
  therefore not record more than 62 voices from the C++; the crate's oracle
  scenes cover 80 voices with the tick off.
- SoLoud set FTZ/DAZ once per process, on whichever thread mixed first, so
  after a device switch it mixed with denormals; the mixer sets them for
  every mix and restores the caller's mode.
- Wind noise was `ll_frand`, per thread and seeded from the clock, never
  reproducible; it is a seeded xorshift. The corpus compares wind by level
  and balance.
- A selected endpoint that is unplugged is reported and logged; the output
  stays silent until another device is selected, as before. In default
  endpoint mode the device follows Windows' default as miniaudio 0.10.42 did,
  but reopens in the original format and lets WASAPI convert when the new
  default has another channel count (miniaudio mixed the channels itself).
- On PulseAudio sinks in ALSA order the SoLoud package passed mixer channels
  straight through, so centre and LFE went to the rears; the device now
  routes by speaker position (`device/layout.rs`). Linux enumerates no
  endpoints, as before (the default device is used).
- Music: gain and envelope advance per decoded frame, so a fade pauses
  during an underrun instead of finishing over silence; a pending fade
  fails when the stream fails, ends, or stalls 30 s past the fade (the VLC
  path's bound), which llvieweraudio turns into a hard stop. A live stream
  that played and drops reconnects after 1, 2, 4 and 8 s while buffered
  audio plays on (VLC did not reconnect). Pause closes a live stream and
  resumes it live, as FMOD's streamer did. ICY titles that are not UTF-8
  are read as Latin-1. MP4/M4A, FLV, WAV, ASF, Matroska, MPEG-TS, HLS, XSPF
  and ASX are refused with a reason.
- The decoded sound file's tail fade has never taken effect: the C++'s
  write-back guard can never be true. The Rust decoder reproduces that, and
  the doc comment says so.

### FFmpeg

HE-AAC and Opus use the decoder-only LGPL FFmpeg 9.0.2 from
[3p-ffmpeg](https://github.com/anne-skydancer/3p-ffmpeg) (release
v9.0.2-r1): libavcodec with only the `aac` and `opus` decoders, libavutil,
libswresample (required by the Opus decoder), shared, no GPL or nonfree
parts, no demuxers (Ogg and ADTS are demuxed in Rust). The engine loads
`avcodec-63` at run time from the viewer's directory (Windows) or `lib/`
(Linux) and checks its major version. The libraries and
`licenses/ffmpeg.txt` are staged next to the viewer. The Linux build uses
glibc symbols up to 2.35.

### Parity corpora

All recorded from the C++ before the port (commits "Audio parity: record
..."), on Windows, under `indra/llaudio/tests/fixtures`.

| corpus | what it holds | result |
|---|---|---|
| decoded sound assets (`decode/`, `llaudio_decode_parity.txt`, 592 KB of input) | 58 Ogg Vorbis assets (31 encoded as the viewer encodes uploads, 27 damaged), their outcomes and the SHA-256 of every decoded file, six in full | 58/58 identical on Windows (against the record) and on Linux with GCC 15 and GCC 14 (against libvorbisfile in the same process, since libvorbis 1.3.7 decodes differently there: MSVC's float pi) |
| mixed scenes (`llaudio_mix_{2,6,8}ch.vksa`, 8.9 MB, script and observations) | 15 scenes through `LLAudioEngine`, `LLAudioSource` and `LLAudioChannel` on the null device at 44.1 kHz and 2, 6 and 8 channels: sources passing by, a turning listener, attenuation 0.5-600 m, rolloff 0.4/1/19, Doppler, 62 voices with evictions, declicked stops and buffer switches, synced starts, loops and queued sounds, 2-D sounds, gains and mute, wind, two other block sizes | 38/38 renders bit-identical (641,214 frames, max difference 0), wind within level and balance, 1,657/1,657 observations, on Windows, Linux GCC 15 and Linux GCC 14 |
| speaker fill (`speakerfill_matrices.txt`) | the real VLC speakerfill filter and patched WASAPI output: fill matrices for impulses over 12 masks, the negotiation over 7 device conditions, 8 masks and every layout, the gain curve, the 50 ms ramp, the 3 s and 2 s envelopes and their completion frames | 92 matrices and 240 decisions identical, the cubic curve and envelopes exact, the ramp within 6e-8 (vkstorm-audio `tests/speakerfill_record.rs`); the rows that have no counterpart (a device rejecting the surround format, 4.0 and 2.0+sides layouts no setting requests) are listed there |

The crate's own oracle (`tools/soloud-oracle`: SoLoud from the patched
source and the viewer's `llwindgen.h`) adds 150 scene renders (44.1 and 48
kHz, 1/2/4/6/8 channels, 80 voices, odd chunk sizes) bit-identical on
Windows, GCC 14 and GCC 15; 550 WAV files read identically to `Wav::load`;
and the decoded file of all 1201 vkstorm-vorbis corpus files byte-identical.
Floating-point and libm: the scenes go through `sqrt` (exact) and constant
`sin`/`cos`; the only libm-sensitive value is the wind generator's `cosf`,
which rounds differently in the UCRT and glibc (in the 48 kHz wind-decay
scene, max 3.9e-7 from 7.05 s). Each platform's port matches its own
SoLoud; the crate's parity test compares wind-only scenes by energy within
1e-6 relative and peak within 2e-6 (500x the measured gap), and the viewer
test compares wind by level, so neither depends on the libm version (CI's
Linux is glibc 2.39, WSL 2.43).

### Tests

- vkstorm-core-rust: `cargo test -p vkstorm-vorbis` (1356 fixture traces
  against each platform's expectations, plus the opt-in full corpus against
  the C oracle) and `cargo test -p vkstorm-audio --features link-curl`
  (unit tests; the device callback contract; the adapter test transcribed
  from `llsoloud_adapter_test.cpp`; mixer parity; WAV and asset parity; the
  speaker fill against the VLC record and the transcribed
  `vlc_speakerfill_test.cpp` cases; every music format and behaviour end to
  end against a mock radio server). Pass on Windows and Linux.
- `INTEGRATION_TEST_llaudio_parity`: the three corpora through the viewer's
  classes (above).
- `INTEGRATION_TEST_llaudio_music`: parcel music through
  `LLStreamingAudio_VKS` on the null device against
  `indra/newview/tests/vksaudio_mock_stream_server.py` (fixtures in
  `indra/newview/tests/fixtures/music`, made by `make_fixtures.sh`): every
  format with its tones and exactly ceil(source frames x 48000 / source
  rate) output frames per segment (no rate change, the first second
  included); the fade-in from exactly zero along the linear envelope,
  completed only after the device clock passed its final sample; the
  fade-out to silence; ICY titles over `ICY 200 OK`; chained Ogg comments;
  PLS and M3U; a reconnect; refusals of video and HTML with the pending fade
  failed; the speaker fill at 5.1.
- Both are in CI's focused list. With CI's configuration they pass on
  Windows (MSVC 19.44) and in WSL (Ubuntu 26.04) with GCC 15 and with
  GCC 14, as does the rest of the focused list.
- The viewer builds, stages (FFmpeg and its licence included) and reaches
  the login screen on Windows with empty settings and user directories; the
  log shows `SoLoud (Rust), version 202002, backend WASAPI @ 48000 Hz, 6
  channels, 1920 frame buffer, mask 0x3f, speakers, device Speakers (2-
  Sound Blaster AE-7)` and `Using default impl to render streaming audio`.
  The engine reports itself as `SoLoud 202002 (Rust)` (Audio Driver
  Version), the SoLoud release its mixer is a port of
  (`vkstorm_audio::UPSTREAM_BASELINE`: 202002, upstream e82fd32c1f, 3p-soloud
  patch level vulkanstorm3), and the verbose form keeps the SoLoud backend's
  wording.

### Measurements

AMD Ryzen 9 9950X3D; Windows 11 with MSVC 19.44; Ubuntu 26.04 under WSL.
Rust built as the viewer builds it (fat LTO, one codegen unit,
`x86-64-v3`). Minimum of three interleaved runs unless stated.

| hot path | what is timed | Windows C++ | Windows Rust | Linux C++ (GCC 14 / 15) | Linux Rust |
|---|---|---:|---:|---:|---:|
| mix, 2 channels | one 1024-frame mix, 64 moving 3D voices, 48 kHz (crate bench against the SoLoud oracle) | 46.1 µs | 39.0 µs | 51.2 / 47.9 µs | 39.7 / 39.8 µs |
| mix, 6 channels | same | 126.8 µs | 98.8 µs | 125.4 / 118.7 µs | 98.7 / 102.3 µs |
| mix, 8 channels | same | 138.9 µs | 116.7 µs | 162.3 / 161.3 µs | 118.2 / 119.2 µs |
| mix through the viewer (parity test, `VKS_AUDIO_BENCH`) | 2 / 6 / 8 channels, one 1024-frame block | 48.6 / 121.3 / 150.4 µs | 40.5 / 110.0 / 125.0 µs | | |
| per-block channel updates (same harness) | `update3DPosition` and the listener commit for 64 voices, 2 / 6 / 8 channels | 2.6 / 3.0 / 3.1 µs | 3.4 / 5.0 / 5.0 µs | | |
| sound asset decode (viewer decode step) | 31 assets, 5.52 MB of decoded files | 67.7 ms | 44.5 ms | | |
| sound asset decode (crate, 308 files) | | 466 ms | 427 ms | 444 / 450 ms | 410 / 412 ms |
| Vorbis decode (vkstorm-vorbis vs libvorbis) | 308 files / 5 min stereo | 623 / 256 ms | 568 / 220 ms | 599 / 604, 247 / 257 ms | 559, 220 ms |

The per-block channel updates are the one row above the C++: each voice
parameter is an FFI call taking the mixer lock (as each SoLoud call took
its mutex), 1.5-2 µs more per 21 ms block with 64 voices, under 0.01 % of
the block. Per-run figures of the viewer harness (2/6/8 channels): C++
48.6/121.3/150.4, 52.7/144.3/167.1, 53.2/142.5/160.9 µs; Rust
43.5/118.1/139.4, 42.0/110.0/125.5, 40.5/110.1/125.0 µs.

Stream decode CPU (demux, decode and conversion to 48 kHz stereo, ms of CPU
per second of audio). Today's path is VLC 3.0.21 on Windows (libvlc with
an `amem` sink converting to 48 kHz float, the media plugin's decode work
without the network and the device; process CPU, Windows' 15.6 ms ticks)
and GStreamer on Linux (the viewer's Linux media plugin; `decodebin !
audioconvert ! audioresample` to 48 kHz float into `fakesink`, about 600 s
of audio per run). The Rust figure is the session thread's CPU (vkstorm-audio
`tests/stream_decode_cpu.rs`, about 600 s of audio per run).

| format | Windows VLC (runs) | Windows Rust | Linux GStreamer | Linux Rust |
|---|---:|---:|---:|---:|
| MP3 128k 44.1 kHz stereo | 2.57 (2.12-3.48) | 0.75 | 1.15 | 0.74 |
| MP3 64k 22 kHz mono | 1.20 (1.20-2.65) | 0.57 | 0.55 | 0.57 |
| MP2 192k 48 kHz | 1.95 (1.95-3.51) | 0.31 | 0.33 | 0.29 |
| Ogg Vorbis q3 44.1 kHz | 2.19 | 0.91 | 2.77 | 0.82 |
| AAC-LC 128k 44.1 kHz | 2.13 (2.13-2.32) | 0.70 | 0.90 | 0.75 |
| HE-AAC v1 48 kHz | 2.04 (0.76-2.67) | 0.63 | 0.60 | 0.56 |
| HE-AAC v2 44.1 kHz | 2.08 (1.89-3.02) | 0.99 | 1.29 | 0.92 |
| Opus 96k 48 kHz | 4.14 | 0.89 | 3.69 | 0.82 |
| FLAC 44.1 kHz | 0.86 (0.86-1.43) | 0.91 | 1.07 | 0.80 |

The Rust path is at or below today's on every row but two that are within
the measurement: FLAC on Windows (0.91 against VLC's best run of 0.86;
its other runs 0.86-1.43, at a 15.6 ms CPU-time resolution over totals of
0.06-0.3 s) and 22 kHz mono MP3 on Linux (0.57 against 0.55). The Rust
figures include a polyphase sinc resampler (0.33 ms per second of stereo
44.1 to 48 kHz; flat to 18 kHz, at least 82 dB of stopband rejection),
where VLC and GStreamer use their default, lower-quality resamplers. VLC's
Ogg and Opus runs are single runs (its repeat option does not loop those
files reliably).

### Kira

[Kira](https://github.com/tesselode/kira) 0.12.5, the main Rust game-audio
engine, was evaluated as the effects mixer instead of the SoLoud port, on
its source and a benchmark (the 64-voice scene through its mock backend,
64 spatial tracks, 1024-frame blocks at 48 kHz, `x86-64-v3`, fat LTO):

- It mixes a stereo `Frame` internally and writes silence to every output
  channel past the second (`backend/renderer.rs`): positional sound on 5.1
  and 7.1 output, the 7.1 order and the music speaker fill are impossible
  without rewriting its core.
- Its spatial model is a different law: volume interpolated in decibels over
  a normalised distance between two radii, panning from the dot product of
  per-ear directions; there is no inverse-distance attenuation with rolloff
  and no Doppler (no velocities anywhere). The output could not match the
  recorded SoLoud scenes.
- Its cpal backend opens the default output device; there is no selection
  by endpoint identity, no form factor, no device clock.
- It took 2.34 ms per 1024-frame block for the 64-voice scene, 60 times
  vkstorm-audio's 39 µs (per-sample spatialisation and parameter
  interpolation on every track).

Adopting it would change what users hear and lose features; the port keeps
SoLoud's behaviour exactly at lower cost.

### What was removed from the VLC plugin, and what was kept

Removed (the VLC music path is the stock upstream one again):

- the retired PCM bridge and its WASAPI endpoint code (`llvlcaudio.*`,
  `llvlcaudiobridge.*`, `llvlcwasapi.h`, `llvlcendpoint.h`,
  `prepare_miniaudio.cmake`, `LL_VLC_PCM_AUDIO`, `LL_VLC_AUDIO_BRIDGE_TESTS`
  and their tests);
- the speaker-fill VLC filter and output modules (`vlc_speakerfill.cpp`,
  `prepare_speakerfill.cmake`, `llvlcspeakerfillconfig.*`, the MinGW build of
  the output module, their test, their staging and the `speakerfill-source`
  LGPL bundle);
- the `media_audio`/`media_music` protocol: generation/serial handshake,
  configure, set_gain, spatial and transition messages, `media_audio.state`,
  the capability negotiation (`llpluginclassmedia`, `llpluginmessageclasses.h`,
  `media_plugin_libvlc.cpp`, `llviewermedia.cpp`'s object/nonpositional
  roles and spatial projection, `LL_VLC_AUDIO_PROTOCOL_TESTS` and
  `llviewermedia_audio_test.cpp`);
- in `LLStreamingAudio_MediaPlugins`: the fade API overrides, hard mute,
  pending fades, the FSMusicSpatialSound push, the status-json poller (now
  in Rust) and the metadata emission; in the plugin: in-band metadata
  (`libvlc_MediaMetaChanged`), the hidden instance's `:network-caching=5000`
  and `:no-video`, music volume branches.

Kept (general plugin fixes, not music): VLC callbacks only store atomics
and the plugin thread does the work; the title string is freed; one
`stopPlayer()` detaches exactly the events it attached and releases the
player and media (upstream leaked them on every reload or resize);
`size_change` stops before swapping the pixel buffer; `resetVLC()` is
null-safe; stop releases the player and start rebuilds it; error paths
report `STATUS_ERROR`; FreeBSD's `--aout=oss`. `llvieweraudio.cpp`'s fade
policy and `llstreamingaudio.h`'s fade interface are kept (the Rust slot
implements it; the stock VLC path does not, so llvieweraudio uses its
frame-timed fade there, with the login hold at zero). `scripts/perf/
test_login_music_gate.py` stays: it tests that policy, not the protocol.

### Listening material

Rendered by the engine on its null device from a 24 s Ogg Vorbis piece
(chords over a 220 Hz drone) streamed from the mock server and resampled to
48 kHz, with the viewer's fade policy (vkstorm-audio `examples/render_music.rs`):
a fade-in after the login hold and a fade-out; a stream start under load
(1.5 s slow answer, stalls, late device periods, eight spinning threads);
the same start at 5.1 through the speaker fill. The checks on them: the
drone stays at 220.000 Hz in every 2 s window (no speed change), no silent
gap after the first sound, the envelopes within 0.042 of the linear ramps,
BL = FL and BR = FR sample for sample at 5.1, LFE silent.

### Platform edges

- Windows and Linux agree on every corpus. libvorbis decodes differently on
  the two (MSVC's `math.h` has no `M_PI`, so its MDCT tables use the float
  constant); vkstorm-vorbis reproduces each platform's C, and the viewer
  test compares Linux with libvorbisfile in the same process.
- FFmpeg's Linux libraries need glibc 2.35 at most (built on Ubuntu 22.04);
  the engine loads them from `lib/` next to the viewer, the test from the
  prebuilt package directory (`VKS_FFMPEG_DIR`).
- The music test needs Python for the mock server, as the asset tests do.
  On Windows, Python 3.14 loses about a third of server-initiated FINs on
  loopback; the server lets the client close sized responses.
- Real devices: the WASAPI path was opened on the machine's three endpoints
  (6-channel Sound Blaster, HDMI, S/PDIF) and PulseAudio and ALSA under
  WSLg; unplugging, default-device moves and audible checks on speakers were
  not exercised by the automation.

## Rung seven: inventory (in progress)

The inventory rung follows the pattern of the earlier rungs: the record
classes (`LLInventoryItem`, `LLInventoryCategory`, `LLPermissions`,
`LLSaleInfo` and their viewer subclasses) and the `LLInventoryModel`
containers stay C++, because viewer code writes their fields directly:
184 files use `gInventory` (about 2,500 lines) and 173 work with
`LLViewerInventoryItem`. Rust makes the decisions behind them; flat records
and operation lists cross the C ABI. The plan, the eight open decisions and
the oracle inventory are in `doc/survey/03-content-model.md` §4-§6.

The model's storage (items, folders, observers) moves to Rust with the UI
rung, when the inventory panels, folder views and filter, its heaviest
readers, move to Rust and can read it directly. Moving it before then would
need a C++ proxy layer over Rust records in every one of those files, which
the UI rung would throw away. A move of the containers alone after R4b is
considered only if measurements show the C++ storage to be a bottleneck
(owner's decision, 2026-10-31).

Sub-rungs and their oracles:

| Sub-rung | Scope | Oracle test | Fixtures |
|---|---|---|---|
| R1 | Data model: types and flags, name and description normalisers, permissions, aggregate permissions, sale info, items, categories, CRCs, LLSD and legacy text forms, message blocks | `INTEGRATION_TEST_llinventory_parity` | `indra/llinventory/tests/fixtures` (C1-C10) |
| R2 | Inventory cache file codec (load, save, validation) and the agent-inventory UDP message handlers | `INTEGRATION_TEST_llinventorymodel_parity` | `indra/newview/tests/fixtures/inventory/cache`, `msgs` |
| R3a | AIS response parsing and model updates | `INTEGRATION_TEST_llaisupdate_parity` | `inventory/ais/update` |
| R3b | Background fetch scheduling | `INTEGRATION_TEST_llinvfetch_parity` | `inventory/ais/fetch` |
| R4a | Object (task) inventory | `INTEGRATION_TEST_llinventory_task_oracle` | `inventory/task` |
| R4b | Inventory offers and giving | `INTEGRATION_TEST_llinventory_offer_oracle` | `inventory/offer`, `give` |

Each sub-rung has a `check_r*_copies.py` script that keeps the frozen oracle
copies identical to the production sources they were taken from.

State (October 2026): the oracle phase is complete for all six sub-rungs. R1
is implemented in the `vkstorm-inventory` crate of vkstorm-core-rust and
replays every R1 corpus byte for byte, with and without `opensim`. On
`codex/inventory-r1` the viewer calls it through `vkstorm-ffi` (`vks_inv_*`,
vkstorm-core-rust #16): `LLPermissions`, `LLAggregatePermissions`,
`LLSaleInfo`, the item, category and object rules and `LLNotecard` keep
their fields and call Rust, and `INTEGRATION_TEST_llinventory_parity`
compares them with the frozen C++ (0 differences). Legacy text readers take
the rest of the stream from what its buffer holds and put back what they did
not consume (`llinventory_vks.h`): `tellg`/`seekg` do not round-trip on a
Windows text-mode file with LF line ends. R2-R4b are
implemented in the same crate (vkstorm-core-rust #8, with the map order taken
from the C++ caller in #9) and replay their oracle recordings per C++
standard library; none is behind `vkstorm-ffi` yet.

### R1: what is Rust and what stayed C++

Every R1 function was first wired through `vkstorm-ffi` and timed with
`llinventory_tool bench` (built with `LL_TESTS`): the frozen C++ copies in
`llinventory_oracle.cpp` against the viewer's Rust-backed classes, in the
same binary, over 100,000 synthetic items and the Linden Library cache (C10),
best of three. The owner decided per function from those numbers
(2026-10-31). Windows, RelWithDebInfo, ns per item (per check for the
permission checks), C++ (frozen) → Rust:

| Function | C++ → Rust | Kept as |
|---|---|---|
| `LLInventoryItem::fromLLSD` (new item each) | 1551.6 → 4224.1 | C++ |
| `LLInventoryItem::asLLSD` | 2004.5 → 3597.5 | C++ |
| item legacy export | 4955 → 992 | Rust |
| item legacy import (new item each) | 7770 → 5798 | Rust |
| `LLInventoryItem::getCRC32` | 17.0 → 13.6 | Rust |
| `LLPermissions::fix` | 2.8 → 3.0 | Rust |
| `allowOperationBy` (5 ops × 3 agents) | 5.8 → 4.1 | Rust |
| `allowModifyBy` | 3.4 → 1.8 | Rust |
| `allowCopyBy` | 6.1 → 4.5 | Rust |
| `allowTransferTo` | 3.0 → 1.9 | Rust |
| `getOwnership` | 2.9 → 2.4 | Rust |
| `getSafeOwner` | 2.2 → 3.6 | Rust (cold call) |
| `initMasks(EType)` | 2.4 → 2.3 | Rust |
| `LLPermissions::accumulate` | 9.0 → 8.3 | Rust |
| `setOwnerAndGroup` | 20.2 → 16.3 | Rust |
| `LLSaleInfo::getCRC32` | 1.7 → 1.8 | Rust |
| `correctInventoryName` | 43.9 → 77.5 | Rust (cold call) |
| category legacy export | 1464 → 532 | Rust |
| category legacy import (new each) | 2289 → 1595 | Rust |
| Library `fromLLSD` | 652 → 3141 | C++ |
| Library `asLLSD` | 1890 → 3520 | C++ |
| Library legacy export | 4750 → 948 | Rust |
| Library categories `importLLSDMap` | 275.7 → 2853.5 | C++ |

Rust is as fast or faster everywhere except the LLSD forms, which pay for
flattening a C++ `LLSD` tree into a node buffer, crossing the C ABI and
building the result tree back (2 to 10 times the C++). So the LLSD forms are
the original C++ again: `LLInventoryItem::asLLSD`/`fromLLSD`,
`ll_create_sd_from_inventory_item`, `LLInventoryCategory::asLLSD`,
`asAISCreateCatLLSD`, `fromLLSD`, `exportLLSD`, `importLLSDMap`,
`importLLSD`, `ll_create_sd_from_inventory_category` and
`ll_create_category_from_sd`. For consistency the LLSD forms of
`LLPermissions` (`packMessage()`/`unpackMessage(LLSD)`, `importLLSD`,
`ll_create_sd_from_permissions`, `ll_fill_sd_from_permissions`,
`ll_permissions_from_sd`) and `LLSaleInfo` (`asLLSD`, `fromLLSD`,
`packMessage()`/`unpackMessage(LLSD)`, `ll_create_sd_from_sale_info`,
`ll_sale_info_from_sd`) are C++ again too: the item's LLSD forms call them,
and a crossing left there would remain in the C++ path. They still call the
Rust rules inside them (`fix`, `fixOwnership`, `initMasks`, `setSalePrice`,
`rename`...). `check_r1_copies.py` checks on every run that each of them is
still identical to its frozen copy (`KEPT_CPP`). They retire, rather than
move, once R2 (the cache load in Rust) and R3a (AIS bodies parsed in Rust,
option B) no longer build C++ `LLSD` trees; the crate keeps their logic for
that. `vkstorm-ffi` exports no LLSD entry point for R1 any more, and the
viewer's `LLSDFlat`/`ll_sd_from_vks_doc` plumbing is gone; both can return
with R3a. `getSafeOwner` and `correctInventoryName` are slower in Rust by
nanoseconds but are cold calls, so they stay Rust.

The Linux numbers come from CI: non-Release builds run
`llinventory_tool bench 3 --library <C10 cache>` on Windows and Linux after
the tests and append the output to the job summary (the step never fails a
build). The rows marked `[C++]` there are the LLSD forms, C++ in both
columns. Linux, GitHub runner (ubuntu-26.04, GCC, RelWithDebInfo), one run,
ns per item, from #277's CI:

| Function | C++ → Rust |
|---|---|
| item legacy export | 2829.1 → 689.5 |
| item legacy import (new item each) | 3813.9 → 2857.6 |
| `LLInventoryItem::getCRC32` | 25.7 → 13.8 |
| `LLPermissions::fix` | 8.3 → 3.5 |
| `allowOperationBy` (5 ops × 3 agents) | 1.7 → 1.5 |
| `allowModifyBy` | 2.2 → 2.2 |
| `allowCopyBy` | 2.2 → 2.1 |
| `allowTransferTo` | 1.3 → 1.3 |
| `getOwnership` | 0.9 → 2.0 |
| `getSafeOwner` | 1.1 → 2.7 |
| `initMasks(EType)` | 3.5 → 2.9 |
| `LLPermissions::accumulate` | 15.7 → 9.2 |
| `setOwnerAndGroup` | 14.4 → 14.1 |
| `LLSaleInfo::getCRC32` | 7.4 → 8.9 |
| `correctInventoryName` | 32.3 → 41.4 |
| category legacy export | 873.6 → 245.2 |
| category legacy import (new each) | 1085.0 → 979.1 |
| Library legacy export | 2892.2 → 666.6 |
| Library `getCRC32` | 24.8 → 13.5 |

The picture matches Windows: the text forms and CRCs are 1.1 to 4 times
faster, the permission checks equal. Four rows are slower by one to nine
nanoseconds (`getOwnership`, `getSafeOwner`, `LLSaleInfo::getCRC32`,
`correctInventoryName`): GCC inlines the C++ forms, which the call into the
Rust library cannot be without cross-language LTO. `getSafeOwner` and
`correctInventoryName` stay Rust (owner's decision: cold calls);
`getOwnership` and `LLSaleInfo::getCRC32`, faster or equal on Windows, are
Rust pending the owner's decision on these numbers.

## Local build

Follow the project's RelWithDebInfo procedure (see `scripts/validate_audio.sh`
for the canonical variables), then:

```sh
autobuild configure -A 64 -c ReleaseFS_open -- --no-opensim --zink --avx2 --no-package \
  -DUSE_DISCORD:BOOL=OFF -DOPENSIM:BOOL=FALSE -DLL_TESTS:BOOL=FALSE
cmake --build build-vc180-64 --config RelWithDebInfo --target vulkanstorm-bin -- -m:4 -nologo
cmake -S indra -B build-vc180-64 -DLL_TESTS:BOOL=ON
cmake --build build-vc180-64 --config RelWithDebInfo --target INTEGRATION_TEST_llimagej2coj -- -m:4 -nologo
cmake --build build-vc180-64 --config RelWithDebInfo --target INTEGRATION_TEST_llsdserialize_parity llsdserialize_tool -- -m:4 -nologo
cmake --build build-vc180-64 --config RelWithDebInfo --target INTEGRATION_TEST_llvolume -- -m:4 -nologo
cmake --build build-vc180-64 --config RelWithDebInfo --target INTEGRATION_TEST_llimagej2coj INTEGRATION_TEST_llimagecodecs -- -m:4 -nologo
cmake --build build-vc180-64 --config RelWithDebInfo --target INTEGRATION_TEST_llmessagetemplate -- -m:4 -nologo
cmake --build build-vc180-64 --config RelWithDebInfo --target INTEGRATION_TEST_llcorehttp_parity INTEGRATION_TEST_llcorehttp -- -m:4 -nologo
cmake --build build-vc180-64 --config RelWithDebInfo --target INTEGRATION_TEST_llmeshunpack_parity INTEGRATION_TEST_lltexturecache_parity INTEGRATION_TEST_lltexturefetch_parity -- -m:4 -nologo
cmake --build build-vc180-64 --config RelWithDebInfo --target INTEGRATION_TEST_llaudio_parity INTEGRATION_TEST_llaudio_music -- -m:4 -nologo
cmake --build build-vc180-64 --config RelWithDebInfo --target INTEGRATION_TEST_llinventory_parity INTEGRATION_TEST_llinventorymodel_parity INTEGRATION_TEST_llinvfetch_parity INTEGRATION_TEST_llaisupdate_parity INTEGRATION_TEST_llinventory_task_oracle INTEGRATION_TEST_llinventory_offer_oracle -- -m:4 -nologo
```

The build directory is `build-vc180-64` (Visual Studio 2026) for both
RelWithDebInfo and Release. Integration tests are built for RelWithDebInfo
only; a Release build keeps `LL_TESTS` off.

Under Git Bash pass MSBuild switches with a hyphen (`-m:4`), not a slash.
The llvolume test also records the corpus (`VKS_VOLUME_DUMP=<file>`) and
times it (`VKS_VOLUME_BENCH=<iterations>`, `VKS_VOLUME_BENCH_OUT=<csv>`);
the same timing for the crate alone is `cargo bench --bench mesher` in
vkstorm-core-rust with the same environment variables.
The asset pipeline tests record (`VKS_MESH_RECORD`, `VKS_TEXCACHE_RECORD`,
`VKS_TEXFETCH_RECORD=<file>`) and time (`VKS_MESH_BENCH`,
`VKS_TEXCACHE_BENCH`, `VKS_TEXFETCH_BENCH=<iterations>`, each with a
`_OUT=<csv>`); the two fetch tests run under
`indra/newview/tests/vksassets_mock_server.py`, as CMake's test step
does. The crate-side timing is `cargo bench --bench hot_paths` in
vkstorm-core-rust.
The audio tests record (`VKS_AUDIO_RECORD=<dir>`, which writes
platform-suffixed files off Windows) and time (`VKS_AUDIO_BENCH=<blocks>`,
`VKS_AUDIO_BENCH_OUT=<csv>`); `VKS_AUDIO_MIX_TOLERANCE`,
`VKS_AUDIO_DECODE_REFERENCE=oracle`, `VKS_AUDIO_SCENES=<script>` and
`VKS_AUDIO_TRACE=1` are diagnostics. The music test runs under
`indra/newview/tests/vksaudio_mock_stream_server.py`. The crate-side tools
are `tools/soloud-oracle`, `tools/vorbis-oracle`, `cargo bench --bench
mixer` (with `SOLOUD_ORACLE`) and `cargo run --example render_music` in
vkstorm-core-rust.
