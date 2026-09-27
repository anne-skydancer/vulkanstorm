# Bounded, asynchronous texture delivery

Development branch: `codex/texture-delivery`, based on `vkstorm-devel` at
`46c6d8c9f3`. Windows/Linux retain the OpenGL 4.3 Core contract.

## Purpose

Reduce the interval between a texture becoming necessary and its first usable
GPU image becoming available. Priority depends on visibility and image residency,
**never on pixel colour**. Grey, white, black or other placeholders are symptoms;
intentionally grey textures have no special treatment.

The investigation found thousands of decoded textures waiting for main-thread GL
creation. More decode concurrency alone can enlarge that backlog. The changes
coordinate decode admission with downstream delivery and move upload preparation
and submission onto the existing single shared-context GL worker.

## Admission and memory

`LLTextureDeliveryBudget` provides a shared **2048 MiB ceiling** for texture-fetch
outputs, decodes already admitted, and preparation for their uploads. Ordinary
work can occupy three quarters of available capacity; the remaining quarter is
reserved for first usable images needed on screen. A request waiting twenty
seconds can use this reserve to avoid indefinite starvation.

Reservations precede decoder submission. They conservatively cover two primary
images at the formatted image's full dimensions rounded up to powers of two,
plus an auxiliary channel when
requested. This accounts for the decoder's initial full-size output allocation
and a separate resized upload image. It intentionally overestimates reduced J2C
outputs. Fast-cache reads reserve their bounded 16-by-16 RGBA entry plus preparation
before reading. New reservations fail without blocking; the existing fetch state
machine retries them without marking an asset missing.

Raw outputs and their scaled derivatives share ownership of the reservation.
Capacity returns when the final owner releases it, including pending cache writes
and uploads. An obsolete result is released by the fetch worker before it seeks a
reservation for another decode. Otherwise old results could pin the capacity
needed to replace themselves.

When available RAM falls, new admission uses a lower effective ceiling, leaving
512 MiB of headroom and using half of additional available memory. The effective
ceiling has a 256 MiB floor so a large RGBA image with auxiliary data can still make progress.
Existing reservations are never revoked. This is admission control, not a promise
to keep total process memory below 2048 MiB. Resident textures, saved raw-image
caches, encoded/network data, decoder/OpenCL scratch memory and driver-private
allocations remain outside this budget. Caller-created local/raw-readback images
already exist before delivery; their upload preparation receives a reservation
at dispatch, but their original allocations are not retrospectively capped.

The decoder remains this fork's OpenJPEG configuration, including its automatic
OpenCL selection and CPU fallback. No decoder replacement or fixed GPU percentage
split is introduced: decode admission yields when delivery cannot drain its work.

## Scheduling and parallel work

All fetched images enter one main-thread delivery queue, including images formerly
posted directly to the background worker. Dispatch uses these tiers:

1. Visible images with no usable GL image, and requests aged twenty seconds.
2. Visible resolution upgrades.
3. Other work.

Age orders work within each tier. Visibility reuses the existing face-demand
traversal instead of rescanning faces in the comparator. Boosted UI/avatar assets
and faceless demand, including GPU particles, retain timely delivery. This is the
viewer's visibility estimate, not a new pixel-level occlusion pass.

Before dispatch, an upload is omitted if the displayed image already satisfies
current demand. Ordinary surface images that became unnecessarily detailed are
resized into a separate raw image on the worker before GL allocation. Sculpt,
auxiliary/callback data and unsupported two-channel CPU scaling retain their
original raw representation. The shared fetch result is never resized in place.

The worker prepares and uploads an isolated `LLImageGL`, computes the existing
alpha/picking mask, and generates GPU mipmaps through the existing GL path. It
does not mutate the displayed image. The pending object is excluded from global
texture-option traversal and rendering bind statistics while it belongs to the
worker. Preparation and GL submission are serialized on one worker but overlap
network fetching, the decode pool and main-thread rendering. This first version
uses the raw source as staging, not an additional persistent-mapped PBO ring.

There are at most sixteen outstanding uploads. Submission pauses when outstanding
source/preparation bytes reach 64 MiB; one admitted image may cross that threshold.
This smaller window bounds driver work independently of the 2048 MiB decode
backlog. Queue saturation postpones work instead of forcing a synchronous upload.
The existing `RenderGLMultiThreadedTextures` diagnostic setting now defaults on in both settings XML and Windows/Linux feature
tables;
unavailable shared contexts retain the main-thread path.

## Completion and correctness

The worker inserts a fence after upload commands and flushes its context. It then
publishes CPU completion with release/acquire synchronization. The render thread
polls the fence with a zero timeout. An unfinished upload leaves the displayed
image intact. Completed uploads can pass an unfinished predecessor.

