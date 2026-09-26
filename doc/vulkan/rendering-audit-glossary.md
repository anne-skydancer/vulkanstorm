# Rendering audit glossary

> Current implementation contract: Windows/Linux OpenGL **>=4.3 Core**.
> Both OIT paths remain for now. Depth peeling will be deprecated before it is
> retired; PPLL is the long-term sole OIT target. Its current 4.4-dependent calls
> need a qualified 4.3 implementation before that transition is complete.


A shared legend for the [targeted rendering audit](rendering-pipeline-targeted-audit.md)
and the [Core/Mesa/Zink audit](gl-core-zink-audit.md). Definitions describe how the
terms are used in these reports. Some entries are names or technical shorthand,
rather than acronyms.

## Graphics APIs and driver components

| Term | Expansion or name | Meaning in the reports |
|---|---|---|
| API | Application Programming Interface | The functions and rules through which the viewer asks a library or driver to do work. |
| GL / OpenGL | Open Graphics Library | The graphics API used by the renderer being audited. `GL` is its common abbreviation and code prefix. |
| Core profile | OpenGL Core profile | The modern OpenGL feature set, excluding removed legacy facilities. A Core version is a required capability level, not a performance rating. |
| Compatibility profile | OpenGL compatibility profile | A profile retaining older graphics facilities alongside newer ones. The proposed renderer will stop supporting this profile. |
| GLSL | OpenGL Shading Language | The language of OpenGL GPU programs. `GLSL 430` means language version 4.30, associated with OpenGL 4.3. |
| OpenCL / CL | Open Computing Language | A compute API used by the audited OpenJPEG backend for accelerated texture decoding. It is distinct from OpenGL compute shaders. |
| Vulkan | API name, not an acronym | The lower-level graphics/compute API targeted by Zink. Using OpenGL through Zink differs from implementing a native Vulkan renderer. |
| Mesa | Project name, not an acronym | A collection of graphics implementations and driver infrastructure. The viewer bundles a particular Mesa build for its Zink path. |
| Gallium | Mesa driver framework | The common interface between parts of Mesa and drivers such as Zink. |
| Zink | Driver name, not an acronym | Mesa's driver that implements OpenGL over Vulkan. |
| Kopper | Mesa component name | Infrastructure involved in presenting Zink-rendered images to a window. |
| WGL | Windows OpenGL interface | Windows-specific facilities for creating and managing OpenGL contexts and their connection to windows. |
| GLX | OpenGL Extension to the X Window System | The interface used to connect OpenGL to X11 windowing on the audited Linux path. |
| EGL | Khronos interface name | Another interface for managing graphics contexts and drawing surfaces. It is not the selected windowing route in the audited Linux Zink setup. |
| GLES / OpenGL ES | OpenGL for Embedded Systems | A related graphics API family; not the desktop OpenGL target of these audits. |
| GLVND | GL Vendor-Neutral Dispatch | Linux infrastructure that routes OpenGL calls to the selected vendor implementation. |
| ICD | Installable Client Driver | The driver implementation selected by a graphics loader. Here, especially the underlying Vulkan driver used by Zink. |
| SDL2 | Simple DirectMedia Layer, version 2 | The library used for Linux window/context setup in the audited viewer. |
| X11 | X Window System, version 11 | The Linux window-system interface selected by that setup. |
| `GL_EXT_…` / `GL_ARB_…` | OpenGL extension-name prefixes | Named capabilities that may be available separately from a core version. `ARB` means Architecture Review Board; `EXT` identifies an extension namespace. Availability must be checked. |

## Rendering and GPU data

