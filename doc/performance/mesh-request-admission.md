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