Once ready, image storage, dimensions, format and picking mask are adopted together
on the render thread. The original `LLImageGL` identity remains stable and its
content revision increments, invalidating GPU-particle bindless snapshots. The
pending object receives the old image and retires it through the existing delayed
texture deletion path. Demand is checked again at completion to avoid replacing an
already sufficient image with an obsolete upgrade.

Failure retains the previous image and releases pending ownership through normal
cleanup. Texture completion is serviced before teleport's early return so completed
jobs cannot pin memory throughout a transition. Shutdown drains workers/fences
before releasing pending images, without scheduling sculpt rebuilds after pipeline
teardown. Normal frame completion never waits for an unsignaled fence.

The fence/poll protocol follows the Khronos references for
[glFenceSync](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glFenceSync.xhtml)
and [glClientWaitSync](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glClientWaitSync.xhtml).
Driver upload calls themselves may still block the upload worker or contend with
rendering; asynchronous submission does not guarantee independent hardware engines.

## CPU scope and validation

### Own-attachment detail retention

Camera-driven downscaling now preserves already resident detail on the user's
own worn attachments, including linked children and all registered material
texture channels. Ownership is checked both when requesting a downscale and
when executing an already queued downscale. Detachment removes protection;
low system memory allows reclamation. This does not request full resolution
for unseen attachments or raise the delivery budget. It can retain more GPU
memory while items remain worn; the general rendering memory estimate still
requires separate investigation. Normal texture lifetime/eviction remains in place.

Asynchronous publication also rejects a lower-resolution replacement of an
existing sharper image. Deliberate downscaling remains a separate path.
The publication fixture covers stale poorer uploads and attachment ownership,
child prim/material channels, detachment, absent images and low-memory escape.

Development builds use the `Vulkanstorm-RelWithDebInfo` channel; the configuration
validator enforces that name separately from the Release channel.

The retention changes were compiled and linked in Windows RelWithDebInfo build
**7.2.5.82006**. The attachment ownership and poorer-publication regression checks
passed; a Zink startup run passed GPU publication/readback checks and exited
normally. Actual camera-away retention on worn attachments still needs in-world
verification. The same build includes the X11 `Region` namespace collision fix;
its compile regression test passes with an X11-compatible global typedef present.

CPU work remains for fetch scheduling, admission, decode fallback, raw-image
resizing, alpha/picking-mask preparation, upload submission, callbacks and final
publication. Moving preparation to a worker reduces main-thread pressure; it does
not turn these algorithms into GPU compute kernels.

Standalone checks:

- `indra/llimage/tests/lltexturedeliverybudget_test.cpp`: capacity boundaries,
  reserved headroom, shared ownership, pressure and concurrent admission/release.
- `python scripts/tests/test_texture_publication.py`: compiles the production
  admission, discard selection, publication and adoption methods against a deterministic GL fixture. Covers
  pending/ready fences, out-of-order completion, failures, stale demand, metadata
  and mask adoption, revision invalidation and shutdown. Verifies zero normal-frame
  wait timeouts. It also builds and runs the budget concurrency test. It is not
  a driver or visual test.
- RelWithDebInfo-only `VULKANSTORM_TEXTURE_DELIVERY_SELFTEST=1`: submits generated
  textures through the real viewer queue and verifies GPU readback for a first
  image, replacement, narrow image, reduced resolution and redundant upload;
  checks reservation cleanup. No account login is required. This is a development
  harness, not a user setting. The master hook-policy check rejects its inclusion.

Validated on 2026-09-27 with Windows RelWithDebInfo build **7.2.5.82005**, built
through Autobuild and fully staged without an installer. The configuration check
confirmed the Release feature/dependency configuration. Budget/concurrency and
publication checks passed, as did the existing particle compute and alpha checks.
Actual GPU readback checks passed on the Radeon RX 9070 XT with native AMD OpenGL
and Mesa/Zink, both with asynchronous delivery enabled. The synchronous fallback
also passed in an isolated profile with background texture uploads disabled.
All three driver test runs exited normally. These are startup and correctness
checks, not measurements of in-world texture delivery under pressure.

In-world
acceptance requires a comparable dense-region traversal on native OpenGL and Zink,
including teleport, focus loss/recovery, avatar bakes, alpha masks, sculpt textures,
particles and MOAP. Measure time to first usable image, delivery backlog/reserved
bytes, process RAM, texture churn and frame-time tails. No speedup is claimed from
unit tests or a successful build. Linux and other vendors require separate runs.

Development-only hooks inherited from `vkstorm-devel` must be removed when preparing
the eventual master integration; this branch is not itself a release branch.
