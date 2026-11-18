<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# GPU reset handling (OpenGL)

When the GPU stops responding, the driver resets it: Windows after 2 s (TDR), Linux after the kernel driver's job
timeout (amdgpu 10 s; i915 has its own hang check; NVIDIA on Linux usually has none). Until now the viewer had no way to
see this: its OpenGL contexts were created without reset notification, so it either crashed inside the driver or carried
on drawing into a dead context. Since this change, the viewer sees every GPU reset, says so, and closes cleanly.

The owner decided this on 2026-11-17 (decision 2 of `reports/aurora-gpu-hangs-2026-11-17`, final form): show a warning,
close cleanly where possible, and change nothing for the next start. In particular, the graphics settings are not
lowered ("users would hate me for resetting settings after a crash").

## What the viewer does

1. **Context creation asks the driver to report a reset.** The viewer requests only the reset notification strategy
   `LOSE_CONTEXT_ON_RESET` (`WGL_/GLX_ARB_create_context_robustness`). It does **not** request robust buffer access:
   that flag bounds-checks every buffer and texel fetch, which costs performance and is not needed to see a reset.
   With the default strategy (`NO_RESET_NOTIFICATION`), `glGetGraphicsResetStatus` always returns `NO_ERROR`, which is
   why the strategy has to be requested at creation.
   - **Windows:** the main context asks for the strategy when the driver lists `WGL_ARB_create_context_robustness`
     (`LLGLManager::mHasWGLContextRobustness`, probed in `initWGL`). At each version the same request is then tried
     without it before stepping down, so a driver that refuses the strategy still gets the context it gets today.
     The worker context (`LLImageGLThread`, through `createSharedContext`) uses exactly the main context's strategy.
     WGL refuses to share between contexts whose strategies differ, and a reset is reported to every context in the
     share group.
   - **Linux (SDL2 2.28, X11/GLX):** `SDL_GL_CONTEXT_RESET_NOTIFICATION` alone does nothing. With its default
     2.1/no profile/no flags, SDL calls the legacy `glXCreateContext`, which takes no attributes. The viewer therefore
     also sets the compatibility profile mask with SDL's default version. SDL then uses `glXCreateContextAttribsARB`,
     and the driver still returns its highest compatibility version, as the legacy call does. If that context cannot
     be made, the request is repeated exactly as it was before this change. Worker contexts are made from the same
     SDL attributes, so their strategy matches. SDL's EGL path (native Wayland) ignores the strategy, so the viewer
     leaves the request unchanged on drivers other than `x11`.
   - **The log says which applies:** `GPU reset notification: on` or `off` (from
     `glGetIntegerv(GL_RESET_NOTIFICATION_STRATEGY)` on the main context, in `LLGLManager::initGL`). On Windows
     there is also `Created OpenGL 4.6 ... context with GPU reset notification.`
2. **One check per frame.** `LLAppViewer::checkGPUReset()` runs once a frame on the main thread, before the frame's GL
   work. It calls `glGetGraphicsResetStatus` only while notification is on and no reset has been seen. The first reset
   is kept (`LLGLReset::latch`): the GL reports a reset until the driver has completed it and `NO_ERROR` after that,
   but the context stays lost.
3. **On the first reset (guilty, innocent or unknown):**
   - **The log.** The first line written is the reset with its kind, the raw status, the cause, `GL_RENDERER` and
     `GL_VERSION`. The log flushes every line, so it is written before anything else runs on the lost context.
   - **An OS message box** (GL is dead, so XUI can't be drawn), titled "Graphics Reset". For a guilty reset (the
     viewer's own GPU work ran past the driver's timeout) it says "The graphics card was overloaded and has been
     reset." For an innocent or unknown reset it says "The graphics driver was reset." The rest of the text is the
     same: the viewer can no longer draw, it will log out and close, and the settings have not been changed. The
     mainloop watchdog is paused while the box waits for the user. A non-interactive run has no one to answer it, so
     there the reset is only logged.
   - **The normal quit:** `requestQuit()`, then the logout, then `cleanup()`, which saves the settings and closes
     everything. Only the parts that need drawing or a visible dialog are left out (below).
   - **Nothing for the next start:** no marker, no settings change, no lower graphics. The clean close removes the run
     markers as every clean close does, so the next start is an ordinary one.

## The shutdown path on a lost context

The OpenGL 4.6 core specification (section 2.3.2, "Graphics Reset Recovery") defines a lost context. After a reset, every
command on the context, or on a context sharing with it, raises `CONTEXT_LOST`. Such a command has no side effects, does
not write the memory it is given for results, and does not block indefinitely or end the application. `GetError` and
`GetGraphicsResetStatus` work normally. Polling queries report completion: `GetSynciv(SYNC_STATUS)` returns `SIGNALED`
and `QUERY_RESULT_AVAILABLE` returns `TRUE`.

So a GL call on the shutdown path cannot block or crash. The only risk is code that **uses a call's output** (a
mapped pointer, a read-back image, a queried value), or that treats an error as fatal. The table goes through every
part of the path:

