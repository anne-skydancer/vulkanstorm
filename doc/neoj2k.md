<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# neo-j2k as the J2C codec (`USE_NEOJ2K`)

[neo-j2k](https://github.com/anne-skydancer/neo-j2k) (BSD-2-Clause) is a JPEG 2000 Part 1 codec written for
texture-sized images. The viewer can use it instead of OpenJPEG; the choice is made when CMake configures.

## The option

`USE_NEOJ2K` (default **OFF**) picks the codec package:

| | `USE_NEOJ2K=OFF` | `USE_NEOJ2K=ON` |
|---|---|---|
| Package | `openjpeg` ([3p-openjpeg](https://github.com/anne-skydancer/3p-openjpeg)) | `neoj2k` ([3p-neoj2k](https://github.com/anne-skydancer/3p-neoj2k)) |
| Library | `openjp2.dll` (delay-loaded) | `neoj2k.dll` (delay-loaded, exports `nj2k_*` only), `libneoj2k.so` on Linux |
| Implementation | `indra/llimagej2coj` | `indra/llimagej2cneo` |
| Decode | OpenJPEG | neo-j2k |
| Encode (uploads, bakes, snapshots) | OpenJPEG | neo-j2k (byte-identical to OpenJPEG's for the viewer's settings) |

One codec per binary: the implementation is the one `fallbackCreateLLImageJ2CImpl()` (`llimage/llimagej2c.cpp`)
finds at link time. There is no runtime routing and no per-file fallback, and OpenJPEG is neither installed nor
linked when the option is on. `indra/cmake/NeoJ2K.cmake` carries the option; `USE_NEOJ2K` with `USE_KDU` is an error.

Local configure: add `-DUSE_NEOJ2K:BOOL=ON` to the CI configure line (`.github/actions/viewer-build/action.yml`).
CI builds both options on every pull request and qualification build (the `<platform>-neoj2k` jobs of
`build-windows.yaml`, `build-linux.yaml` and `build-linux-lts.yaml`); a release build never uses neo-j2k yet.

## What the viewer does with it (`llimagej2cneo.cpp`)

- **One decoder per thread**: a `thread_local` neo-j2k decoder on each thread that decodes (the ImageDecode pool,
  the main thread for local files and the bake integrity checks), created on first use and destroyed at thread exit.
  The decoder has the Second Life limits (`NJ2K_DECODER_LIMITS_SL_TEXTURE`) and non-strict mode, keeps 4 MiB of
  working memory between images, and always starts a request at reduce factor 0. Encoders are made per call.
- **One header parse**: `getMetadata` and the decode that follows it on the same bytes share the parse (neo-j2k
  keeps the header model while the main header is the same).
- **The samples go straight into the `LLImageRaw`** (8 bits, interleaved, bottom-up): no int32 planes, no copy loop.
- **The rules of `llimagej2coj` stay** and are shared as headers: the reduce factor rule (`llj2creduce.h`), the
  decode guard (`llj2cdecodeguard.h`) and the comment scan. The size is the image area, not the tile grid.
- **The texture cache key** (`getEngineInfo()`) is `neo-j2k output 1`, the viewer's own decoder-output version, not
  neo-j2k's release: a library update does not purge every user's cache. Bump the number when decoded pixels change.
  Switching codec changes the string, so a viewer that switches purges its cache once.
- **Not in this change**: `TRUNCATED` and `NEED_DATA` statuses are not passed to the texture fetcher (a decode of a
  cut download succeeds as OpenJPEG's does), and the bake mask is not decoded with the colour in one pass.
- **Floating point**: every decode and encode runs with round-to-nearest-even and no flush-to-zero or
  denormals-are-zero (`llimagefpstate.h`). The main thread has flush-to-zero on (`LLVector4a::initClass`).

## Busy or idle: threads in one image

neo-j2k starts no thread. Before each decode on an ImageDecode pool thread, the viewer lends the codec the pool as
its executor only when the pool is idle: fewer decode requests are waiting than the pool has workers
(`LLImageDecodeThread::isIdle()`, one atomic load; the count goes up when a request is posted and down when a worker
starts it). A busy pool lends nothing, so each texture decodes on one thread, which is what a saturated pool wants.

`LLImageDecodeThread::parallelFor` is the executor: the calling pool thread claims jobs itself from a shared counter,
helper tasks posted to the same pool claim the rest, and the call returns when every job has returned. Jobs never
wait for one another, a helper task that starts late finds nothing to do, and a closed pool refuses the helpers, so
the caller finishes alone: no deadlock with every worker busy. Every job runs with the C floating-point state.
Shutdown is unchanged: `LLImageDecodeThread::shutdown()` closes the queue and joins the workers; no thread is killed.

Only pool threads inside a decode request borrow the pool (`LLImageDecodeThread::getCurrent()`), so the pool outlives
every borrower. Decodes on the main thread, bakes and uploads run on their own thread.

neo-j2k itself splits only a tile of 2^18 samples or more in its largest component (512 x 512), so most textures
are one thread whatever the pool does (neo-j2k `doc/api.md`, "Threads in one image").

## Tests

- `llimagej2c_decode_test` (integration test, both options): the viewer's decode of malformed codestreams. With
  neo-j2k the oversized and subsampled fixtures are a small codestream patched in its SIZ and COD, since neo-j2k's
  encoder takes only the viewer's own images.
- The codec-independent rules against neo-j2k itself: `llj2c_reduce_neoj2k_test`, `llj2c_decodeguard_neoj2k_test`,
  `llj2c_decoderlimits_neoj2k_test` and `llj2c_jp2_file_neoj2k_test` are the OpenJPEG tests of `llimagej2coj/tests`,
  compiled unchanged against neo-j2k's OpenJPEG-compatible headers.
- `llimageworker_test`: `parallelFor` runs every job once with the C floating-point state, cannot deadlock when every
  worker calls it at once, and finishes on a closed pool.
- `llimagej2c_corpus_tool` (built on demand, either option): decodes a folder of codestreams for comparison (`hash`)
  and timing (`bench`, `pool`).

## Licence

neo-j2k is BSD-2-Clause and ports code from OpenJPEG (BSD-2-Clause). A `USE_NEOJ2K` build ships `licenses/neoj2k.txt`
and `licenses/openjpeg.txt` from the package. See `doc/licensing.md`.
