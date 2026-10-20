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
- The fixtures and parity were produced on Windows. Linux's glibc differs
  from the UCRT for the modelled edge inputs (hex floats, `sscanf` overflow,
  NaN spelling), where the Rust now behaves the same on both platforms.

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
```

Under Git Bash pass MSBuild switches with a hyphen (`-m:4`), not a slash.
