# R1 ownership, lifecycle and shared contracts

R1 is implemented in the production native viewer. Its complete local software
replay passes on pinned Windows SwiftShader, including ordinary authentication,
connected UI, resource replacement, neighbours, crossing, teleport,
logout/relogin and shutdown. This is an ownership/lifecycle acceptance; rendering
and service parity assigned to R2Ã¢â‚¬â€œR9 remain separate requirements. Cross-platform
CI repeats the replay and preserves its evidence.

## Implemented contracts

| Requirement | Production implementation | Qualification |
| --- | --- | --- |
| Account and region identities | Account generation, per-host circuit incarnation, request revision and current-region epoch. HTTP/reliable callbacks enter mainloop and recheck ownership there. | Production login/failure/retry/cancellation/logout/relogin replay; compiled actual delayed-completion bodies. |
| Object, face, material and interaction identities | Host-qualified local IDs, UUIDs, owner generations and face/material revisions. Replacement expires old interactions immediately; reused parent IDs retire linked interactions. | Real UDP ObjectUpdate/local-ID reuse; live face/linkset guards and decoded mesh replacement. |
| Avatar, control-avatar, attachment and inventory/task identities | Typed scene owners; early self-avatar CPU initialization and wearable-metadata request. The self owner moves across regions with a new weak terrain query. Control-avatar metadata never creates a human-body substitute. Inventory revisions and task requests retain actual identities. | Actual avatar/attachment/control messages, AvatarAnimation, AgentWearablesUpdate, RequestTaskInventory/ReplyTaskInventory, inventory revisions and self-owner continuity. |
| Neighbours and crossing | Authenticated grants create bounded circuits/scenes. Crossing promotes an existing scene; disable, expiry and far teleport retire stale circuits. Same-host region reuse gets a new incarnation. | Real loopback peer socket/ACKs, colliding local IDs, outward/return crossings and simulator retirement. |
| Variable regions and floating origin | Sparse L/M terrain obeys region dimensions. Global origins are subtracted as signed 64-bit integers before float conversion; account position is rebased through global double coordinates. | M patch beyond 256 metres in a 1024Ãƒâ€”512 neighbour; production crossing; independent large-origin checks. |
| Motion and correction | Receipt/evaluation timestamps, authoritative transforms, velocity/acceleration, target omega, selection freeze/thaw, dilation, health phase-out and interpolation settings. | Message-driven freeze/thaw across viewer frames; production motion policy with independent numerical expectations and invalid-time inputs. |
| Shared resource descriptions | Geometry channels/stride/counts, immutable generations, image origin/encoding/alpha, samplers, material resolution and selected-device capabilities. Draw admission rejects another device's resources. | Production renderer/readbacks, replacement/orientation/blending and device capability evidence. |
| Bounded streaming and retirement | Reservations cover CPU geometry/terrain, GPU meshes/images, masks and decode admission. Queues/retries are bounded; cancellation/replacement check exact adapter/revision. Submissions retain resources through GPU completion; offscreen frames release completed device resources. | Actual decoder/HTTP replacement, last-owner/cancellation fixtures, refusal and old/new generation readbacks using production renderers. |
| Startup and shutdown sequencing | Existing LLAppViewer/LLStartUp settings/authentication/account paths initialize CPU image/cache workers, transport/capabilities, inventory and appearance owner. Operation flags publish real progress; account retirement precedes worker/UI/device destruction. | Production replay, worker-order fixtures, partial initialization, authentication retry, successful/timeout logout and normal shutdown with GL traps. |

Unknown geometry retains its logical owner and pending/unsupported state. It is
not drawn as a correctly rendered ordinary prim. Early appearance does not invent
an authoritative world transform. Optional channels and unresolved material
images are explicit; absent producers are not advertised as implemented.

## Resource and request limits

| Owner or payload | Admission limit |
| --- | --- |
| Circuits/scenes | 65 including current; 64 neighbours |
| Scene objects / CPU avatars | 16,384 / 256 |
| Object cache requests | 4,096 globally; 1,024 per region; three attempts |
| CPU primitive payloads / sparse terrain | 128 MiB / 64 MiB; 4,096 patches per region |
| CPU mesh cache | 128 MiB; 1,024 entries; four simultaneous transfers |
| Mesh transfer/retry | 16 MiB wire admission; three retryable attempts, 2/4-second backoff |
| GPU vertex/index payloads | 256 MiB |
| Core / remote images | 256 MiB / 512 MiB, charging GPU RGBA plus retained CPU RGBA |
| Remote image requests / entries | 32 including decode; 16,384 entries |
| Auxiliary masks / decode admission | 64 MiB / 256 MiB |
| General native asset downloads | 4,096 queued; finite capability/transfer deadlines |