| Term | Expansion or name | Meaning in the reports |
|---|---|---|
| CPU | Central Processing Unit | Runs scene management, geometry preparation, scheduling and graphics submission. |
| GPU | Graphics Processing Unit | Runs shaders, rasterization and applicable compute work. |
| VRAM | Video Random-Access Memory | Memory used by the graphics device for textures, geometry and other resources; on discrete cards, normally dedicated graphics memory. |
| VBO | Vertex Buffer Object | An OpenGL buffer used for vertex attributes such as positions, normals and texture coordinates. |
| VAO | Vertex Array Object | An OpenGL object describing vertex-input configuration and associated buffer bindings. It is not itself the vertex-data allocation. |
| FBO | Framebuffer Object | An OpenGL object that groups the color/depth attachments into which a rendering pass draws. |
| SSBO | Shader Storage Buffer Object | A buffer that shaders can read and write, suitable for object metadata, computed results and other structured data. |
| MDI | Multi-Draw Indirect | Submits multiple draws through one API call, with draw parameters stored in a buffer. Compute can produce those parameters. The current one-command use does not achieve that batching benefit. |
| LOD | Level of Detail | A choice among mesh representations with different detail. Selecting existing LODs is different from generating simpler geometry. |
| PBR | Physically Based Rendering | Material/shading techniques using properties such as roughness and metallicity. Also identifies a material path in the viewer. |
| HDR | High Dynamic Range | Representation/processing of brightness over a wider range than the final conventional display image. |
| OIT | Order-Independent Transparency | A family of techniques intended to handle overlapping transparent surfaces without relying on ordinary object-level draw sorting. |
| PPLL | Per-Pixel Linked List | An OIT technique that stores lists of transparent fragments per pixel, then resolves them. Long-term sole OIT target. The current implementation remains gated at 4.4 while the renderer baseline is 4.3. |
| Depth peeling | Transparency technique | Uses successive rendering passes to separate transparent depth layers. Retained for now; deprecation comes before eventual retirement in favor of PPLL. |
| HUD | Heads-Up Display | Viewer overlays and HUD-attached content, with rendering rules distinct from ordinary world geometry. |
| UI | User Interface | Menus, panels, text and other interaction controls. |
| UV / UVs | Texture-coordinate axes | Coordinates locating a point in a texture. U and V are axis labels, not an acronym. |
| glTF / GLTF | GL Transmission Format | A 3D asset format. The code/reports use this label for relevant material and scene-asset paths; those paths are not necessarily interchangeable. |
| U16 | Unsigned 16-bit integer | A value in the range 0–65,535, relevant to the existing index/offset layouts. The code's packing cap is an additional implementation constraint. |
| Alpha | Opacity/coverage component | Used in blending and transparency. Alpha blending and alpha masking have different rendering requirements. |
| Rasterization | Rendering stage | Converts primitives, usually triangles, into fragments for pixel processing. It already runs on the GPU. |
| Compute shader | General-purpose GPU shader | Runs explicitly dispatched work rather than being invoked through an ordinary draw. Proposed for culling, LOD and command generation. |
| Skinning / rigging | Skeletal mesh deformation | Moves mesh vertices using joint transformations and weights. Ordinary rigged drawing already performs skinning in vertex shaders. |
| Skin palette | Joint-matrix collection | The matrices a shader uses to deform a particular mesh for an avatar's current pose. |
| Morph | Vertex-shape adjustment | Applies weighted changes to mesh vertices, for example for appearance or expressions. |
| Frustum culling | View-volume rejection | Rejects objects outside a camera's visible volume before drawing them. |
| Occlusion culling | Hidden-object rejection | Rejects objects hidden behind other geometry. Requires more information than frustum culling. |
| Hi-Z / depth hierarchy | Hierarchical depth | A multi-resolution depth representation that can support conservative GPU occlusion tests. |
| Picking | Interaction geometry query | Determines which object or surface the user points at or selects. This is one reason some CPU geometry remains useful. |

## Memory, synchronization and performance

