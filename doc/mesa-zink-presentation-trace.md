# Mesa/Zink presentation trace, 2026-09-22

Read-only investigation with viewer closed. Viewer revision fcaf5d5101; tested
Mesa revision 00e42c51b10d8e0769489156fa414f111897d515. No performance patch applied.

## Revision discipline

The local third-party Mesa source checkout is at 3e2092a295 and has existing
modifications. It is not the tested DLL revision. Source below was read using
`git show 00e42c51b1:<path>` from C:/Dev/3p-mesazink/mesa-src; no checkout or
patch modification occurred. The original package recipe's null-guard patch
does not modify WGL, Kopper or st_manager. Runtime captures identify 00e42c51b1.
This traces corresponding source, not a disassembly proof of installed code.

## Confirmed source path

- Viewer `indra/newview/llappviewerwin32.cpp:1220` selects the bundled Mesa DLLs
  or System32 OpenGL. Both backends retain the GL window path.
- `indra/llwindow/llwindowwin32.cpp:2030` uses wglSwapIntervalEXT(0) for disabled
  VSync. `:3980` calls SwapBuffers(mhDC) for both; the early exit belongs to
  native Vulkan, not Zink. No Zink-specific layered-window style was found.
- Mesa `src/gallium/frontends/wgl/stw_st.c:136` passes the existing HWND to
  Kopper via VkWin32SurfaceCreateInfoKHR. `zink_kopper.c:118` creates that surface,
  `:306` sets swapchain presentMode, and `:787` calls vkQueuePresentKHR.
- At `zink_kopper.c:34`, Windows unconditionally selects
  VK_PRESENT_MODE_IMMEDIATE_KHR. This is not a FIFO selection masquerading as
  uncapped operation. Vulkan present mode is distinct from Windows independent
  versus composed flip; IMMEDIATE does not prove independent-flip eligibility.
- Mesa `src/gallium/targets/wgl/wgl.c:224` returns a winsys framebuffer only for
  D3D12, not Zink. `stw_st.c:430` therefore adds ST_FLUSH_WAIT on end-of-frame;
  `src/mesa/state_tracker/st_manager.c:811` waits using fence_finish with
  OS_TIMEOUT_INFINITE. This establishes a synchronization point, not its cost.
  It precedes the low-level present and is not measured by the CSV's DXGI
  msInPresentAPI alone. Archived rejected changes targeted this area: do not
  transplant them or remove this wait without lifetime analysis and validation.

## Saved telemetry changes the hypothesis

Recomputed directly from existing CSVs using numeric medians, without new runs:

| Median metric (ms) | native-midday-01 | zink-midday-01 |
|---|---:|---:|
| msBetweenPresents | 29.116 | 49.586 |
| msInPresentAPI | 0.074 | 0.071 |
| msGPUActive | 10.132 | 36.575 |
| msUntilRenderComplete | 0.967 | 0.124 |
| msUntilDisplayed | 0.967 | 6.432 |

Both report SyncInterval=0 and PresentFlags=512 for every row. Native is
Independent Flip; Zink is Composed Flip. The GPU-active metric is indicative,
not qualified per-pass GPU time. PresentMon documents GPU timing limitations
with hardware-accelerated scheduling; its effective state was not established.
Do not subtract these medians to manufacture a CPU/GPU frame decomposition.
Both CSVs report Runtime=DXGI, which does not change the verified GL backends.

Crucially, earlier zink-01 contains both modes in the same process/run:

| Mode | Samples | Median interval ms | Median GPU-active ms | Median until displayed ms |
|---|---:|---:|---:|---:|
| Independent Flip | 2042 | 46.076 | 34.847 | 0.234 |
| Composed Flip | 470 | 45.801 | 34.838 | 6.669 |

These were not randomized mode-control trials, but they demonstrate that Zink
can reach independent flip and weaken the claim that composition alone explains
the throughput deficit. They show a display-latency difference without a similar
median present-interval difference. Why the later session stayed composed is
still unknown. Windows composition state depends on driver/OS, surface and
desktop conditions; neither screenshot endpoints nor mode labels establish
continuous foreground state.

## Separate correctness finding: uninitialized loader metadata

At tested Mesa `stw_st.c:269`, `struct kopper_loader_info loader_info;` is a
stack object without initialization. `stw_st_fill_private_loader_data()` fills
the Win32 surface fields and has_alpha, but not initial_swap_interval,
present_opaque or compression (see include/kopper_interface.h).
`zink_kopper.c:470` copies the structure. `:301` consults present_opaque when
premultiplied alpha is supported and has_alpha is true. This is an indeterminate
field read on that conditional path. Actual supported alpha modes and selected
compositeAlpha were not captured, so no symptom or performance attribution is
established. Windows' hardcoded IMMEDIATE mode makes initial_swap_interval
irrelevant to that mode-selection branch, but it still should be initialized.

A separate minimal correctness change should initialize the structure and
explicitly populate its intended fields. Do not bundle a forced opaque policy,
changed present mode, or synchronization rewrite into that correction.

## Next measurement, before optimization

1. Record actual Vulkan swapchain format, extent, image count, presentMode and
   compositeAlpha, plus effective GPU-scheduling/display configuration, in a
   separate diagnostic run. Do not infer these from the renderer dropdown.
2. Get a sampled CPU/GPU timeline separating viewer work, WGL end-of-frame wait,
   Zink submission and GPU execution. The full SwapBuffers call needs attribution;
   the observed DXGI Present call is not its substitute.
3. Validate the large GPU-active difference using GPU pass timings under the same
   fixed lighting; prior single-frame shader profiles used different lighting.
   Shadows, terrain, reflection updates and alpha/PPLL need individual costs.
4. Repeat matched baseline captures and qualify collector overhead. Add a
   separate hero-probe scene and visual recordings. No visual acceptance follows
   from these timing tables.

## External references

- [Microsoft flip-model presentation](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model)
- [PresentMon metric definitions](https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md)
- [PresentMon timing limitations](https://github.com/GameTechDev/PresentMon#tracking-gpu-work-with-hardware-accelerated-gpu-scheduling-enabled)

Local source references above are pinned Git-object anchors; the current Mesa
working files are a different revision and must not be used as exact substitutes.