Reservations belong to actual immutable resources. Removing a cache entry cannot
free its charge while a facade, scene, callback or GPU submission owns it.
Refusal preserves valid existing resources and records a refusal. Completed
unused entries may be evicted; pending and externally held resources are
protected. Non-retryable decode/integrity failures remain explicit errors.

These are payload/admission policies, not total process memory, driver overhead
or physical GPU capacity measurements. Sustained full-viewer pressure and
performance qualification remains part of R10.

## Evidence and execution

The [complete result](parity/evidence/r1-owner-lifecycle-summary.json) identifies
source, canonical executable, staging, libraries, capabilities and regressions.
The [archive](parity/evidence/r1-owner-lifecycle.zip) retains production session/HTTP
records, owner snapshots, readbacks, validation controls and build/staging
evidence. The earlier [mesh-budget witness](r1-resource-contracts.md) is historical.

Replay executes actual viewer message readers, CPU workers, HTTP decoders and
renderers; authentication/simulator endpoints are loopback fixtures. It compares
independently expected decoded vertices for two HTTP mesh revisions, retains the
same self owner across outward/return crossing, and requires logout to release
account-owned objects, avatars, mesh requests and CPU mesh reservations. Missing
evidence, wrong device/libraries, validation errors, GL calls, crashes and
timeouts fail acceptance.

`--headless --r1-contracts` explicitly selects a no-window development replay
through the existing window factory/native Vulkan context. It uses offscreen
Vulkan rendering, process-local cursor/clipboard state and isolated account/cache
directories. Older executables lacking this mode are refused; there is no
foreground fallback. Presentation is recorded as unqualified. Native HWND/SDL
swapchain qualification continues separately; offscreen success cannot replace it.

The complete canonical RelWithDebInfo stage remains at
`C:\Dev\vulkanstorm\worktrees\vkstorm-vulkan\build-vc170-64`. Qualification produces
no installer, release publication or `latest` update.

Software evidence does not qualify public-grid authentication, AMD/NVIDIA
hardware, physical-driver performance, complete COF/wearables production,
sculpt/flexible/animesh geometry, PBR, lighting or full-world parity. Those
requirements retain their roadmap assignments and explicit qualification limits.

## Follow-up audit corrections

The 13 October audit reopened three R1 resource cases and an R0 retention check.
World draw bindings now release the bound generation before entering the completed
binding cache; the charged image remains owned by the submission until that point.
The software world test observes the actual Diligent texture reference counters,
verifies the submitted image charge, and requires both allocation and reservation
to retire after completion while the binding cache remains populated. World draw
also unbinds context vertex/index buffers after recording; submissions retain
their exact geometry through completion. The GPU test independently observes
both actual buffer reference counters reaching zero on retirement.

`VSWorldRenderer::publishMesh` publishes only admitted replacements. Faces,
terrain and existing avatar mesh producers share it; refusal preserves the valid
current mesh and retry state. Same-patch terrain updates and compatible same-owner
full object updates preserve the admitted GPU generation before publication;
UUID/kind/asset changes cannot inherit an unrelated mesh. Compiled actual terrain
and object replacement bodies cover identity, face-count, invalid-data and
CPU-admission cases as well as preserved ownership and reservations. Compiled production publication tests exercise a
full budget, successful retry and last-owner release. The real software renderer
also verifies refusal retains the exact currently published generation and pixels.

Native mesh/texture downloads enforce 16 MiB received-body limits, and general
native asset downloads enforce 64 MiB. The shared HTTP callback checks aggregate
size and integer overflow before allocating/appending each body chunk, regardless
of response status, chunking or Range compliance. Admission failure is terminal,
including simulator URL mismatch handling. Other callers retain their default
unlimited policy. Compiled production callback/retry tests cover exact limits,
accumulated chunks, oversize/overflow rejection and the unchanged default.

Successful Lavapipe evidence packaging now verifies every startup/world baseline
`artifact_sha256` entry, including comparisons, readbacks and logs. Missing,
corrupt or traversing artifact identities fail acceptance. Regression tests
exercise removal and corruption rather than relying on top-level report presence.
These follow-up cases require a fresh cross-platform CI run; earlier green runs
do not qualify them merely because their original cases passed.
