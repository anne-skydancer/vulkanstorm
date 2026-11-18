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
     there is also `Created OpenGL 4.6 ... context with GPU reset notification.` Every log line about GPU resets
     carries the tag `GPUReset` (the development hook's carry `GPUResetTest`).
2. **One check per frame.** `LLAppViewer::checkGPUReset()` runs once a frame on the main thread, before the frame's GL
   work. It calls `glGetGraphicsResetStatus` only while notification is on and no reset has been seen. The first reset
   is kept (`LLGLReset::latch`): the GL reports a reset until the driver has completed it and `NO_ERROR` after that,
   but the context stays lost.
3. **On the first reset (guilty, innocent or unknown):**
   - **The log.** The first line written is the reset with its kind, the raw status, the cause, `GL_RENDERER` and
     `GL_VERSION`. The log flushes every line, so it is written before anything else runs on the lost context.
   - **An OS message box** (GL is dead, so XUI can't be drawn), titled "Graphics Reset". For a guilty reset (the
     main context's own GPU work caused it: a timeout or a GPU fault) it says "The graphics card was overloaded and
     has been reset." For an innocent or unknown reset it says "The graphics driver was reset." The rest of the text
     is the same: the viewer can no longer draw, it will now close, and the settings have not been changed. The
     mainloop watchdog is paused while the box waits for the user. A non-interactive run has no one to answer it, so
     there the reset is only logged.
   - **Guilt is per context.** The viewer asks its main context. A reset caused by the texture thread's context
     reports innocent there, so it gets the "driver was reset" text; the log line's cause lists the texture thread
     among the possible causes.
   - **The normal quit:** `requestQuit()`, then the logout, then `cleanup()`, which saves the settings and closes
     everything. A quit already under way when the reset comes is carried on, not requested again. Only the parts
     that need drawing or a visible dialog are left out (below), and the quit can no longer be cancelled.
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
| `LLImageGLThread` (worker, shared context) | uploads, `glFenceSync` + `glClientWaitSync(GL_TIMEOUT_IGNORED)` (NVIDIA) | no-ops; a wait issued after the reset **returns at once** (the specification). A wait that is already blocked when the GPU hangs ends when the driver resets the GPU and ends the hung work; that has not been run yet (hook mode 1). Where the driver never resets the GPU (Linux NVIDIA), it waits as long as the GPU hangs | none; joined in `cleanup()` as before |
| `requestQuit()` | none (floaters asked to close, metrics) | | none; not called again when a quit is already under way |
| `idleShutdown()`: waits for modal dialogs and for floaters that ask "save changes?" | none, but no one can see or answer a dialog | would wait for ever | **Not waited for** once the context is lost; each floater is still asked to close once. Unsaved edits in open editors (notecards, scripts, gestures) are lost, as in a crash |
| `abortQuit()`: the Cancel of a "save changes?" dialog | none | input still reaches the invisible dialogs: Escape, or a click where Cancel would be, would cancel the quit and leave a window with nothing on it | **Ignored** once the context is lost: the quit cannot be cancelled |
| `userQuit()`: the window's close button, File > Quit | none | would show a "ConfirmQuit" no one can see, or request the quit again and restart the logout timeout at every click | **Ignored** once the context is lost: the quit is already under way |
| the main loop's pace | the buffer swap in `display()` (vsync), the frame limiter | `display()` is skipped and the limiter stops once the logout is sent, so the loop would spin a core until the viewer has closed (up to the 5 s and 6 s waits below) | **10 ms sleep** per frame once the context is lost (`doFrame`) |
| `saveFinalSnapshot()` (`idleShutdown`, the exit block of `doFrame`, `gDoDisconnect`) | renders the scene, `glReadPixels` | renders nothing, reads nothing: the next login screen would show a blank picture | **Skipped** (the logout camera position is still saved) |
| `idleShutdown()` during a teleport, or once disconnected | none | `idle()` returns before it: the teleport ends only through `display()`, the disconnect notice only through the user | **`doFrame` drives it** once the context is lost; disconnected, it quits as that notice's Quit button does |
| waits for uploads (≤ 5 s) and for the `LogoutReply` (≤ 6 s) | none | | none (already bounded) |
| `cleanup()`: `shutdownViews`, `shutdownGL` (`glDelete*`, `glFinish`), the worker's join | deletes, `glFinish` | no-ops, no wait | none |
| window destruction | `wglDeleteContext` / `SDL_GL_DeleteContext` | window-system calls, valid on a lost context | none |
| development checks (`RenderDebugGL`, `DebugSession`) | `stop_glerror` halts on any GL error; the debug callback logs and halts on high severity; the state checks (`LLGLState::checkStates` and its destructor, `LLGLDepthTest::checkState`, `getUniformLocation`, `check_framebuffer_status`) query the GL and halt or log when it disagrees | `CONTEXT_LOST` on every call and no query answered: a halt, or thousands of log lines | **All stand down**: once the reset is seen, and in the frame of the reset before that, when the GL reports `CONTEXT_LOST` (`gl_context_lost()`, which reads `glGetError` on the calling thread's context and leaves the reset status to the per-frame check). The debug callback may not call the GL, so it recognises the message instead: NVIDIA's id is the error enum, and the other vendors' texts are expected to name `CONTEXT_LOST` (not seen on hardware yet) |

No shipping code maps a buffer (`glMapBuffer*` are loaded but never called), so no NULL mapped pointer can be written
through. `LastGPUString` and the rest of the graphics identity are read from the healthy context at start-up. They are
stored early in startup, never at shutdown, so a reset cannot change what the next start compares.

**Threads.** The check runs on the main thread only. The kept reset is an atomic, because the GL debug checks also run
on the worker thread; `gl_context_lost()` reads only the calling thread's own GL error. No thread is killed or
abandoned (see the clean-shutdown premise). The worker's waits issued after the reset return at once, one already
blocked ends with the driver's reset (see the table), and the worker is closed and joined in `cleanup()` like any other
close.

## Cost

- **Per frame:** one `glGetGraphicsResetStatus` (a status query, with no flush and no wait), and nothing more once a
  reset is seen. Measured in a standalone program on a context made as the viewer makes it (4.6 compatibility,
  `LOSE_CONTEXT_ON_RESET`; RTX 5070 Ti, driver 621.14): about 1.0 µs a call back to back (median of 21 × 100 000
  calls), and 1.6 µs with a 21.8 ms draw in flight. The query does not wait for the GPU: the `glFinish` after it still
  waited the whole 21.8 ms. That is about 0.01 % of a 16.7 ms frame. **Not measured:** the frame time inside the
  viewer; no fps effect is expected.
- **Debug checks:** `gl_context_lost()` runs only when a check has already failed, so a healthy session pays nothing.
- **After a reset:** a 10 ms sleep per frame until the viewer has closed (nothing is drawn by then).
- **Robust buffer access:** not requested, so shaders and buffers get no bounds checks.
- **Context creation:** one attribute pair, plus one extra creation attempt per version on a driver that refuses the
  strategy.

## Platforms

| Platform | Reset notification | Notes |
|---|---|---|
| Windows (NVIDIA, AMD, Intel) | WGL; the drivers list `WGL_ARB_create_context_robustness` | TDR after 2 s by default. Compiled and built; the strategy is confirmed on NVIDIA (RTX 5070 Ti, standalone program) |
| Linux, X11/GLX (Mesa radeonsi/iris, NVIDIA) | GLX through SDL2's attribute path | amdgpu and i915 reset after their timeouts; NVIDIA on Linux rarely resets a hung GPU. **Not compiled or run yet:** the PR's Linux CI build is the first compile, and every X11 user moves from `glXCreateContext` to `glXCreateContextAttribsARB`, so Linux needs its own login check |
| Linux, native Wayland/EGL | not requested (SDL 2.28's EGL path ignores it) | unchanged from before |
| macOS | not built | unchanged |

## Testing

- **Unit test:** `indra/llrender/tests/llglreset_test.cpp` (standalone, run in ci-checks) checks four things: the four
  values the specification allows, any other value as an unknown reset, the latch (the first reset is kept through
  `NO_ERROR` and through later resets of another kind), and a frame-by-frame sequence. The GL values in
  `llglreset.h` are checked against the GL headers by `static_assert` in `llgl.cpp`.
- **Development hook (never in release or CI builds):** configure with `-DVKS_GPU_RESET_TEST:BOOL=ON`. The debug
  setting `RenderGPUResetTest` (not persisted) runs one test at the next frame and returns to 0:
  - `1` hangs the GPU with one draw: an 8 × 8 viewport whose fragment shader runs 2^32 − 1 dependent steps per
    pixel. The driver resets the GPU, and the reset is the viewer's (guilty): after about 2 s on Windows, and on Linux
    where the driver has a GPU timeout (amdgpu 10 s). Without one (Linux NVIDIA) the screen stalls until the draw
    ends, and nothing is reset. Windows bug-checks after 5 TDRs within a minute (`TdrLimitCount`), so do not repeat
    this test quickly.
  - How long the draw is, measured with short draws (under 0.25 s, far below the 2 s timeout) and scaled up to the
    hook's step count:
    - the hook's own shader and viewport in OpenGL, RTX 5070 Ti, driver 621.14: 9.7 ns a step, so **42 s**;
    - the same loop as a Vulkan compute twin, read with the shader's cycle counter: 26.9 cycles a step on the RTX
      5070 Ti (at 2.7 to 2.9 GHz), and 29.0 cycles on an AMD RDNA2 GPU (an integrated GPU at 2.2 GHz: 59 s). The AMD
      driver's own disassembly shows the loop at its minimum: `v_fmaak_f32`, `v_fract_f32` and four scalar
      instructions, as wave64.
    - The step count needs margin for the fastest GPUs: at 3 GHz, RDNA2's 29 cycles give 41 s for 2^32 − 1 steps,
      but only 21 s for 2^31. Newer AMD GPUs may need fewer cycles a step (RDNA3 runs many wave64 instructions in one
      pass). At 2^31 steps, half the cycles would leave the draw near amdgpu's 10 s, so that it could end before the
      reset; at 2^32 − 1 steps they still give about 21 s at 3 GHz. RDNA3 and RDNA4 were not measured.
  - `2`, `3` and `4` simulate a guilty, an innocent and an unknown reset without touching the GPU. They show both
    message texts and run the quit on a live context.
  - Expected: the log line `GPU reset: the driver reports a ... context reset`, the box, the logout, a clean exit,
    and an ordinary next start (no "crashed last time" notice, settings as they were).
- **Release login test (owner-assisted):** the change touches shutdown and context creation. Per AGENTS.md it needs a
  Release build that reaches the world before it is called safe. The log must show `GPU reset notification: on`, and
  quitting must work as before. Linux needs its own login check (see Platforms).
- **What has run:** the unit test (g++, clang and MSVC); the RelWithDebInfo viewer build on Windows with the hook off;
  a compile of the hook's files on Windows with the hook on; the query cost and the hook draw's length above.
- **What has not run yet:** any viewer with a GPU reset, real or simulated (hook modes 1 to 4); the Linux compile and
  run; the Release login tests on Windows and Linux.

## Release integration

The detection and the handling ship. The test hook stays on `vkstorm-devel`. An integration into release leaves out the
following:
- the `VKS_GPU_RESET_TEST` option in `indra/cmake/00-Common.cmake` and its block in `indra/newview/CMakeLists.txt`;
- `indra/newview/llgpuresettest.cpp` and `.h`;
- the `RenderGPUResetTest` setting;
- the two `#if VKS_GPU_RESET_TEST` blocks in `llappviewer.cpp` (the include, and the call in `checkGPUReset`).

These names are in the forbidden list of `scripts/tests/check_release_hooks.py`. As with `VKS_GL_STATS`, an integration
also leaves out the names themselves.