| Step | GL use | On a lost context | Change |
|---|---|---|---|
| `display()`: every frame's rendering | thousands of calls; snapshots and picks read back; the teleport progress is driven here | no-ops, a frame of wasted calls; read-backs return nothing | **Skipped** once the context is lost (`doFrame`) |
| `idle()`: texture creation and upload, media and parcel textures | `glGen*`, `glTex(Sub)Image*` | no-ops; `glGenTextures` writes no names, so the name pool hands out stale values, harmless on a dead context | none |
| `LLImageGLThread` (worker, shared context) | uploads, `glFenceSync` + `glClientWaitSync(GL_TIMEOUT_IGNORED)` (NVIDIA) | no-ops; **the wait returns at once**. The unbounded wait from the GPU-hang study becomes bounded | none; joined in `cleanup()` as before |
| `requestQuit()` | none (floaters asked to close, metrics) | | none |
| `idleShutdown()`: waits for modal dialogs and for floaters that ask "save changes?" | none, but no one can see or answer a dialog | would wait for ever | **Not waited for** once the context is lost; each floater is still asked to close once |
| `saveFinalSnapshot()` (`idleShutdown`, the exit block of `doFrame`, `gDoDisconnect`) | renders the scene, `glReadPixels` | renders nothing, reads nothing: the next login screen would show a blank picture | **Skipped** (the logout camera position is still saved) |
| `idleShutdown()` during a teleport, or once disconnected | none | `idle()` returns before it: the teleport ends only through `display()`, the disconnect notice only through the user | **`doFrame` drives it** once the context is lost; disconnected, it quits as that notice's Quit button does |
| waits for uploads (≤ 5 s) and for the `LogoutReply` (≤ 6 s) | none | | none (already bounded) |
| `cleanup()`: `shutdownViews`, `shutdownGL` (`glDelete*`, `glFinish`), the worker's join | deletes, `glFinish` | no-ops, no wait | none |
| window destruction | `wglDeleteContext` / `SDL_GL_DeleteContext` | window-system calls, valid on a lost context | none |
| development checks (`RenderDebugGL`, `DebugSession`) | `stop_glerror` halts on any GL error; the debug callback logs and halts on high severity | `CONTEXT_LOST` on every call: a halt, or thousands of log lines | **Both stand down** once the context is lost (`assert_glerror`, `gl_debug_callback`) |

No shipping code maps a buffer (`glMapBuffer*` are loaded but never called), so no NULL mapped pointer can be written
through. `LastGPUString` and the rest of the graphics identity are read from the healthy context at start-up. They are
stored early in startup, never at shutdown, so a reset cannot change what the next start compares.

**Threads.** The check runs on the main thread only. The kept reset is an atomic, because the GL debug checks also run
on the worker thread. No thread is killed or abandoned (see the clean-shutdown premise). The worker's waits return on a
lost context, and it is closed and joined in `cleanup()` like any other close.

## Cost

- **Per frame:** one `glGetGraphicsResetStatus` (a status query, with no flush and no wait), and nothing more once a
  reset is seen. **Not measured:** no viewer was built for this change; the frame-time comparison belongs to the
  qualification build.
- **Robust buffer access:** not requested, so shaders and buffers get no bounds checks.
- **Context creation:** one attribute pair, plus one extra creation attempt per version on a driver that refuses the
  strategy.

## Platforms

| Platform | Reset notification | Notes |
|---|---|---|
| Windows (NVIDIA, AMD, Intel) | WGL; the drivers list `WGL_ARB_create_context_robustness` | TDR after 2 s by default |
| Linux, X11/GLX (Mesa radeonsi/iris, NVIDIA) | GLX through SDL2's attribute path | amdgpu and i915 reset after their timeouts; NVIDIA on Linux rarely resets a hung GPU |
| Linux, native Wayland/EGL | not requested (SDL 2.28's EGL path ignores it) | unchanged from before |
| macOS | not built | unchanged |

## Testing

- **Unit test:** `indra/llrender/tests/llglreset_test.cpp` (standalone, run in ci-checks) checks four things: the four
  values the specification allows, any other value as an unknown reset, the latch (the first reset is kept through
  `NO_ERROR` and through later resets of another kind), and a frame-by-frame sequence. The GL values in
  `llglreset.h` are checked against the GL headers by `static_assert` in `llgl.cpp`.
- **Development hook (never in release or CI builds):** configure with `-DVKS_GPU_RESET_TEST:BOOL=ON`. The debug
  setting `RenderGPUResetTest` (not persisted) runs one test at the next frame and returns to 0:
  - `1` hangs the GPU with one draw: an 8 × 8 viewport whose fragment shader runs 2^31 dependent steps per pixel,
    about 10 to 30 s. The driver resets the GPU, and the reset is the viewer's (guilty): after about 2 s on Windows,
    and on Linux where the driver has a GPU timeout. Without one (Linux NVIDIA) the screen stalls until the draw ends,
    and nothing is reset. Windows bug-checks after 5 TDRs within a minute (`TdrLimitCount`), so do not repeat this
    test quickly.
  - `2`, `3` and `4` simulate a guilty, an innocent and an unknown reset without touching the GPU. They show both
    message texts and run the quit on a live context.
  - Expected: the log line `GPU reset: the driver reports a ... context reset`, the box, the logout, a clean exit,
    and an ordinary next start (no "crashed last time" notice, settings as they were).
- **Release login test (owner-assisted):** the change touches shutdown and context creation. Per AGENTS.md it needs a
  Release build that reaches the world before it is called safe. The log must show `GPU reset notification: on`, and
  quitting must work as before.

## Release integration

The detection and the handling ship. The test hook stays on `vkstorm-devel`. An integration into release leaves out the
following:
- the `VKS_GPU_RESET_TEST` option in `indra/cmake/00-Common.cmake` and its block in `indra/newview/CMakeLists.txt`;
- `indra/newview/llgpuresettest.cpp` and `.h`;
- the `RenderGPUResetTest` setting;
- the two `#if VKS_GPU_RESET_TEST` blocks in `llappviewer.cpp` (the include, and the call in `checkGPUReset`).

These names are in the forbidden list of `scripts/tests/check_release_hooks.py`. As with `VKS_GL_STATS`, an integration
also leaves out the names themselves.
