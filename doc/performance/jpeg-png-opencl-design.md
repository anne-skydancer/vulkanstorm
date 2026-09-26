# JPEG and PNG decode acceleration design

Status: proposed engineering design, not implemented. Source baseline: vkstorm-devel
d50ffb1c06. This work is parked behind the current memory investigation.

## Purpose and boundaries

Improve image-ready latency and throughput using vendor-neutral OpenCL and multiple
independent image jobs. Preserve the established CPU decoders, output semantics,
texture demand, expiry and focus-loss behaviour. Do not introduce an asset pool,
pin decoded images, change image quality, or require graphics interoperability.
Windows and Linux are the initial implementation targets. Driver availability is
detected at runtime; no additional hardware purchase or cross-vendor testing by the
user is assumed. Supported does not mean validated on every device.

## Verified current interfaces

- `indra/llimage/llimageworker.cpp`: LLImageDecodeThread already owns an eight-thread
  ImageDecode pool. Each queued ImageRequest holds the formatted image and responder,
  creates LLImageRaw, and calls the format-specific synchronous decode method.
  processRequest holds image-data locks for its duration. finishRequest publishes
  primary/auxiliary images and the request ID through the existing responder.
- `indra/newview/lltexturefetch.cpp`: endWork clears the decode handle but explicitly
  notes that the underlying thread pool cannot cancel an individual queued item.
- `indra/newview/llviewertexturelist.cpp`: getRawImageFromMemory calls decode directly;
  getImageFromMemory wraps that result as a local texture. Thus not every PNG/JPEG
  path already benefits from the eight workers. Embedded glTF image preparation
  reaches this path through getFetchedTextureFromMemory.
- `indra/llimage/llimagejpeg.cpp`: libjpeg decompression interface.
- `indra/llimage/llpngwrapper.cpp`: libpng reads into caller-owned LLImageRaw using
  reversed row pointers, with palette/transparency, grayscale, bit-depth and gamma
  transforms. These are compatibility requirements, not optional cleanup.
- The inspected C:/Dev/openjpeg source has an internal OpenCL worker array capped at
  eight and OPJ_OPENCL_WORKERS configuration. Its exact packaged revision/API must
  be reconciled before integration; it is not assumed to share a viewer context.

## Architecture

Use the existing CPU executor, a central admission controller, and a small OpenCL
execution service. A worker is an independent job execution slot, not a new thread
pool for each format. Context/program state is shared within the new JPEG/PNG
service; mutable kernel arguments, command queues, codec state and scratch belong
to an execution slot and cannot be mutated by two jobs concurrently.

Proposed interfaces (names illustrative):

```
DecodeTicket submit(EncodedImageRef, DecodeSpec, Priority, Generation, Completion);
void cancel(DecodeTicket);
DecodePlan probe(HeaderView, DecodeSpec, DeviceCapabilities);
// Plan reports host output, auxiliary output, host/device scratch and transfer bytes.
// Admission returns admitted, deferred, unsupported, oversized, or invalid.
```

Lifecycle: queued -> header probe -> admitted -> CPU stage -> GPU stage -> readback
-> completion pending -> consumer ownership. CPU-only plans omit GPU stages.
Cancellation and failure are terminal alternatives; notification happens at most
once per ticket. Pending GPU commands must finish before their memory is reused.

Do not block a CPU worker waiting for an admission token. Deferred jobs remain in
the controller, holding encoded input but no decoded output or large scratch.
Bound pending encoded bytes too, by backpressure at callers rather than silently
dropping requested images. Preserve existing demand priority and add aging; do not
invent a second, conflicting texture-priority system.

Initially a synchronous codec adapter may occupy a worker through GPU completion.
Moving to continuations requires transferring owned buffers between stages and
reacquiring image locks on their owning thread; never carry existing RAII locks
across threads. Event callbacks enqueue completion work instead of calling viewer
or OpenGL APIs. Texture creation/upload remains on its established path.

For embedded/local asynchronous loading, introduce a separate ticket-based API.
Publish the result on the existing viewer thread with an asset generation check.
Keep synchronous APIs for callers that require immediate results; do not block the
render thread waiting for a newly introduced GPU future. GPU conversion of such
callers is a distinct audited migration, not a side effect of replacing decode().

## Memory and concurrency admission

Independent limits cover active CPU jobs, active GPU jobs, pending encoded input,
host bytes and device bytes. Worker count alone is never a memory limit.

For each job reserve the checked worst-case simultaneous live set: output + auxiliary
output + codec scratch + transfer staging + device input/output + device scratch.
Include row padding, coefficient arrays and interlace passes, not just W*H*4.
Perform overflow checks and format dimension checks before allocation. Unknown
requirements use incremental reservation before allocating or choose the CPU path;
an underestimated reservation must never silently exceed the budget.

Track actual allocations once by ownership, alongside unspent reservations. Idle
scratch counts against the same budget. Reuse a bounded amount of scratch; release
oversized idle allocations under pressure instead of retaining each worker's peak.
Reservation transfer must be atomic at each lifecycle transition. Cancellation
releases only memory no longer referenced by in-flight commands.

Completion output continues to count while waiting for the consumer. Transfer its
charge to completion/texture accounting when handed off; do not declare it freed.
Reserve completion capacity at admission so producers cannot fill every slot and
then deadlock waiting to publish. Consumer draining must not require a decode token.

CPU fallback also consumes host budget. If a valid image exceeds the chosen working
budget, explicitly defer or use an approved single-job/streaming plan that fits;
never bypass limits because the GPU path was rejected. Do not lower texture quality.

