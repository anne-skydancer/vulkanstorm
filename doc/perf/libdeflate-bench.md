<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# libdeflate vs zlib-ng for mesh LODs and LLSD blobs (bench)

Status: **measured, 2026-11-16 and 2026-11-17**, offline on the owner's PC, no viewer build, no viewer run.
**Decided:** D1 option 1 (zlib-ng single call, draft PR #547), D2 as recommended (caps hotfix 87379e897f); see section 8.
Harness: `scripts/bench/libdeflate/` (this branch). Owner request: prove that libdeflate does for LODs and mesh
geometry what zlib-ng does, without tanking fps and without warped results, or disprove it.

Labels: **[measured]** this bench, numbers below; **[code]** read in the source at the cited file:line
(devel `a18403dd0d`; `indra/llcommon` and `indra/llmath` are identical on release); **[inferred]** reasoned from the two,
not measured.

Hardware: AMD Ryzen 9 9950X3D (Windows reports 8 cores / 8 logical processors to this session), 61.6 GB RAM, Windows 11
Pro 10.0.26300, MSVC 14.51.36231 (VS 18). Other agents were building on the same PC during the session; every timing
section waits until no `cl`, `link`, `MSBuild`, `cmake`, `ninja`, `rustc` or `cargo` runs, runs the strategies round-robin
inside each repetition and reports the minimum (and median) of 7 to 9 repetitions. A `cpptools` and a `find` process
used about one core each throughout; they do not favour either library (interleaved).

## 1. Summary

1. **No warped results [measured].** On 53,915 real blocks from the owner's cache (13,608 mesh assets: 46,188 LOD,
   6,663 skin, 778 physics_convex, 286 physics_mesh; 1,260 MB compressed, 1,817 MB inflated), libdeflate 1.26 gives
   **byte-identical output for every block**. Decoded one level up with the viewer's own Release code
   (`LLVolume::unpackVolumeFaces`, its private `unpackVolumeFacesInternal`, `cacheOptimize`, `LLSDSerialize::fromBinary`),
   **all 46,474 LOD and physics_mesh blocks give bit-identical geometry** (156,000 faces, 60.4 M vertices, 84.7 M triangles:
   positions, normals, tangents, UVs, weights, indices, extents) and all 7,441 skin/convex blocks give identical LLSD.
2. **Robustness [measured].** 862,640 mutated inputs (truncations, bit flips, header and trailer edits, trailing bytes,
   zeroed runs), libdeflate under AddressSanitizer: **no case where both accept with different output, no case only
   zlib-ng accepts, no ASan report**. Adler-32 is enforced: a one-bit trailer flip is refused by both on all 53,915 blocks.
   Two single-bit flips out of 107,830 were accepted by libdeflate only; an exhaustive flip scan of those blocks shows
   4 such bits out of 5,128, each **decoding to exactly the original data** (libdeflate's documented leniency for an
   incomplete Huffman code, `deflate_decompress.c:789-842`).
3. **No fps effect [code].** Every mesh inflate runs on the mesh repo thread or the two `MeshLodProcessing` pool threads;
   the main thread only queues requests (section 3). Main-thread ms per frame from mesh inflate is **0 today and 0 with
   libdeflate**. The main-thread inflate sites are small or rare (legacy materials responses, navmesh, Milky Way map once,
   inventory cache at login).
4. **Speed [measured].** Whole corpus, one thread, clean run (2026-11-17): zlib-ng as the viewer calls it
   **2,777 ms**; the decided zlib-ng single call (1 MiB kept per thread) **2,705 ms (−2.6 %)**; libdeflate with the same
   bounded memory **2,581 ms (−7.1 %)**, or **2,442 ms (−12 %)** if it may keep up to 16 MiB per thread. Per block at the
   median size: 4.1 µs today, 2.9 µs libdeflate; at the largest block (5.64 MB = 5,505 KiB out): 10.3 vs 6.9 ms.
   Inflate is only about **8 % of the LOD unpack** (parse, unpack and `cacheOptimize` are the rest): with the order
   alternated per block and bounded memory, libdeflate makes the whole `lodReceived` work **0.7 %** cheaper (34.5 s to
   34.3 s for 46,188 LOD blocks). **Settle time at Rooftops-scale loads: no measurable change [inferred]**: at most
   0.14 to 0.24 s of worker CPU over a 113 s settle, on threads that are not the bottleneck.
5. **No added lag [measured + code].** Every whole-buffer site already waits for the complete body before it inflates
   (mesh: `llmeshrepository.cpp:3755-3768`); libdeflate shortens that wait at every size measured. The one place where a
   switch would add latency is the inventory cache at login (79 MB: +15 ms with the file write): it stays on zlib-ng.
6. **Recommendation (decided: D1 option 1, D2 as recommended): libdeflate is proven safe, but the speed does not
   justify it.** Both alternatives save little on worker threads: the zlib-ng single call about 0.2 % of the LOD unpack
   [inferred: 2.6 % of an 8 % share], libdeflate 0.7 % [measured]. Option 1, hygiene with zlib-ng (one
   `inflate(Z_FINISH)` into a bounded per-thread scratch, plus a per-call output cap), was recommended for bounded
   memory and the cap rather than for speed. libdeflate cannot resume a decode, so it keeps its full lead only with a
   large kept buffer (up to 16 MiB per thread). Options in section 8; the libdeflate design is in section 7.

## 2. Latency: last byte in to usable output

