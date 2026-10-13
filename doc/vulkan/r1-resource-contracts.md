# R1 native resource ownership

The first local R1 integration establishes mesh resource lifetime and accounting
in the production viewer. R1 remains open: this implementation does not establish
neighbour scene continuity, motion/selection parity or whole-viewer parity.

## Production ownership

`VSWorldAssets` owns account-scoped immutable CPU mesh generations. A download
completion posts to the existing `mainloop` work queue. Before decoding or
publishing, the delivery checks the weak cache owner, active session identity,
account generation, region epoch, connected phase, exact HTTP adapter and asset
revision. Cancellation, replacement, logout and delayed deliveries cannot publish
into a later generation. Decode continues to use the shared viewer mesh/skin
decoders with the existing payload, expansion, face and index bounds.

Changing simulator endpoints cancels incomplete mesh requests and reissues them
against the new authenticated capability only after the destination region is
ready. Completed UUID-addressed mesh data survives a crossing or teleport within
the same account. Logout/relogin invalidates the cache. The current scene still
clears object/terrain/avatar owners at a region change; preserving those owners
and neighbour circuits is remaining R1 work.

Each asset/LOD entry has a monotonically assigned revision. `replace()` invalidates
all cached LODs of one UUID, cancels their exact outstanding adapters and assigns
new revisions. Existing scene references to the old immutable geometry remain
valid until retired. This is the cache replacement contract; connecting local
asset editing/reload producers to it remains part of the producer integration.

Requests are bounded to four concurrent transfers and 1,024 entries. Only the
shared HTTP status policy's retryable failures receive retries, with at most three
attempts and 2/4-second application backoffs. Decode/integrity and residency
failures remain explicit errors. Completed unused entries can be evicted in least
recently requested order; pending requests and geometry held by a scene are
protected from eviction.

## Resident bytes and GPU retirement

`VSResourceBudget` attaches a shared reservation to the actual immutable resource,
rather than charging only a cache entry. Cache reset, replacement or map removal
cannot release bytes while another owner still references that generation. The
reservation's shared accounting state also survives destruction of its cache.

The CPU mesh cache admits at most 128 MiB of charged decoded geometry/skin data.
This includes old decoded generations retained by scenes. Mesh wire/temporary
decode admission remains separately bounded; these counters do not claim to
measure total process memory, texture residency or driver allocation overhead.

The world renderer independently limits immutable vertex/index buffer payloads
to 256 MiB. Its geometry owner carries the reservation. Existing fence-backed
submissions retain packets and geometry until completion, so in-flight buffers
remain charged after scene replacement. An allocation refused by this policy
does not produce a substitute mesh or a null draw packet; producers retry missing
meshes on subsequent updates. Invalid geometry and foreign-device packets retain
their existing decisive error checks.

Scene evidence reports CPU live bytes/limit, outstanding requests, cache entries,
evictions and replacements, plus GPU live mesh bytes/limit and allocation refusals.
The limits are explicit initial policies, not a measured performance target or a
claim about all available device memory.

## Qualification

`scripts/tests/test_native_world_assets.py` compiles the actual queued publication,
region reconciliation and replacement bodies, and the actual reservation class.
It verifies delayed account/region/phase/owner/request/revision rejection, retry
limits and non-retryable failure, same-account decoded reuse, replacement,
last-owner retirement, capacity overflow and cache-owner destruction. These tests
run without a window or network. Existing scene/camera admission and session
lifecycle fixtures remain applicable.

The canonical `build-vc170-64` RelWithDebInfo viewer is rebuilt and fully staged
through the existing Autobuild/CMake workflow. A build or these compiled fixtures
does not qualify runtime GPU budget-pressure behavior, real crossing continuity,
all resource producers, or authenticated relogin. Those remain explicit R1
acceptance work. The running software graphics CI qualifies its own recorded
source revision; local changes do not retroactively change that evidence.
