<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# neo-j2k as the J2C codec (`USE_NEOJ2K`)

[neo-j2k](https://github.com/anne-skydancer/neo-j2k) (BSD-2-Clause) is a JPEG 2000 Part 1 codec written for
texture-sized images. **The viewer uses it by default**; OpenJPEG stays available as the other option until
`llimagej2coj` is retired. The choice is made when CMake configures.

## The option

`USE_NEOJ2K` (default **ON** on Windows and Linux, OFF elsewhere: 3p-neoj2k has packages for those two only) picks the codec package:

| | `USE_NEOJ2K=ON` (default) | `USE_NEOJ2K=OFF` |
|---|---|---|
| Package | `neoj2k` ([3p-neoj2k](https://github.com/anne-skydancer/3p-neoj2k), release v0.1.0) | `openjpeg` ([3p-openjpeg](https://github.com/anne-skydancer/3p-openjpeg)) |
| Library | `neoj2k.dll` (delay-loaded, exports `nj2k_*` only), `libneoj2k.so` on Linux | `openjp2.dll` (delay-loaded) |
| Implementation | `indra/llimagej2cneo` | `indra/llimagej2coj` |
| Decode | neo-j2k | OpenJPEG |
| Encode (uploads, bakes, snapshots) | neo-j2k (byte-identical to OpenJPEG's for the viewer's settings) | OpenJPEG |

One codec per binary: the implementation is the one `fallbackCreateLLImageJ2CImpl()` (`llimage/llimagej2c.cpp`)
finds at link time. There is no runtime routing and no per-file fallback, and OpenJPEG is neither installed nor
linked when the option is on. `indra/cmake/NeoJ2K.cmake` carries the option; `USE_NEOJ2K` with `USE_KDU` is an error.

Where it is set, so that every way of configuring builds neo-j2k:

- `indra/cmake/NeoJ2K.cmake`: the CMake default is ON on Windows and Linux and OFF elsewhere; ON on another platform stops the configure.
- `autobuild.xml`: `-DUSE_NEOJ2K:BOOL=ON` in the `configure` options of every Windows and Linux configuration that lists CMake options (never in the `common` block, which macOS
  inherits) (the configurations `ReleaseFS`, `ReleaseFS_open`, `RelWithDebInfoFS` and the others go through
  `scripts/configure_firestorm.sh`, which passes the option: ON by default, `--no-neoj2k` for OpenJPEG, always OFF
  on macOS, which has no neo-j2k package). The two configurations that set `INSTALL_PROPRIETARY=TRUE` (Kakadu) are
  left alone: with `USE_KDU` on, `USE_NEOJ2K` is an error.
- The CI configure step passes it explicitly, ON or OFF, and a release build uses it as any other
  (`scripts/ci_release_config.py` checks it).
- `fs-build-variables` carries no codec option.

Local configure: `autobuild configure -A 64 -c ReleaseFS_open` builds neo-j2k; add `-- --no-neoj2k` (or
`-DUSE_NEOJ2K:BOOL=OFF`, which wins) for the OpenJPEG build.

CI builds both options on every pull request and qualification build: the `windows`, `linux` and `linux-lts` jobs are
neo-j2k, and the `<platform>-openjpeg` jobs of `build-windows.yaml`, `build-linux.yaml` and `build-linux-lts.yaml` pass
`-DUSE_NEOJ2K:BOOL=OFF`. The builds need the released package named in `autobuild.xml` and
`scripts/linux_lts_packages.json`: a placeholder entry (version `0.0.0.0`, zero hashes) stops them at once
(`scripts/ci_neoj2k_pin.py --require`, tested by `scripts/tests/test_neoj2k_pin.py`).

## What the viewer does with it (`llimagej2cneo.cpp`)

- **One decoder per thread**: a `thread_local` neo-j2k decoder on each thread that decodes (the ImageDecode pool,
  the main thread for local files and the bake integrity checks), created on first use and destroyed at thread exit.
  The decoder has the Second Life limits (`NJ2K_DECODER_LIMITS_SL_TEXTURE`) and non-strict mode, keeps 4 MiB of
  working memory between images, and always starts a request at reduce factor 0. Encoders are made per call.
- **One header parse**: `getMetadata` and the decode that follows it on the same bytes share the parse (neo-j2k
  keeps the header model while the main header is the same). `getMetadata` checks the SL limits at the full size
  (`nj2k_get_decoded_dimensions` at reduce 0), as OpenJPEG's header read did, and refuses a size over 65,535.
- **The samples go straight into the `LLImageRaw`** (8 bits, interleaved, bottom-up): no int32 planes, no copy loop.
- **The rules of `llimagej2coj` stay** and are shared as headers: the reduce factor rule (`llj2creduce.h`), the
  decode guard (`llj2cdecodeguard.h`), and, in `llj2ccommon.h`, the comment scan and the encoder's layer and
  resolution policy: both implementations include them. `llimagej2cneo.cpp` derives from `llimagej2coj.cpp` and keeps
  its Linden Lab (LGPL-2.1) header and the Phoenix Firestorm and Catznip attributions.
- **Departure: the size is the image area.** `getMetadata` gives `x1 - x0` by `y1 - y0` (the image area); OpenJPEG's
  wrapper gave the tile grid, `tiles across * tile width`, which is larger for a multi-tile codestream with a partial
  last tile or an image offset (neo-j2k plan finding F1: a 1000-wide image in 512-wide tiles read as 1024). The image
  area is the correct one. Second Life textures are one tile with no offset, so the two agree on every texture in the
  1,200-texture corpus (0 differences).
- **The texture cache key** (`getEngineInfo()`, compared with `LastJ2CVersion` in `LLAppViewer::initCache`, which
  schedules a texture purge on a mismatch) is, in the neo-j2k build, the string the OpenJPEG build gives:
  `OpenJPEG: 2.5.4, Runtime: 2.5.4` (`LL_J2C_CACHE_KEY` in `llj2ccommon.h`). The two codecs decode a texture to the same
  pixels (whole files and cut downloads: the A/B above), so **switching between the two builds never purges the user's
  texture cache**, and a neo-j2k release does not either. **This supersedes the earlier key `neo-j2k output 1`**, which
  purged the cache on every switch. Change the constant, in both implementations (a test checks the OpenJPEG build
  computes it), only when the decoded pixels can change. The codec's name stays visible, separately, in the log
  (`J2C Engine is: neo-j2k 0.1 (0.1.0) (texture cache key: OpenJPEG: 2.5.4, Runtime: 2.5.4)`) and the About box
  (`getEngineName()`).
- **Not in this change**: `TRUNCATED` and `NEED_DATA` statuses are not passed to the texture fetcher (a decode of a
  cut download succeeds as OpenJPEG's does), and the bake mask is not decoded with the colour in one pass.
- **Floating point**: every decode and encode runs with round-to-nearest-even and no flush-to-zero or
  denormals-are-zero (`llimagefpstate.h`). The main thread has flush-to-zero on (`LLVector4a::initClass`).

## Busy or idle: threads in one image

neo-j2k starts no thread. Before each decode on an ImageDecode pool thread, the viewer lends the codec the pool as
its executor only when the pool is idle: fewer decode requests are in flight, waiting or running, than the pool has
workers, so a worker is free (`LLImageDecodeThread::isIdle()`, one atomic load; the count goes up when a request is
posted and down when it ends; the worker count is the one the pool started with). A busy pool lends nothing, so each
texture decodes on one thread, which is what a saturated pool wants.

`LLImageDecodeThread::parallelFor` is the executor: the calling pool thread claims jobs itself from a shared counter,
helper tasks posted to the same pool claim the rest (at most one for each free worker, so a busy pool gets none and
its queue is not filled with idle tasks), and the call returns when every job has returned. Jobs never wait for one
another, a helper task that starts late finds nothing to do, and a closed pool refuses the helpers, so the caller
finishes alone: no deadlock with every worker busy. Every job runs with the C floating-point state.
Shutdown is unchanged: `LLImageDecodeThread::shutdown()` closes the queue and joins the workers; no thread is killed.

Only pool threads inside a decode request borrow the pool (`LLImageDecodeThread::getCurrent()`), so the pool outlives
every borrower. Decodes on the main thread, bakes and uploads run on their own thread.

neo-j2k itself splits only a tile of 2^18 samples or more in its largest component (512 x 512), so most textures
are one thread whatever the pool does (neo-j2k `doc/api.md`, "Threads in one image").

## Tests

- `llimagej2c_decode_test` (integration test, both options, the same expectations through either codec): the
  viewer's decode of malformed codestreams, a lossy encode, every small size with one to four components lossless and
  exact, the comment round trip, `getMetadata` values, discard levels 0 to 5, a cut download, the bake mask channel
  (`first_channel` 4), and a JP2 box file refused. With neo-j2k the oversized and subsampled fixtures are a small
  codestream patched in its SIZ and COD, since neo-j2k's encoder takes only the viewer's own images.
- The codec-independent rules against neo-j2k itself: `llj2c_reduce_neoj2k_test`, `llj2c_decodeguard_neoj2k_test`,
  `llj2c_decoderlimits_neoj2k_test` and `llj2c_jp2_file_neoj2k_test` are the OpenJPEG tests of
  `llimagej2coj/tests`, built from the same sources against neo-j2k's OpenJPEG-compatible headers (with
  `LL_J2C_NEOJ2K` defined, which skips the two JP2-writing checks neo-j2k cannot make). To make them pass, two OpenJPEG-side
  fixtures changed: `llj2cdecoderlimits_openjpeg_test.cpp` and `llj2c_jp2_file_openjpeg_test.cpp` set
  `prog_order = OPJ_RLCP` (the viewer's setting; neo-j2k's encoder takes no other), which changes nothing for OpenJPEG.
- `llimageworker_test`: `parallelFor` runs every job once with the C floating-point state, runs on two threads at
  once, cannot deadlock when every worker calls it, and finishes on a closed pool; `isIdle` counts the requests in
  flight and a busy pool gets no helper task.
- `llimagej2c_corpus_tool` (built on demand, either option): `hash` decodes a folder of codestreams for comparison
  between the two builds, `encode` encodes a generated set (sizes 4 to 512, one to four components, lossless, lossy and
  with a comment) and prints each codestream's size and hash, `bench` and `pool` time the decode. CI builds the tool;
  the A/B runs are local.
- CI runs the llimage unit test and `llimagej2c_decode_test` in both options (building a test target runs it).

## Licence

neo-j2k is BSD-2-Clause and ports code from OpenJPEG (BSD-2-Clause). A `USE_NEOJ2K` build ships `licenses/neoj2k.txt`
and `licenses/openjpeg.txt` from the package. See `doc/licensing.md`.