Initial experiments use one and two GPU jobs, then four only if both frame time and
memory improve. Keep the existing CPU worker ceiling initially. Byte limits and
size thresholds are benchmark parameters, not asserted production defaults.

OpenJPEG coordination is a release gate: an outer job-count semaphore does not bound
its retained device scratch. Add/version a backend budget, stats and trim interface,
or keep the new acceleration disabled until a conservative combined bound can be
enforced. Do not change OpenJPEG's proven runtime policy through environment changes
per job. Avoid nested CPU worker oversubscription as well.

## JPEG backend

First GPU eligibility: baseline 8-bit grayscale/YCbCr with explicitly supported
subsampling. Other colour spaces, progressive/arithmetic modes and unusual layouts
stay on the existing decoder until separately implemented and tested.

CPU: validate stream, parse tables and entropy-decode quantized coefficients.
OpenCL: dequantize, inverse DCT, chroma upsample and convert to required channels.
Write into the existing CPU output via readback in the first implementation.
The coefficient API can materialize a full image and must be charged accordingly;
benchmark this against the current scanline decoder before using it broadly.
Do not mistake coefficient extraction for free preprocessing.

Use the existing integer reconstruction, rounding, upsampling and colour conventions
as the reference. Require byte equality to the pinned CPU configuration for the
initial supported modes. Any tolerance proposal needs separate review; it must not
hide gamma, orientation, alpha or chroma errors. Restart markers may later expose
entropy parallelism, but arbitrary files cannot be assumed to contain them.

## PNG backend

First eligibility: non-interlaced 8-bit RGB/RGBA. CPU validates chunks and inflates
the IDAT zlib stream. OpenCL reconstructs filtered bytes and applies equivalent
output transformations. Preserve chunk CRC/zlib integrity handling and malformed
input limits. IDAT chunks are not independent compressed images.

Libpng's current public wrapper does not expose the required raw filtered stream.
Choose explicitly between a maintained adapter/fork and a narrowly scoped validated
chunk+inflate frontend. Do not claim this is a switch around png_read_image.

Filter scheduling: None is independent; Sub permits prefix sums by byte lane;
Up is parallel across a row after its predecessor; Average/Paeth require dependency
ordering. Analyze mixed-filter runs, then schedule supported tiles/workgroups.
No arbitrary independent row launch, no device-wide spin barriers, and no per-row
kernel launch design without measured evidence. Dependent rows may run serially
within a group while other images occupy the device. Support all five filters in
the eligible image class or route the entire job safely to CPU before publication.

Palette, low bit depth, 16-bit, interlaced and other variants initially use libpng.
Subsequent support must reproduce transparency and current gamma/row-orientation
semantics exactly. Do not convert all formats to RGBA unconditionally.
GPU DEFLATE is a separate later project; PNG acceleration must justify itself without
assuming that project will rescue a slow hybrid path.

## Routing, cancellation and failure

Choose a backend from measured format/size classes and device support, including
transfer time. Never wait to accumulate a batch; process available independent jobs.
Do not run both CPU and GPU copies speculatively. Apply hysteresis to routing to
avoid oscillation. Keep GPU work bounded when graphics is busy; APIs do not promise
that compute queue priority protects rendering.

Check cancellation before expensive CPU stages, submission and publication. An
already submitted kernel is not generally preemptible; discard its stale result
after completion and release safely. Keep encoded input for one CPU fallback until
GPU success is accepted. On OpenCL failure, retire/quarantine the failed resources,
release their reservation only when safe, and admit CPU fallback under the same
host limits. Disable repeatedly failing device paths for the session. Invalid files
fail normally instead of looping between backends. Shutdown drains events before
destroying contexts and emits no callbacks into destroyed viewer objects.

## Validation and rollout gates

1. Instrument current PNG/JPEG caller paths and decode stages; count images, sizes,
   latency and CPU time. Establish that they matter in the intended workload.
2. Standalone CPU reference corpus and bounded scheduler tests, including cancellation,
   completion backpressure, oversized/malformed images and device failure.
3. JPEG kernel prototype; compare total image-ready latency, peak host/device memory
   and output against the existing decoder. Retain only beneficial format/size classes.
4. PNG prototype with every filter/mixed-filter sequence in the supported class.
   Benchmark inflation, filtering, conversions and transfers separately.
5. Coordinate OpenJPEG accounting, then integrate asynchronous callers incrementally.
6. In-world comparison under native OpenGL and Mesa/Zink on available hardware.
   Measure p50/p95/p99 image-ready latency and frame time, focused/unfocused recovery,
   host working set/commit, device allocations and steady-state memory after bursts.

No throughput-only acceptance. A repeatable rendering or memory regression blocks
enabling the backend. Additional vendor devices are validated when available, with
untested configurations identified honestly. Direct CL/GL sharing is optional future
work with capability and renderer-specific tests; the first design requires no such
interop and includes readback in every performance claim.

## Return to the memory investigation

No decoder implementation or retention changes accompany this document. Resume with
measuring CPU vertex mirrors of published compute buffers, then copied skin weights
in rigged CPU geometry. Preserve picking, bounds, selection, buffer reuse and texture
expiry. Neither decoder acceleration nor fewer copies promises a 4-6 GiB total until
measured against the actual allocation breakdown.

## Format/API references

- PNG specification: https://www.w3.org/TR/png-3/
- OpenCL API/event and object lifetime: https://registry.khronos.org/OpenCL/specs/unified/html/OpenCL_API.html
- Optional GL sharing: https://registry.khronos.org/OpenCL/specs/unified/refpages/man/html/cl_khr_gl_sharing.html

These specify format/API behaviour, not speedups for this viewer.