What the owner's "lag" question measures: the time from the last byte of a body being available to the decoded bytes
being ready, single thread, best of 7 to 9 runs (clean run, 2026-11-17). Today = zlib-ng 2.2.5 as the site calls it;
decided = the zlib-ng single call into a per-thread scratch, 1 MiB kept (D1 option 1, PR #547); libdeflate = the
bounded design of section 7 (512 KiB kept). The numbers are hot-loop timings on one idle thread; only the ratios carry
over to the viewer, where other threads compete for caches and memory bandwidth.

| Site (thread) | Body | Today | Decided (zlib-ng single call) | libdeflate (bounded) | Label |
|---|---|---|---|---|---|
| Mesh LOD/skin/physics block (pool or repo thread) | median of corpus: 1.2 KB in, 1.9 KB out | 4.1 µs | 4.0 µs | 2.9 µs | [measured] |
| | p99: 438 KB in, 767 KB out | 1.22 ms | 1.20 ms | 1.15 ms | [measured] |
| | largest in corpus: 3.66 MB (3,573 KiB) in, 5.64 MB (5,505 KiB) out | 10.3 ms | 8.1 ms | 6.9 ms | [measured] |
| | synthetic 4 / 8 / 16 MiB out (real mesh payload, zlib level 6) | 7.6 / 18.5 / 50.7 ms | 6.3 / 12.4 / 24.1 ms | 5.6 / 11.0 / 21.4 ms | [measured] |
| LLSD skin/convex, whole `unzip_llsd` incl. parse (pool or repo) | 7,727 blocks | 295 to 298 ms in all (38.3 µs per block) | not timed with the parse | 285 to 287 ms (37.0 µs) | [measured] |
| HTTP reply with Content-Encoding gzip (llcorehttp worker thread) | synthetic 4 / 8 / 16 MiB | 6.1 / 12.0 / 23.5 ms | unchanged (stays) | 5.8 / 11.4 / 22.2 ms (whole-body copy + gzip with the ISIZE size) | [measured] |
| Inventory cache `*.inv.llsd.gz` at login (main thread, login screen) | 7.7 MB in, 79 MB out | 19.3 ms decode, 42.6 ms with the file write | unchanged (stays) | 20.5 ms, 57.4 ms | [measured]; **not switched** |
| | 0.52 MB in, 5.04 MB out | 1.4 / 2.7 ms | unchanged | 1.4 / 2.4 ms | [measured] |
| | 0.31 MB in, 3.42 MB out | 0.8 / 1.8 ms | unchanged | 0.8 / **3.3 ms** (slower with the write) | [measured]; a regression, which supports staying on zlib-ng |
| Milky Way map (main thread, once at the first sky draw) | about 2.2 MB out, size known | about 3 ms | unchanged (already one call) | about 3 ms (−5 %) | [inferred]: it already inflates in one `Z_FINISH` call into an exact buffer (`llnightsky.cpp:53-83`) |
| Legacy materials responses (main thread, coroutine) | small LLSD, size not sampled | µs | µs | µs | [inferred]; no corpus |
| Navmesh (main thread, coroutine; pathfinding floater only) | gzip, size not sampled | ms at most | as today | lower | [inferred]; no corpus |

The mesh path cannot gain or lose latency through buffering: `LLMeshHandlerBase::onCompleted` runs only when the transfer is
complete, copies the whole body (`llmeshrepository.cpp:3755-3758`) and then calls `processData` (3768), which posts the
inflate to the pool (4014, 4132). Disk-cache reads also read the whole block first (2161, 1636). [code]

## 3. Where inflate runs today [code]

| Caller | File:line | Thread | Notes |
|---|---|---|---|
| `LLUZipHelper::unzip_llsd` (the shared whole-buffer inflate) | `llcommon/llsdserialize.cpp:2249-2352` | the caller's | zlib-ng `inflate(Z_NO_FLUSH)` into a `thread_local` 512 KiB chunk, `realloc` + `memcpy` per chunk, then `fromBinary` |
| LOD from HTTP | `llmeshrepository.cpp:3952` → pool lambda 4014-4026 → `lodReceived` 2481 → `LLVolume::unpackVolumeFaces` `llvolume.cpp:2327` | `MeshLodProcessing` pool (2 threads, `llmeshrepository.cpp:984`); repo thread if the post fails (4037) | |
| LOD from disk cache | `fetchMeshLOD` 2105 → pool lambda 2175 / repo fallback 2239 | pool; repo | `fetchMeshLOD` is called only from `run()` (1146) |
| LOD or skin inside the header reply | `headerReceived` 2304 → 2416, 2464 | repo thread | header handler 3824 via `onCompleted` |
| Skin from HTTP / cache | 4132 → `processSkin` 4073; 1648 / 1706 | pool; repo | |
| Decomposition, physics shape | 1823, 1929, 4192, 4265 → 2604, 2651 | repo thread | |
| `onCompleted` of every mesh handler | 3666, dispatched by `mHttpRequest->update()` | repo thread (`LLMeshRepoThread::run`, 1073) | |
| Main thread mesh entry points | `notifyLoadedMeshes` 4860, 4867 → `loadMeshLOD` 1362-1404, `loadMeshSkinInfo` 1331 | main | **queue only, no inflate** |
| Legacy materials | `llmaterialmgr.cpp:434, 475, 542` | main (coroutine responses, 871-882) | |
| Navmesh | `llpathfindingnavmesh.cpp:159` → `unzip_llsdNavMesh` `llsdserialize.cpp:2356` | main (coroutine) | gzip; has a double free (section 9) |
| Milky Way map | `lldrawpoolwlsky.cpp:115` → `LLNightSky::parseMilkyWay` `llnightsky.cpp:228` → `inflate_exactly` 53 | main (render), once | output size known |
| Inventory cache | `llstartup.cpp:2700, 2711` → `loadSkeleton` `llinventorymodel.cpp:3044` → `gunzip_file` `llsys.cpp:1368` | main, login | gzip stream file to file |
| `.slm` upload preview | `llmodel.cpp:1600, 1621`; `llvolume.cpp:2318` | model loader | upload only |
| Backup import | `fsfloaterimport.cpp:243` | main | user action |
| HTTP Content-Encoding | `llcorehttp/_httplibcurl.cpp:132-213`, from `completeRequest` 697-712 | llcorehttp worker | streaming loop over a complete body |
| EXR images | `llreflectionmapmanager.cpp:49` (tinyexr, `uncompress`) | caller's | third-party header, size known |

`zlib.h` is included but unused in `llviewerobjectlist.cpp:76-80` and `llprimitive/llmodel.cpp:37-41` (no `z_`/`Z_`
symbol in either file).

## 4. Correctness [measured]

Corpus: the owner's `%LOCALAPPDATA%\Vulkanstorm_x64\cache` copied read-only (`copy_mesh_corpus.py`) into the repo's
`.fixture-tmp`: 13,608 files with the mesh cache preamble. The harness parses each header with the viewer's own
`LLSDSerialize::fromBinary`, takes `mHeaderSize` from `tellg()` as `headerReceived` does (2349), and keeps every block the
preamble flags mark as cached (`LLMeshHeader::getFlags`, `llmeshrepository.h:375`; skin, convex and physics_mesh are bits
5, 6 and 7 because `LLModel::NUM_LODS` is 5). No block failed the header parse; 23,497 blocks are listed but not cached.

| Check | Result |
|---|---|
| zlib-ng (viewer loop) vs libdeflate, bytes | 53,915 / 53,915 identical; 0 accepted by one only |
| zlib-ng single-call scratch (the decided D1 option 1, 1 MiB kept) vs viewer loop | 53,915 / 53,915 identical |
| libdeflate bounded ("sized", 512 KiB kept) vs viewer loop | 53,915 / 53,915 identical |
| Retries of the retaining libdeflate scratch during `verify` | 2 over 161,745 libdeflate calls (one per cold growth) |
| Adler-32: one bit of the trailer flipped | refused by both on 53,915 / 53,915 |
| Geometry, LOD + physics_mesh (viewer `unpackVolumeFaces` vs bounded libdeflate + the same `fromBinary(…, 96)` + `unpackVolumeFacesInternal`; also with the retaining scratch on 2026-11-16) | 46,474 / 46,474 bit-identical after `cacheOptimize(true)` |
| Determinism control (viewer path against itself) | 500 / 500 identical |
| LLSD, skin + physics_convex (`toBinary` of both trees) | 7,441 / 7,441 identical |
| Inflate ratio (out / in) | median 1.81, p99 7.45, max 11.82: mesh payloads are quantised `U16` arrays and barely compress |
| Inventory caches (gzip) | 3 / 3 identical |

Fuzz (`bench_plain_asan.exe fuzz`, seed 20261116, 16 mutations per block, all 53,915 blocks; libdeflate and the
harness built with `/fsanitize=address`; the zlib-ng package library is not instrumented):

| Class | Cases | Both refuse | Both accept, same output | Accept, different output | Only zlib-ng | Only libdeflate |
|---|---|---|---|---|---|---|
| truncated at a random length | 107,830 | 107,830 | 0 | 0 | 0 | 0 |
| truncated by 1 to 4 bytes | 107,830 | 107,830 | 0 | 0 | 0 | 0 |
| 1 bit flip | 107,830 | 107,668 | 160 | 0 | 0 | 2 |
| 8 bit flips | 107,830 | 107,830 | 0 | 0 | 0 | 0 |
| trailer (Adler-32) bit flip | 107,830 | 107,830 | 0 | 0 | 0 | 0 |
| header CINFO / FDICT edit (FCHECK kept valid) | 107,830 | 80,649 | 27,181 | 0 | 0 | 0 |
| 1 to 64 trailing random bytes | 107,830 | 0 | 107,830 | 0 | 0 | 0 |
| a run of 1 to 32 bytes zeroed | 107,830 | 107,807 | 23 | 0 | 0 | 0 |

In the "both accept, same output" cases (flips in bits neither decoder reads, such as padding, and header window sizes
both tolerate) the two outputs are equal and pass Adler-32. The two "only libdeflate" cases were scanned exhaustively
(`flipscan`): blocks 29429 (skin, 339 bytes) and 46667 (skin, 302 bytes) have 2 such bits each out of 2,712 and 2,416;
zlib-ng returns `Z_DATA_ERROR`, libdeflate returns exactly the original data (compared with the unmodified block's
output). With libdeflate the viewer would accept these damaged blocks and get the right skin. Refusal codes differ: a truncated stream is `ZR_BUFFER_ERROR` today (`Z_BUF_ERROR`) and would be
`ZR_DATA_ERROR` with libdeflate; every caller compares only against `ZR_OK` and logs the code.

## 5. Speed [measured]

Both built like the viewer: MSVC Release `/O2 /arch:AVX2 /MD`; zlib-ng is the viewer's own package library
(`packages/lib/release/zlib.lib`, 3p-zlib-ng 2.2.5, with its runtime-dispatched `inflate_fast_avx2`, `adler32_avx2`,
`chunkmemset_safe_avx2`); libdeflate 1.26 from the release tarball (SHA-256 `125856d4…a42b5a`, equal to GitHub's digest),
all library sources compiled with the same flags. The viewer-linked harness uses the Release `llcommon.lib`,
`llmath.lib`, `llmeshoptimizer.lib` (LTO, `/LTCG`) and the viewer's Release compile definitions.

Strategies (the uncompressed size of a block is unknown; the mesh header only gives compressed sizes, `llmeshrepository.h:341-356`):

- **zlib-ng viewer loop**: `unzip_llsd`'s loop, verbatim (512 KiB chunk, `realloc` + `memcpy` per chunk).
- **libdeflate exact size**: the output size known in advance (an oracle; the Milky Way map's case).
- **libdeflate ratio ×4 / ×8, grow ×2**: `malloc(in × k)`, doubled on `LIBDEFLATE_INSUFFICIENT_SPACE`, retried from the start.
- **libdeflate thread scratch** (fastest, retaining): a per-thread buffer, 512 KiB to start (today's chunk size), grown on
  `LIBDEFLATE_INSUFFICIENT_SPACE` to the next power of two of max(2 × capacity, 4 × input), released after a call that
  needed more than 16 MiB. The parser reads straight from it: no allocation and no copy per call.
- **zlib-ng thread scratch (rival)**: zlib-ng kept, the loop fixed: one `inflate(Z_FINISH)` into its own per-thread
  scratch, grown by doubling and continued where it stopped.
- **libdeflate sized (bounded)**: a fixed 512 KiB per-thread scratch for inputs of at most 128 KiB; any larger input,
  or a small one that overflows, gets a one-off buffer of the next power of two of 4 × input, doubled on overflow and
  freed after the parse. At most 512 KiB is kept per thread, the same as today's chunk.
- The zlib-ng rival keeps at most 1 MiB per thread (larger buffers are released after use), as the decided design.

Whole corpus (53,915 blocks, 1,817 MB out), one thread, strategies interleaved, 7 repetitions, clean run of
2026-11-17 (no build ran; the earlier runs agree within 2 %):

| Strategy | Memory kept per thread | Per block median | p99 | max | Corpus (min / median) | MB/s out | Retries |
|---|---|---|---|---|---|---|---|
| zlib-ng viewer loop (today) | 512 KiB chunk | 2.5 µs | 1,122 µs | 10.3 ms | 2,777 / 2,784 ms | 654 | — |
| libdeflate exact size (oracle) | — | 2.1 µs | 1,050 µs | 6.5 ms | 2,449 / 2,452 ms | 742 | — |
| libdeflate ratio ×4, grow ×2 | none | 2.1 µs | 1,119 µs | 6.9 ms | 2,576 / 2,580 ms | 705 | 41,076 / 377,405 calls |
| libdeflate ratio ×8, grow ×2 | none | 2.2 µs | 1,127 µs | 6.9 ms | 2,597 / 2,602 ms | 700 | 938 / 377,405 |
| libdeflate thread scratch (retaining) | up to 16 MiB | 2.0 µs | 1,053 µs | 6.5 ms | 2,442 / 2,448 ms | 744 | 0 once warm |
| **zlib-ng single call (rival; decided)** | **up to 1 MiB** | **2.4 µs** | **1,147 µs** | **8.0 ms** | **2,705 / 2,710 ms** | **672** | — |
| libdeflate sized (bounded) | 512 KiB | 2.1 µs | 1,124 µs | 6.9 ms | 2,581 / 2,588 ms | 704 | 0 in the timed passes |

Threads (the mesh pool is 2, plus the repo thread), corpus wall time, minimum (median) of 7:

| Threads | zlib-ng viewer loop | zlib-ng single call (decided) | libdeflate bounded | libdeflate retaining |
|---|---|---|---|---|
| 1 | 2,797 ms | 2,731 ms (−2.4 %) | 2,625 ms (−6.1 %) | 2,445 ms (−12.6 %) |
| 2 | 1,446 ms | 1,409 ms (−2.6 %) | 1,344 ms (−7.1 %) | 1,274 ms (−11.9 %) |
| 3 | 945 ms | 916 ms (−3.1 %) | 880 ms (−6.9 %) | 833 ms (−11.9 %) |

Scaling is linear for all four (2.9 to 3.0 times at 3 threads): no shared lock or allocator contention shows at the
mesh path's thread count.

Memory per call: zlib-ng allocates 42,112 bytes of inflate state per `inflateInit` (hooked `zalloc`), plus today's
512 KiB `thread_local` chunk and a `realloc`ed result of the output size. libdeflate: one 11,568-byte decompressor per
thread, plus its output buffer.

**Retention matters for libdeflate, not for zlib-ng.** libdeflate cannot resume: on `INSUFFICIENT_SPACE` it starts
again from the first byte. A scratch that grows and is released after use (keep limit 1 MiB) made every block above
it retry on every call: **3,161 ms**, slower than today (5,866 retries in 7 passes; a section flagged DISTURBED, so
indicative only). The two ways out are measured above: keep up to 16 MiB per thread (2,442 ms; 64 MiB over 4 threads),
or size the buffer from the input up front (the bounded variant, 2,581 ms with 512 KiB kept). zlib-ng continues where
it stopped, so a 1 MiB limit costs the decided single call little (2,705 ms; 2,658 ms with 16 MiB kept on 2026-11-16).

The per-block microseconds in this section are hot-loop numbers (corpus in memory, nothing else on the thread); only
the ratios carry over to the viewer.

Whole LOD unpack (the work `lodReceived` does), one thread, 46,188 LOD blocks, today's path and the bounded libdeflate
path, **order alternated per block** (clean run, 2026-11-17):

| | Today (zlib-ng, `unpackVolumeFaces`) | libdeflate bounded | Change |
|---|---|---|---|
| Inflate alone, 46,188 LOD blocks | 2,745 to 2,756 ms | 2,573 to 2,586 ms | −6.2 % |
| Whole unpack (inflate + `fromBinary` + unpack + `cacheOptimize(true)`) | 34,442 to 34,622 ms (0.75 ms per block) | 34,222 to 34,373 ms | **−0.7 %** |
| Inflate share of the whole unpack | 8.0 % | 7.5 % | |
| `unzip_llsd` with parse, 7,727 skin + physics blocks | 295 to 298 ms | 285 to 287 ms | −3.4 % |

Three repetitions; the spread is the range of the three. The first run (2026-11-16, fixed order with libdeflate always
second, and the 16 MiB retaining scratch) gave −13 % and −2.0 %; both the fixed order and the retained memory
favoured libdeflate. The decided zlib-ng single call was not timed inside the whole unpack; from its inflate-only
gain (2.6 %) and the 8 % share it is about −0.2 % [inferred].

## 6. What this means in the viewer

- **Main-thread ms per frame from mesh inflate: 0 today, 0 with libdeflate [code].** Section 3: no mesh inflate site is
  reachable from the main thread; `notifyLoadedMeshes` only pushes requests (4860, 4867).
- **Worker CPU at Rooftops scale [inferred].** The Rooftops settle ran 38,123 mesh fetches in 112.8 s
  (`doc/perf/multicore-baseline-2026-11.md` 4.8). The corpus average is 51.5 µs per block with zlib-ng today, 50.2 µs
  with the decided single call, 47.9 µs with bounded libdeflate and 45.3 µs with retaining libdeflate: at most
  38,123 × 1.3 µs = **0.05 s** (decided) or 38,123 × 3.6 to 6.2 µs = **0.14 to 0.24 s** (libdeflate) **of worker CPU
  saved over the whole settle**, spread over the repo thread
  and two pool threads. This overstates it: header fetches inflate nothing unless the 4 KiB reply carries a LOD or skin.
- **Settle time [inferred].** The whole LOD unpack costs 0.75 ms per LOD block (section 5), so 38,123 fetches are at
  most about 29 s of worker CPU, against 2 × 112.8 s of pool time: the pool is not saturated on average, and the settle
  follows the fetches (network and request scheduling). Expected change in settle time: **none measurable** (≤ 0.3 s
  even if the workers were the critical path throughout).
- **Hitches.** None caused or removed: the inflate never runs on the frame.

## 7. Integration sketch (not implemented)

**One shared helper**, new `indra/llcommon/llinflate.{h,cpp}` (`SPDX-License-Identifier: GPL-3.0-or-later`):

```cpp
namespace LLInflate
{
    enum class Result { OK, DATA_ERROR, SIZE_ERROR, MEM_ERROR };
    // zlib or gzip wrapper, whole buffer in, view of the output out. The view points into a
    // per-thread scratch and stays valid until the next call on the same thread.
    Result zlib(const U8* in, size_t in_size, const U8*& out, size_t& out_size);
    Result gzip(const U8* in, size_t in_size, const U8*& out, size_t& out_size); // ISIZE trailer as the first size
    // known output size (Milky Way map)
    Result zlibExact(const U8* in, size_t in_size, U8* out, size_t out_size);
}
```

- `thread_local` libdeflate decompressor and output buffer. Two memory policies, measured in section 5: the retaining
  scratch (grow to the next power of two of max(2 × capacity, 4 × input), keep up to 16 MiB per thread: the fastest)
  or the bounded "sized" policy (512 KiB kept, a one-off buffer for inputs above 128 KiB: 2,581 ms). A per-call
  output cap (D2) returns `SIZE_ERROR`. No shared state: each thread owns its two objects; they are freed by the thread's `thread_local`
  destructors (the pool is joined at `LLMeshRepoThread` shutdown, `llmeshrepository.cpp:1327`). No `shutdown()` path
  gains a free: the AGENTS.md caller check finds main, repo, the two pool threads, the model loader and the
  llcorehttp worker (not converted) as the threads that run these functions, none with a shared GL context in this path.
- `LLUZipHelper::unzip_llsd(LLSD&, const U8*, S32)` (`llsdserialize.cpp:2249`): the z_stream loop becomes one
  `LLInflate::zlib` call; `strip_deprecated_header` and `fromBinary(…, UNZIP_LLSD_MAX_DEPTH)` read the view. All callers
  stay as they are (`llvolume.cpp`, `llmodel.cpp`, `llmeshrepository.cpp`, `llmaterialmgr.cpp`, `fsfloaterimport.cpp`).
- `unzip_llsdNavMesh` (2356): `LLInflate::gzip`, then a `malloc`ed copy for the caller, which `free`s it. This also
  removes the double free (section 9); the hotfix goes first, on every line.
- `llnightsky.cpp:53` `inflate_exactly`: `LLInflate::zlibExact`.
- **Stays on zlib-ng:** `zip_llsd` (compression), `llcorehttp` reply decoding, `gunzip_file` / `gzip_file`, tinyexr,
  curl, minizip-ng, libpng and every other package that links it (section 10).

**Packaging.** An autobuild package `3p-libdeflate` in the owner's GitHub organisation, like `3p-curl` and `3p-openssl`:
CMake static library, `LIBDEFLATE_BUILD_SHARED_LIB=OFF`, `LIBDEFLATE_BUILD_GZIP=OFF`,
`LIBDEFLATE_COMPRESSION_SUPPORT=OFF` (decompression only), windows64 (MSVC) and linux64 (ubuntu-26.04) with the
linux-lts override (ubuntu:24.04) as curl has; `indra/cmake/LIBDEFLATE.cmake` with `use_prebuilt_binary(libdeflate)` and
`ll::libdeflate` linked to `llcommon`; the `USE_CONAN` branch can use Conan's `libdeflate`. libdeflate dispatches its
x86 code paths at run time (`lib/x86/cpu_features.c`), so `/arch` does not matter for it. **FreeBSD:** `autobuild.xml`
has no FreeBSD platform (0 matches); libdeflate supports it upstream. Vendoring the ~20 MIT source files under
`indra/` is the alternative; it breaks the repo's Autobuild convention for third-party code (AGENTS.md).

**Licence.** MIT, GPL-3.0-compatible. Row for `doc/licensing.md`:
`| libdeflate 1.26 | MIT | OK | 3p-libdeflate; whole-buffer zlib/gzip decompression only; zlib-ng stays for streams and compression |`.

**Tests.** A `llcommon` integration test with a few small committed zlib/gzip blobs (made by the test, not owner data):
round trip, truncation, Adler flip, gzip ISIZE, the output cap; the bench stays as the corpus-level check.

## 8. Decisions for the owner

**Decided by the owner:** D1 **option 1** (zlib-ng single call into a bounded per-thread scratch; implemented as the
draft PR #547, which depends on this study) and D2 **as recommended** (per-call caps; hotfix 87379e897f on release,
in review). libdeflate is not adopted. The options below are kept as the record of what was weighed.

**D1. Change the whole-buffer inflate?** Both alternatives save little, on worker threads only: the zlib-ng fix
saves about 0.2 % of the LOD unpack [inferred: 2.6 % of an 8 % share], bounded libdeflate 0.7 % [measured]. Neither changes fps or the
settle time. Why the zlib-ng fix helps at all: today every block is inflated into the 512 KiB chunk and then copied
into a `realloc`ed result; 99 % of blocks are below 512 KiB, so the cost is that extra copy and allocation per block,
not the superlinear growth of B3, which only matters above about 16 MiB (no corpus block comes close: the largest is
5.64 MB). The fix inflates once, in place.

0. **Change nothing except the B1/B2 hotfix** (in progress). Honest baseline: the speed gains are about 0.2 % and 0.7 % of
   worker-thread work.
1. **Hygiene with zlib-ng (recommended):** one `inflate(Z_FINISH)` call into a per-thread scratch with bounded memory
   (512 KiB kept, larger buffers released after use; `avail_out` capped at `UINT_MAX`), plus the output cap of D2.
   2,705 ms instead of 2,777 ms on the corpus with 1 MiB kept, and the 16 MiB synthetic body from 50.7 ms to 24.1 ms. No new package, no licence row, no change on corrupt data.
   About 0.25 d with review. Recommended over 0 for the bounded memory and the cap, not for speed.
2. **Adopt libdeflate as designed in section 7.** 2,581 ms with memory bounded like option 1 (4.6 % below it, 28 % less
   per median block), 2,442 ms if it may keep 16 MiB per thread. Identical results. Costs a new
   package on two platforms plus linux-lts, a licence entry and a dependency to keep current, for no measurable
   settle-time or fps change. About 1 to 1.5 d.
3. **Adopt and also convert the streams.** Not recommended: section 10.

**D2. Output cap (with D1 = 1 or 2; F3 belongs here).** Today `unzip_llsd` has none: a bomb grows by `realloc` until
it fails (`llsdserialize.cpp:2307`), and `unzip_llsdNavMesh` additionally wraps its `U32` size past 4 GiB (F3). Deflate
reaches about 1,000:1, so a 1 MB block can ask for 1 GB.

- **Legitimate worst case of a mesh block [inferred]:** a LOD has at most 8 faces of at most 65,535 vertices (`U16`
  indices; the unpack rejects more, `llvolume.cpp:2411`). Per vertex: position 6 B, normal 6 B, UV 4 B, weights up to
  13 B (16 to 29 B); indices about 2 triangles per vertex, 12 B. That is 8 × 65,535 × about 28 to 41 B, **about 15 to
  22 MB** with the LLSD framing. The corpus's largest block is 5.64 MB.
- **Not every caller is a mesh block.** The backup import manifest (`fsfloaterimport.cpp:243`) embeds whole assets
  (textures, sounds, meshes: `mManifest["asset"][id]["data"]`, 365-371, 1188), so a large linkset backup can inflate
  to hundreds of MB legitimately. Legacy materials responses are small LLSD. The navmesh size is not sampled.
- **So the cap is per call**, a parameter of the shared helper: **64 MiB for mesh blocks** (about 3 times the
  derived worst case; LOD, skin, decomposition, physics, materials), **256 MiB for the navmesh** (no sample; generous),
  and for the import either none or 1,032 × the file size (the deflate maximum ratio, so it never refuses valid data).
- The bench's own `LD_CAP` is 256 MiB for every call; no corpus block came within a factor of 45 of it. It is a bench
  guard, not the proposal.

**D3. The navmesh double free** is a bug, not a decision: hotfix on every line, independent of D1.

**Effort, calibrated on this repo's history.** The curl 8 move (packages for windows64, linux64 and linux-lts, the
decoding rework, 2026-11-05 to 11-06, about a dozen commits) took about 2 days and was larger. A decompression-only
`3p-libdeflate` package: 0.5 d; the helper, three call sites and the test: 0.5 d; review pipeline and checker:
0.25 to 0.5 d; plus the owner's in-world check of a Release build (mesh loading is on the rendering path).

## 9. Bugs and cruft found in the inflate paths

| # | Where | What | Label |
|---|---|---|---|
| B1 | `llsdserialize.cpp:2401-2444` `unzip_llsdNavMesh` | On `Z_DATA_ERROR`, `Z_NEED_DICT` or `Z_MEM_ERROR` the switch frees `result` and `delete[]`s `in` (2408-2411), then `break`s out of the **switch only**. On the first chunk this ends in a second `delete[] in` (2440) and a second `free(result)` (2444). **On an error after one or more chunks it is a heap use after free with attacker-chosen bytes:** `realloc` of the freed `result` (2417), `memcpy` of the decoded chunk, bytes the region chose, into that freed memory (2434), then the frees again (2440, 2444). The four lines are byte-identical on every line (release `unzip_llsdNavMesh` at 2356, canary 2352, rust 1242). Reached only through the pathfinding navmesh view (`requestGetNavMeshForRegion(region, false)`, `llpathfindingnavmeshzone.cpp:332`); the input is controlled by the region. A truncated body is safe (`Z_BUF_ERROR` ends the loop without the switch). | **confirmed**. `navmesh_uaf.cpp` (scratchpad `navmesh\`) is a verbatim copy of the function covering the first-chunk case only; ASan reports `attempting double-free` (alloc 2370, frees 2410 and 2440); it needs the ASan runtime folder of MSVC on `PATH` to run. The auditor's cases (scratchpad `audit\`: `t.cpp`, `real_fn.inc`, `b.cmd`) compile the real function: mode 0 an error on the first chunk, mode 1 a CRC-32 flip (the error comes after many 16 KiB chunks), mode 2 a flip in the middle of the data. **Hotfix in progress** (commit 43e312b40f on release, under review) |
| B2 | `llsdserialize.cpp:2387, 2393-2399` | The `Z_STREAM_ERROR` branch frees the same two buffers and does not return either: same double free. Reached only if `inflateInit2` fails (out of memory), because its result is not checked (2387). | confirmed by reading; OOM only. Covered by the B1 hotfix |
| B3 | `llsdserialize.cpp:2305-2321` `unzip_llsd` | `realloc` + `memcpy` per 512 KiB chunk: superlinear for large bodies (16 MiB: 50.7 ms vs 24.1 ms with one growing buffer); it only matters above about 16 MiB, and the corpus has no such block (largest 5.64 MB). For ordinary blocks the cost is the extra copy and allocation per block. | measured; inefficiency |
| B4 | `llsdserialize.cpp:2257-2261` | The `thread_local` chunk is allocated `nothrow` and not checked. **Refuted as a bug** by the auditor: zlib-ng returns `Z_STREAM_ERROR` for a null `next_out` and writes nothing, so an OOM here fails cleanly (`ZR_BUFFER_ERROR`). | cruft only |
| B5 | `llsdserialize.cpp:2269, 2321` | `inflateInit`'s result is overwritten unchecked; `while (ret == Z_OK && ret != Z_STREAM_END)` has a redundant second test; commented-out `// free(result);` lines. | cruft |
| B6 | `llsdserialize.cpp:2237-2246` | `unzip_llsd(std::istream&, …)` ignores a short `is.read` and inflates uninitialised bytes (Adler-32 then fails). Callers: `.slm` upload, backup import. | plausible, low |
| B7 | `llviewerobjectlist.cpp:76-80`, `llprimitive/llmodel.cpp:37-41` | `zlib.h` included, nothing used. | cruft (grep: no `z_`/`Z_` symbol in either file) |
| B8 | `llpathfindingnavmesh.cpp:155-159` with `llsdserialize.cpp:2370-2376` | The navmesh body is copied three times before it inflates (LLSD binary to `std::string`, to `std::istringstream`, to `new U8[size]`) and inflated in 16 KiB chunks with a `realloc` per chunk. | cruft / inefficiency, main thread, pathfinding only |
| F3 | `llsdserialize.cpp:2365, 2417` `unzip_llsdNavMesh` | `cur_size` is `U32` and there is no output cap: past 4 GiB `cur_size + have` wraps, a `realloc(p, 0)` frees the block and the next free is a double free; long before that, a gzip bomb (about 1,000:1) keeps the **main thread** inflating and growing a buffer until memory runs out (a frozen viewer). Folded into D2. | confirmed by reading |

## 10. Streams: buffer, then burst-decode?

libdeflate has no streaming API. A single deflate stream **cannot be split into independently decodable chunks**:
back-references reach up to 32 KiB into earlier output and block boundaries fall anywhere, so decoding has to start at
the beginning. Only a compressor that writes independent frames (full flushes with a reset dictionary, or separate
streams) makes chunk-parallel decoding possible, which applies to our own cache formats only, not to server data.

| Site | Body complete and bounded? | Sizes | Partial output consumed? | Gain of buffer + burst | Verdict |
|---|---|---|---|---|---|
| llcorehttp Content-Encoding (`_httplibcurl.cpp:132-213`, called at `completeRequest` 697-712; llcorehttp worker thread) | **Already buffered**: curl's own decoding is off (`CURLOPT_HTTP_CONTENT_DECODING 0`, `_httpoprequest.cpp:637-640`) and the reply is decoded after the transfer, from the whole `BufferArray` | LLSD replies; textures and mesh come without a coding (the CDN's `binary/octet-stream` passes through) | no | synthetic 4 / 8 / 16 MiB: 6.2 / 12.3 / 23.8 ms today, 5.9 / 11.5 / 22.4 ms buffered + libdeflate [measured], with the flatten copy of the chunked `BufferArray` included; a few % | keep zlib-ng: the loop also decodes gzip members after the first, keeps what decoded of a truncated reply and falls back to raw deflate (comment 70-95); libdeflate would need all three rebuilt |
| curl inside (other users) | curl links zlib-ng; the viewer's handles do not let curl decode | — | — | — | out of scope |
| Inventory cache `gunzip_file` (`llsys.cpp:1368`; main thread at login) | yes, a file | 0.3 to 7.7 MB in, 3.4 to 79 MB out | no (written to a file, then parsed) | **none or negative** [measured]: 19.3 vs 20.5 ms decode, 42.6 vs 57.4 ms with the file write (79 MB): faulting in a fresh 79 MB buffer costs more than libdeflate saves; gzread works in a hot 32 KiB buffer | keep zlib-ng |
| Navmesh `unzip_llsdNavMesh` | already whole-buffer (the LLSD binary field) | not sampled | no | as for mesh blocks | whole-buffer site: covered by section 7 |
| Mesh blocks, LLSD blobs | already whole-buffer | section 5 | no | section 5 | section 7 |

## 11. What was not verified

- Linux and the linux-lts toolchain: only MSVC on Windows was measured. libdeflate and zlib-ng both dispatch at run
  time, so the ratio should carry over [inferred].
- Legacy materials responses, navmesh bodies and real gzip HTTP replies: no samples on disk; their rows are inferred or
  synthetic.
- The settle-time effect is inferred from the Rooftops log, not measured with a viewer: no viewer was built or run.
- The zlib-ng package library is not ASan-instrumented (prebuilt); only libdeflate and the harness are.
- The fuzz pass ran with the first scratch policy (8 MiB keep limit, growth to 8 × input); the tuned policy changes only
  buffer sizes. The final `verify` pass (all checks of section 4) ran with the tuned policy.
- Real-world cold-thread behaviour: one retry per thread the first time a block outgrows 512 KiB (measured: 2 retries
  in 107,830 calls during `verify`); not measured inside a running viewer.
- **The corpus is one producer's output.** It is the owner's Second Life cache, and the cache only stores blocks that
  decoded (for example `processSkin` writes the cache only after `skinInfoReceived` succeeded,
  `llmeshrepository.cpp:4073-4111`), so it holds no decode failures and no blocks from OpenSim or third-party
  compressors (stored blocks, fixed Huffman codes, other window sizes). The fuzz pass covers damaged input; other
  producers' valid streams are not sampled.
- **Per-block microseconds are hot-loop numbers** (corpus in memory, decoder and caches warm, nothing else on the
  thread). Only the ratios carry over to the viewer. Allocator and memory-bandwidth contention with the viewer's other
  threads were not measured.
- **The geometry check corroborates; it does not add a separate proof.** The unpack is a pure function of the
  inflated bytes, so byte identity already implies identical geometry. The check shows that the libdeflate twin
  (header strip, parser call, private unpack) reproduces the viewer's path exactly.
- Logs: `verify.txt` (first scratch policy) and `verify3.txt` (16 MiB retaining scratch) are superseded by
  `verify5.txt` (2026-11-17: bounded libdeflate as the geometry twin, plus the byte checks of the decided zlib-ng
  single call and of bounded libdeflate); all three agree on every correctness count.
- **A harness bug found and fixed on the way:** with the 1 MiB keep limit, the zlib-ng single-call scratch released
  its buffer inside the call, so the returned view dangled and the first `verify` run crashed reading freed memory
  (exit 139; no result was produced or used). The release now happens after the caller is done with the output. The
  speed passes never read the output, so their timings are not affected. **The same ordering applies to the decided
  implementation (PR #547): release or shrink the per-thread scratch only after `fromBinary` has finished reading it.**
- `copy_mesh_corpus.py` flattens the cache's sub-folders by file name and skips a destination that already exists with
  the same size without comparing bytes. Cache file names carry the full asset UUID, so a collision is not expected,
  but the copier does not detect one.
- `Windows reports 8 cores`: the 9950X3D has 16; the session may be limited by affinity or a VM layer. Thread scaling
  was measured up to 3 threads only, the count the mesh path uses.

## 12. Reproducing

```
python -I scripts/bench/libdeflate/fetch_libdeflate.py --out .fixture-tmp/libdeflate-bench/src
python -I scripts/bench/libdeflate/copy_mesh_corpus.py --cache "%LOCALAPPDATA%/Vulkanstorm_x64/cache" --out .fixture-tmp/libdeflate-bench/corpus
scripts\bench\libdeflate\build_bench.cmd build-vc180-64 .fixture-tmp\libdeflate-bench\src\libdeflate-1.26 .fixture-tmp\libdeflate-bench\build
set TEMP=%CD%\.fixture-tmp& set TMP=%CD%\.fixture-tmp
cd .fixture-tmp\libdeflate-bench
build\bench_viewer.exe pack corpus blocks.pack
build\bench_viewer.exe verify blocks.pack
build\bench_plain_asan.exe fuzz blocks.pack 20261116 16 1
build\bench_plain.exe flipscan blocks.pack 29429
build\bench_plain.exe speed blocks.pack 3 7
build\bench_plain.exe synthetic blocks.pack 9
build\bench_viewer.exe pipeline blocks.pack 3
build\bench_plain.exe gzfile <file.inv.llsd.gz>... <scratch folder>
```

`build_bench.cmd` needs a Release viewer build directory (for `llcommon.lib`, `llmath.lib`, `llmeshoptimizer.lib` and
the packages); it copies those libraries into `<out>\vlibs` first. Nothing from the corpus or libdeflate is committed.