| Term | Expansion or name | Meaning in the reports |
|---|---|---|
| Resident geometry | Geometry kept in GPU-accessible storage | Reuses uploaded mesh data across frames rather than repeatedly rebuilding/copying it. |
| CPU mirror / shadow | CPU-side data copy | A copy supporting CPU readers or buffer updates. Removing it requires identifying all its consumers. |
| Persistent mapping | Long-lived CPU mapping of a GPU buffer | An upload/access technique. It is separate from keeping geometry resident. |
| Barrier | GPU memory-ordering operation | Makes earlier writes usable by the specified later GPU consumers. It does not by itself establish that the CPU can safely reuse storage. |
| Fence | Completion marker | Lets another context or the CPU establish when preceding GPU work has completed. |
| Readback | GPU-to-CPU transfer | Fetches results into CPU-visible memory. Waiting immediately for them can introduce stalls. |
| Dispatch | Compute-work submission | Launches a compute shader over groups of work items. |
| Descriptor | GPU-resource description | Information used to locate resources such as buffers and textures. Zink maps OpenGL resource state into Vulkan resource bindings. |
| `db` / `lazy` | Zink descriptor-mode names | `db` selects the descriptor-buffer mode; `lazy` names an alternative mode. These are configuration values, not viewer feature levels. |
| Draw call | Rendering submission | A request to draw geometry. Many small calls can consume CPU/driver time even when GPU work is modest. |
| Batch | Compatible work grouped together | Multiple draws or uploads submitted together to reduce overhead. Material and state differences can prevent grouping. |
| Frame time / frame tail | Frame duration / slower end of its distribution | Low typical time gives throughput; unusually slow frames cause visible hitches. |
| p50 / p95 / p99 | 50th / 95th / 99th percentile | For measured frame durations, that percentage of samples is at or below the value. p50 is the median; p99 exposes the slow tail. Lower is better. |
| FPS | Frames Per Second | Rendering throughput. Frame-time percentiles describe uneven pacing more clearly than average FPS alone. |
| Vsync | Vertical synchronization | Coordinates presentation with display refresh; it can affect measured frame pacing and waits. |
| A/B | Comparison of alternatives A and B | Controlled measurements changing one policy or implementation while keeping relevant conditions equivalent. |
| KiB / MiB | Kibibyte / mebibyte | Binary memory units: 1 KiB = 1,024 bytes; 1 MiB = 1,048,576 bytes. |
| ms | Millisecond | One thousandth of a second. |
| Soft 60:40 allocation | Proposed service-budget policy | A rendering/decode scheduling target under contention, allowing unused capacity to be borrowed. It is not a fixed hardware partition. |

## Texture decoding, builds and report labels

| Term | Expansion or name | Meaning in the reports |
|---|---|---|
| JPEG / J2C | Joint Photographic Experts Group / JPEG 2000 codestream shorthand | J2C here identifies JPEG 2000 compressed texture data, not ordinary JPEG image data. |
| OpenJPEG / OPJ | JPEG 2000 codec project / code prefix | The decoder package examined for the OpenCL workload. `OPJ_…` names its settings and related identifiers. |
| Codec | Coder/decoder | Software that compresses and/or decompresses a representation, here texture images. |
| CI | Continuous Integration | Automated build and validation jobs; the reports refer to the project's release build configuration and artifacts. |
| DLL | Dynamic-Link Library | A Windows shared-library file, such as a packaged graphics implementation. |
| `.so` | Shared-object filename suffix | A Linux shared-library file. |
| LTO | Link-Time Optimization | Compiler optimization spanning separately compiled code at link time. A benchmark candidate for the Mesa package. |
| LLVM | Compiler-infrastructure project name | A build dependency/configuration item discussed in the Mesa audit; not a viewer graphics API. |
| SHA | Secure Hash Algorithm | In “source SHA,” shorthand for the Git commit identifier that pins the audited source. A package archive checksum identifies a different artifact. |
| XML | Extensible Markup Language | The format used by the cited viewer settings file. |
| ID | Identifier | A value identifying an object, material, draw or other resource. It needs a defined lifetime when resources are reused. |
| `LL…` | Linden Lab code-name prefix | A historical naming convention for many classes in the inherited viewer code, such as `LLFace`; it does not identify a graphics capability. |
| `FS…` | Firestorm code/settings prefix | A naming convention inherited by settings such as `FSImageDecodeThreads`. The effective setting must be checked in this fork. |
| `3p-…` | Third-party package prefix | Repositories/packages supplying dependencies, such as `3p-mesazink` and `3p-openjpeg`. |
| R1–R4 | Rendering findings 1–4 | Local labels in the targeted audit: context contract, physics-debug client arrays, render-target allocation, and redundant submission/upload work. |
| M1–M6 | Mesh stages 1–6 | Local labels in the broader audit: residency, visibility/LOD, batching, coverage, geometry/deformation, and occlusion/scheduling. |

Names such as AMD, NVIDIA, Intel, Mesa and Zink identify hardware vendors or
projects; they do not imply a particular capability or performance result. Code
identifiers in backticks are exact names to search for, not additional acronyms
that must be memorized.
