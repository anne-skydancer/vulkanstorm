# Mesh request admission

Completion processing keeps its 1.5 ms soft time allowance and fair queue service.
Admission now limits total active plus queued network work using three signals:
waiting mesh/skin count, estimated retained decoded payload bytes, and observed
completed mesh/skin entries per second. Partial notification steps do not count
as fully drained entries. These signals do not measure total process/GPU memory.

Initial throttle thresholds are 256 entries, 64 MiB estimated payload, or more
than two seconds of measured drain capacity (minimum 32 entries). Recovery needs
all lower thresholds: 128 entries, 32 MiB, and at most one second of drain capacity
(minimum 16 entries). Drain rate uses wall time and a smoothed one-second-or-longer
window, and resets after the completion queue empties. These are initial tuning
values, not an assertion of performance parity.

Healthy queues retain full configured concurrency. Only after a throttle threshold
is crossed does concurrency taper with pressure, retaining a floor of one outstanding request
when the configured limit is nonzero. Existing in-flight requests are not cancelled.
This is not a hard bound on retained bytes if the consumer ceases to make progress;
the diagnostic counters expose that condition for investigation.

Decoded mesh estimates are charged at enqueue and retained through partial fanout;
completed deliveries release the charge. Skin estimates are frozen at enqueue.
Counters are protected by the existing completion mutex. Network-work accounting
uses pending skin requests, not completed skin results.

Regression tests cover time-bounded completion service, fairness, hysteresis,
large payloads, stalled/fast consumers, elapsed-time sampling, recovery and extreme
inputs. In-world cold/warm measurements remain necessary. Priority lanes, LOD
request ordering, and compute retry policy are separate work.

## Request prioritization

As of the September 28 delivery investigation, admission follows Firestorm
`10cd9da2638e19ff149e85fbcd00f7f33563c0ff`: when demand exceeds available slots,
refresh pending scores and select the highest scores across the whole queue.
When all requests fit, preserve queue order. Lane quotas and the five-second
age override no longer determine admission. Lane counters remain diagnostic.
The worker services skin requests, then LOD requests, then headers, matching
upstream's class priority. This can delay headers under sustained skin/LOD load;
there is no longer a reserved first-geometry share.

Keep completion-pressure limits, removal of ownerless requests, failure wakeups,
and bounded worker passes that defer delayed retries to the next pass. HTTP
timeouts and transport retry policy are unchanged. This change aligns request
selection with upstream; it does not establish or fix the cause of the observed
multi-minute interval with no mesh completions. In-world validation is required.

`test_mesh_request_priority.py` exercises the production selection block for
score ordering, bounded/zero admission, queue preservation when all requests fit,
and worker class priority.

## HTTP delivery recovery

Mesh downloads now use parallel HTTP/1.1 connections: AP_MESH2 no longer opts
into pipelining, and both small and large mesh requests explicitly request
HTTP/1.1. The ordinary mesh connection default remains eight; large downloads
retain their separate two-connection policy. Textures, uploads, and other callers
keep their existing transport policy. Upstream request prioritization is retained.

Mesh options enable curl's low-speed timeout at less than one byte per second
for a twenty-second window. This covers a silent response and a transfer that
stops after receiving data without imposing a short total deadline on slow,
progressing downloads. Curl's speed accounting means this is not an exact
twenty-second timer since the last byte. DNS/connection setup retains its separate
connection timeout. Overall small/large transfer limits remain 120/600 seconds;
the small-mesh pipeline multiplier no longer applies.

Transient mesh failures now return immediately to the repository rather than
retrying inside HTTP. The handler is released, freeing active/admission capacity.
A worker-owned deferred queue waits at least thirty seconds, then appends the
request to its class queue's tail. Retry-After delta-seconds and HTTP dates can
extend this cooldown. Timeout/connection failures, retryable server errors,
HTTP 408 and HTTP 429 never become unavailable merely through retry exhaustion.
Nonretryable failures such as 404/410 retain terminal handling. Deferred work
retains request values only, not viewer objects or HTTP handlers, and is destroyed
with the repository. It currently remains queued across region changes, just as
in-flight requests do; cancellation of no-longer-needed assets is separate work.

Ordinary MeshHTTP messages report failed attempts and deferral status, including
policy, status, protocol enum, elapsed/connect/first-byte timing and downloaded
bytes. They omit capability URLs and asset identifiers. Each repository retry
creates a new HTTP operation, so its transport attempt counter restarts at one.
These timings describe curl attempts, not time waiting in the repository queue.

Validation: test_mesh_http.py runs the production option block against a local
HTTP server using the bundled curl on Windows, exercising silent and partial
stalls and progressing transfers. It also exercises the production retry-selection
block for one-slot fairness, delayed retries, empty queues, and unchanged default
retry-first ordering. These checks do not substitute for in-world measurements.

`test_mesh_deferred_retry.py` exercises the production failure routing and deferred
queue drain: backoff does not populate admission queues, matured retries append
behind existing work, LOD accounting is preserved, repeated transient failures
remain retryable, terminal missing-asset responses remain terminal, and longer
server-requested cooldowns delay resubmission.
