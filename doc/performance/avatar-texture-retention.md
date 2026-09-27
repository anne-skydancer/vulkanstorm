# Avatar texture retention

Already-loaded avatar and attachment textures receive a grace period before
ordinary detail reduction. Distance is measured from the user's avatar, not the
camera, and the existing Firestorm Draw Distance (`RenderFarClip`) caps proximity
protection.

| Distance | Retention before downscaling |
| --- | --- |
| Up to and including 10 m | 60 seconds |
| Over 10 m and below 20 m | 30 seconds |
| 20 m and beyond, or outside Draw Distance | Normal policy |

The timer starts at the first requested reduction. Repeated attempts do not
renew it; movement between bands changes the duration against the same start.
Demand for the current detail resets the timer. Shared textures receive the
strongest applicable protection across their current face owners and material
channels. Dead owners do not retain textures.

The user's own worn attachments retain their existing detail independently of
camera direction and the proximity timer. Detachment removes that protection.
Low system memory overrides all retention. This policy neither forces full-detail
downloads nor replaces general texture eviction or changes the global discard bias.

The guard runs before queueing downscaling and again before a queued reduction
executes. The retention policy was integrated into master through PR #79;
asynchronous delivery and budgeting followed in PR #80. The development branch
also retains its runtime validation hooks; these remain excluded from master.

Validation: `python scripts/tests/test_avatar_texture_retention.py` compiles the
production retention method against deterministic time, distance and ownership
fixtures. Run `python scripts/tests/check_release_hooks.py` for release policy.
The fixture is not an in-world visual or memory-pressure test.
