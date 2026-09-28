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
