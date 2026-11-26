<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# The C++ cleanup on release-clean

`release-clean` was cut from the tip of `vkstorm-release` (537be74479) on 2026-11-25. It is a work branch: the base of
the clean C++ code that the later Rust rewrite starts from. Pushes to it build and publish nothing; pull requests into it
get the full CI.
No `<Vulkanstorm>` markers and no upstream merges. `release-clean` is the clean base of the Rust port, not a product line:
cleanup commits do not carry per-line `<Vulkanstorm>` markers (the commit messages say what changed and why), and the branch
takes no merges from upstream Firestorm or Linden Lab. Linden Lab and Firestorm licence headers stay unchanged. Markers
that earlier hotfixes left in the code are kept as they are.

## One branch per subsystem, one at a time

Each subsystem gets its own branch `clean/<subsystem>`, cut from the tip of `release-clean`, and lands in `release-clean`
through its pull request before the next branch is cut. The subsystems depend on one another in one direction (everything
uses `llcommon`, most use `llmath`, `newview` uses all), so a later branch always starts from cleaned foundations and no
two cleanup branches change the same interfaces at once.

## Two pull requests per subsystem (from `llmath` on, decided 2026-11-26)

Every subsystem gets the same modernisation, style changes included, so the whole tree reads one way. The changes are
split by what they can do to behaviour, so that review effort goes where behaviour can change:

1. **Bugs and safety.** The defect fixes, the sanitizer and analyser findings, and every fix-it family that can change
   what the code does: `override`, `= delete`/`= default`, by-value and `const&` parameters, scoped locks, `avoid-bind`,
   `pass-by-value`, `use-emplace`, `loop-convert`, `use-ranges`, `use-default-member-init`, and hand edits. This pull
   request is read line by line by the auditor.
2. **Style.** The families that only change how the code is written: `use-auto`, `use-using`, `use-nullptr`,
   `return-braced-init-list`, `concat-nested-namespaces`, `use-designated-initializers` and the like, one commit per
   family. Each commit is proven by identical compiled code: the analysis CI builds the subsystem before and after the
   commit and compares the machine code (`doc/clean/analysis.md`, "Identical compiled code"). A style commit's subject
   ends with the tag `(style)`, for example `llmath: modernize-use-auto (style)`; an untagged commit is not checked. A style commit whose code differs moves to the first pull request and gets a
   full read; the auditor only samples the style pull request for readability.

Both pull requests come from `clean/<subsystem>`; the second is cut after the first has landed. `llcommon` (#612, #613)
predates this rule and mixed the two.

## Coroutines (decided 2026-11-26)

Boost.Fiber and Boost.Context leave the C++: an in-house C++20 task type and main-thread executor replace `LLCoros`'
fibers, every caller is converted, and the fiber-aware locks become `std::` ones. The work has its own branch,
`clean/coroutines`, cut after `clean/llcommon` lands: the new core sits beside the fibers, the callers move library by
library (`llmessage`, login, `newview`) in commits that each build and pass CI, and the fibers go at the end. The run
order stays as it is today with one deliberate change (decision 1 of the survey plan): a coroutine that launches
another no longer waits behind it in Boost's queue, so the run-order oracle's scenarios 3 and 4 differ on the new
executor. `HttpCoroutineAdapter` stays with `co_await` calls.

## Order (bottom-up)

1. Foundations: `llcommon`, then `llmath`.
2. Data and I/O: `llfilesystem`, `llimage` with the J2C back ends (`llimagej2coj`, `llimagej2cneo`), `llxml`,
   `llinventory`, `llprimitive`.
3. Network: `llcorehttp`, then `llmessage`.
4. Platform and rendering: `llwindow`, then `llrender`.
5. Characters and audio: `llcharacter`, `llappearance`, `llaudio`.
6. The UI toolkit: `llui`.
7. `newview` in slices, one branch per area (`clean/newview-<area>`): the render pipeline and draw pools; objects, prims,
   mesh and physics; avatars and animation; textures and images; light, shadows, sky and water; inventory and assets;
   chat, IM, voice and groups; agent, world, regions, network and login; the app shell (startup, settings, menus);
   floaters and panels; audio, media and web; RLV/RLVa; the Firestorm and contributor features.
8. The rest: `llplugin` and `media_plugins`, the crash loggers, `viewer_components`, `llwebrtc`, `llmeshoptimizer`,
   `llphysicsextensionsos`, `llkdu`, the test framework.

## Inventory (`vkstorm-devel` tip, 2026-11-25)

1,604 `.cpp` files, 178 of them tests: `newview` 922, `llcommon` 148, `llui` 112, `llmessage` 85, `llmath` 51, `test` 31,
`llcorehttp` 29, `llrender` 28, `media_plugins` 22, `llinventory` 22, `llcharacter` 21, `llprimitive` 20, `llwindow` 19,
`llappearance` 16, `llimage` 14, `llaudio` 12, `llfilesystem` 10, `llplugin` 8, the rest 32.
