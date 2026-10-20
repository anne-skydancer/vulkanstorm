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
  C++ (`indra/llmath/tests/fixtures/llvolume_corpus.vksv`, 2.2 MB) is the
  oracle; regenerating it means building `INTEGRATION_TEST_llvolume` at the
  pre-rung commit with `VKS_VOLUME_DUMP=<file>`.
- `LLVolume`, `LLProfile`, `LLPath` and `LLVolumeFace` stay the in-memory
  representation. Profile and path generation return pointers into
  per-thread scratch storage that the C++ copies out (a few hundred bytes);
  the mesh sweep, the face builders and the tangents write straight into
  the caller's 16-byte aligned buffers (`mMesh`, `mPositions`, `mNormals`,
  `mTexCoords`, `mIndices`, `mTangents`), so nothing the renderer consumes
  is copied. `LLPath::PathPt` and `LLProfile::Face` keep their layout, shared
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
- Floating-point identity. The C++ operation order is kept; `lerp` is MSVC's
  `std::lerp` (an `fma` at its core, so Rust's `mul_add`); `_mm_rsqrt_ps`
  results are CPU-specific (Intel and AMD tables differ), so the fixture
  carries a signature of the recording CPU and the tests compare the
  rsqrt-derived arrays (cap normals, tangents, normalised normals) with a
  tolerance only when it differs; `sinf`/`cosf` are the platform libm as in
  the C++ (UCRT on Windows), so off Windows/MSVC the float arrays are
  compared with a tolerance and the structural data exactly. One LLVM
  canonicalisation (`x * -1.0` to a sign flip) had to be prevented because
  it differs from a multiply for the NaN a zero-area cap produces.
- Parity: `cargo test -p vkstorm-volume` (crate) and
  `INTEGRATION_TEST_llvolume` (viewer, through the FFI) rebuild all 881
  cases, 3939 faces, 27158 array blocks (positions, normals, texture
  coordinates, indices, tangents, normalised normals, profile points and
  faces, path points, mesh) plus counts, extents, centres, texture
  extents, face masks and LOD triangle estimates: all bit-identical on the
  recording machine (Windows, MSVC 19.44, AMD Ryzen 9 9950X3D). The corpus
  covers all six profile curves, four hole shapes, line/circle/circle2/test
  and flexible paths, hollow, both cuts, twist, taper, shear, radius offset,
  revolutions, skew and ratio, detail 0, 1, 1.5, 2.5 and 4, and sculpt
  stitching (sphere, torus, plane, cylinder, invert, mirror).
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

## Local build

Follow the project's RelWithDebInfo procedure (see `scripts/validate_soloud.sh`
for the canonical variables), then:

```sh
autobuild configure -A 64 -c ReleaseFS_open -- --no-opensim --zink --avx2 --no-package \
  -DUSE_DISCORD:BOOL=OFF -DOPENSIM:BOOL=FALSE -DLL_TESTS:BOOL=FALSE
cmake --build build-vc170-64 --config RelWithDebInfo --target vulkanstorm-bin -- -m:4 -nologo
cmake -S indra -B build-vc170-64 -DLL_TESTS:BOOL=ON
cmake --build build-vc170-64 --config RelWithDebInfo --target INTEGRATION_TEST_llimagej2coj -- -m:4 -nologo
cmake --build build-vc170-64 --config RelWithDebInfo --target INTEGRATION_TEST_llsdserialize_parity llsdserialize_tool -- -m:4 -nologo
cmake --build build-vc170-64 --config RelWithDebInfo --target INTEGRATION_TEST_llvolume -- -m:4 -nologo
```

Under Git Bash pass MSBuild switches with a hyphen (`-m:4`), not a slash.
The llvolume test also records the corpus (`VKS_VOLUME_DUMP=<file>`) and
times it (`VKS_VOLUME_BENCH=<iterations>`, `VKS_VOLUME_BENCH_OUT=<csv>`);
the same timing for the crate alone is `cargo bench --bench mesher` in
vkstorm-core-rust with the same environment variables.
