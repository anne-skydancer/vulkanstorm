# Shader, material, and lighting contracts

## Provenance and scope

Inspected worktree: `anne-skydancer/vulkanstorm`; `git rev-parse HEAD` returned `59108e15a1f8f94d2da7c674d937d19f5cf9450d`. The repository was clean when checked. This is a read-only source reconstruction, not a visual/runtime validation.

This report owns GLSL, `indra\newview\llviewershadermgr.cpp`, `indra\newview\llviewershadermgr.h`, and `indra\newview\llsettingsvo.cpp`. It does **not** infer framebuffer allocation formats, blend/depth state, scheduling, drawpool routing, or probe capture cadence merely from shader names. Those are separate CPU/backend reports. In particular, a shader named “deferred” can be a forward alpha shader.

Citation shorthand, with **1-based source line numbers**:

- `S\...` = `indra\newview\app_settings\shaders\...`.
- `M:...` = `indra\newview\llviewershadermgr.cpp:...`.
- `MH:...` = `indra\newview\llviewershadermgr.h:...`.
- `E:...` = `indra\newview\llsettingsvo.cpp:...`.

“Observed” below describes actual executable shader expressions, including historical oddities. “Parity invariant” is a consequence of those expressions. “Design option” is explicitly not an implementation already present here.

## 1. G-buffer: a tagged union, not one universal material model

`getGBuffer()` reads `diffuseRect`, `specularRect`, raw `normalMap`, and conditionally `emissiveRect`; returns albedo, decoded normal, specular payload, raw normal-buffer B as environment intensity, raw A as the material/sky tag, and emissive. No color-space conversion occurs inside that accessor. Without `HAS_EMISSIVE`, emissive is zero. (`S\class1\deferred\gbufferUtil.glsl:26-58`.)

### Logical fields written by producers

| Output | Legacy opaque/masked material | SL PBR opaque/masked material | Important exceptions |
|---|---|---|---|
| `frag_data[0].rgb` | **sRGB-valued** diffuse texture RGB multiplied by vertex RGB | **linear** base-color texture RGB, explicitly decoded before multiplication by vertex RGB | Windlight sky stores sRGB-like sky color in RT0 only when the emissive attachment is disabled; otherwise RT0 is zero |
| `frag_data[0].a` | Fullbright/emissive blend factor, **not coverage** | Zero | Sky fallback writes 1; legacy EMISSIVE alpha mode uses texture alpha as emissive intensity |
| `frag_data[1].rgb` | sRGB-valued specular texture × specular factor, or factor alone | Linear **R=occlusion, G=perceptual roughness, B=metallic** | No color transform applies to ORM |
| `frag_data[1].a` | Glossiness = specular-factor A × normal-map A when present | Zero for ordinary PBR objects | Terrain writes a special value described below |
| `frag_data[2].rg` | Encoded eye/view-space normal | Same encoding | Sky outputs arbitrary/placeholder RG; its tag, not its normal, determines shading |
| `frag_data[2].b` | Environment intensity × specular-texture A | Zero | Simple shiny/bump paths use vertex A directly |
| `frag_data[2].a` | `GBUFFER_FLAG_HAS_ATMOS` | `GBUFFER_FLAG_HAS_PBR` | Windlight sky: `SKIP_ATMOS`; actual HDRI: `HAS_HDRI` |
| `frag_data[3].rgb` | Zero | **Linear** emissive factor × explicitly decoded emissive texture | Sky uses this attachment for sky color when enabled; HDRI is already HDR color |
| `frag_data[3].a` | Zero | Zero | Sky writes 1 |

Producer citations:

- Legacy: `S\class3\deferred\materialF.glsl:278-306,328-334,350-366,498-509`.
- SL PBR: `S\class1\deferred\pbropaqueF.glsl:68-121`.
- Sky: `S\class1\deferred\skyF.glsl:84-127`.

**Do not trust stale comments.** `pbropaqueF` calls its emissive output “sRGB Emissive” at line 120, but lines 103-104 multiply a linear emissive factor by `srgb_to_linear(texture(...))`. Its line-40 declaration also gives a misleading packed-order comment; actual lines 93-101 unambiguously implement **ORM**, not OMR. `softenLightF` calls `gb.specular` “PBR linear Emissive” at line 132, but immediately interprets it as ORM at lines 169-172.

Simple nonmaterial opaque geometry has additional conventions: diffuse RGB is vertex RGB × texture RGB, albedo A=0, the entire specular RGBA is `vertex_color.aaaa`, and environment intensity is vertex A. A Vulkan material abstraction that interprets all vertex alpha as transparency will break these surfaces. (`S\class1\deferred\diffuseF.glsl:40-50`.)

PBR terrain RT1 A is named `base_color_factor_alpha`, but its expression blends **`.z`**, not `.a`, from four `baseColorFactors`; retain the observed value when recreating its payload, or deliberately treat a correction as a separate visual change. Main PBR lighting only reads RT1 RGB. Terrain defaults missing ORM to `(1,1,0)` and missing occlusion to 1. (`S\class1\deferred\pbrterrainF.glsl:403-436`; `S\class3\deferred\softenLightF.glsl:167-188`.)

### Tag-dependent interpretation

The tag constants are injected by `indra\llrender\llshadermgr.cpp:636-640` (a narrow backend-definition lookup):

| Tag | Stored floating value |
|---|---|
| `GBUFFER_FLAG_SKIP_ATMOS` | 0.0 |
| `GBUFFER_FLAG_HAS_ATMOS` | 0.34 |
| `GBUFFER_FLAG_HAS_PBR` | 0.67 |
| `GBUFFER_FLAG_HAS_HDRI` | 1.0 |

`GET_GBUFFER_FLAG(data,flag)` means **`abs(data-flag)<0.1`**. Despite “flag” names and comments mentioning bits, these are mutually separated floating tags, **not an OR-able bitmask**. Quantized storage is anticipated by the tolerance. Filtering across material boundaries can yield a different tag or no recognized nonlegacy tag; preserve G-buffer sampling behavior rather than casually changing filtering.

The base-light consumer has ordered branches:

1. PBR: interpret RT1 as ORM and RT0 as linear.
2. HDRI: copy emissive RGB.
3. Skip-atmosphere/Windlight sky: read emissive RGB if present, otherwise albedo RGB; decode sRGB and multiply by `sky_hdr_scale`.
4. Legacy: explicitly decode albedo and specular RGB, shade with legacy equations, and mix toward base color using albedo A.

The final base-light output is linear RGB with A=0. (`S\class3\deferred\softenLightF.glsl:167-211,240-284`.)

**Parity invariants:**

- The same attachment contains different color encodings on different pixels. Replacing it wholesale with an sRGB image view or “always linear” material unpack breaks one branch unless writers and all readers are changed together.
- RT0 A cannot be reused as opaque coverage; RT2 A cannot be treated as surface opacity.
- Emissive-buffer presence changes both writes and sky reads. It is a cross-pass permutation, not only an optional extra render target.
- HDRI branch reads emissive even though the generic sky writer can put color in RT0 when emissive is absent. Valid CPU feature combinations must be retained; do not assume arbitrary combinations are supported.

## 2. Normal, depth, and coordinate contracts

### Normal encoding

Observed encoding is spherical/hemisphere-style two-component packing:

- `f = sqrt(8*n.z + 8)`.
- Stored RG = `n.xy/f + 0.5`; B=environment intensity; A=tag.
- Decode: `q = RG*4-2`, `f = dot(q,q)`, `g = sqrt(1-f/4)`, normal XY=`q*g`, Z=`1-f/2`.

(`S\class1\deferred\globalF.glsl:46-60`.)

It is **not** octahedral packing and it is **not** `(normal+1)/2`. The encoding has a south-pole singularity at `n.z=-1`; it expects normalized normals and valid encoded samples. Precision/filtering differences near its domain edge can create NaNs.

`decodeNormal()` assigns XYZ but never W; thus `getNorm()`'s returned W is undefined. Main G-buffer reading is safe because it separately retrieves the raw tag. The standalone SSR post shader explicitly reads `getNorm(tc).w` as a tag, which is an existing undefined-value trap, not a valid contract to emulate bit-for-bit. (`S\class1\deferred\globalF.glsl:52-60`; `S\class1\deferred\deferredUtil.glsl:163-172`; `S\class1\deferred\gbufferUtil.glsl:45-55`; `S\class3\deferred\screenSpaceReflPostF.glsl:59-69`.)

### Spaces and tangent basis

SL PBR positions are transformed by model-view and stored in `vary_position`; rigid normals/tangents use `normal_matrix`; skinned normals/tangents use transformed point differences. Normal mapping uses `B = sign * cross(N,T)` and `normalize(nx*T + ny*B + nz*N)`. PBR flips the final normal by `gl_FrontFacing`. (`S\class1\deferred\pbropaqueV.glsl:68-105`; `S\class1\deferred\pbropaqueF.glsl:84-106`.)

Legacy material vertex code likewise produces eye-space positions and normal/tangent vectors, but the reviewed legacy material fragment shader does **not** apply the PBR `gl_FrontFacing` flip. (`S\class1\deferred\materialV.glsl:72-129`; `S\class3\deferred\materialF.glsl:278-295`.)

Lighting view direction is `-normalize(pos)`; directional light vectors and local-light centers must share that space. Shader variable names such as `pos_world` in SSAO are misleading: those positions came from inverse projection and are eye-space. (`S\class1\deferred\deferredUtil.glsl:307-329`; `S\class1\deferred\aoUtil.glsl:49-57,77-95`.)

### Depth and screen UV

General reconstruction is:

1. normalized UV → XY NDC via `2*uv-1`;
2. depth sample → Z NDC via `2*depth-1`;
3. multiply by `inv_proj`, divide by W.

Helpers include inverse reconstruction from an already-NDC position and conversion of clip-space coordinates to normalized UV or pixel coordinates. Standard depth is near=0, far=1; forward view Z is negative. (`S\class1\deferred\deferredUtil.glsl:157-160,175-190,307-348`.)

The nominal full-screen vertex shader emits input position directly as clip position and interpolates `(pos.xy*.5+.5)`. (`S\class1\deferred\postDeferredNoTCV.glsl:26-37`.)

**Native Vulkan parity trap:** depth-range conversion, viewport Y convention, winding/front-facing, screen UV, sampled depth, shadow projection, SSAO, SSR, DoF, refraction, and OIT comparisons form one coordinate contract. A Vulkan `[0,1]` projection or reversed-Z design can work, but changing only the reconstruction helper cannot: SSAO and CoF contain their own conversions, OIT explicitly sorts ordinary window depth, and water haze manually recreates it.

## 3. Material semantics: legacy, SL PBR, and native glTF are distinct

### Legacy material

- Alpha modes are NONE=0, BLEND=1, MASK=2, EMISSIVE=3.
- Diffuse RGB multiplies vertex RGB in sRGB-valued space, before any decode.
- Texture alpha, not vertex alpha, is tested against `minimum_alpha - 1/512` for legacy material MASK. This half-8-bit bias is intentional to avoid threshold acne.
- Specular texture RGB multiplies specular-factor RGB; texture A modulates environment intensity. Specular-factor A supplies glossiness and normal-map A multiplies it.
- Fullbright/emissive intensity is `emissive_brightness`, or `max(diffuseTexture.a, emissive_brightness)` in EMISSIVE mode.

(`S\class3\deferred\materialF.glsl:88-95,247-317,328-334,350-372`.)

This is not a shader-side metallic/roughness conversion. The lighting pass explicitly branches to a legacy BRDF and separate legacy reflection rules.

### SL PBR attached to existing prims/meshes

- Base-color texture is explicitly decoded sRGB→linear **before** vertex color multiplication.
- MASK discards when resulting base-color A is below `minimum_alpha`; no legacy `1/512` bias.
- Normal texture RGB is linear vector data remapped to `[-1,1]`.
- ORM R is AO; G × roughness factor; B × metallic factor.
- Emissive texture is explicitly decoded and multiplied by linear emissive factor.
- Opaque/masked output is lower-bounded by zero, not universally upper-clamped by the shader.

(`S\class1\deferred\pbropaqueF.glsl:72-120`.)

**Scene-agent CPU corroboration:** SL PBR binding supplies white for missing base/ORM/emissive textures, a flat normal when absent or discard level>4, and media can override both base-color and emissive textures (`indra\newview\llfetchedgltfmaterial.cpp:64-144`). Base-color factor is baked into the vertex stream (`indra\newview\llface.cpp:1459-1497`); roughness, metallic, emissive, and four independent packed UV transforms are uniforms. Binder cutoff is -1 except MASK's material cutoff, uploaded for all nonshadow cases; therefore it can replace alpha-pool initial thresholds such as .004/.1. Non-MASK shadow leaves the caller's threshold. Defaults are base-color1, roughness1, metallic1, cutoff.5, emissive0, opaque and single-sided (`indra\llprimitive\llgltfmaterial.cpp:50-81`). SL PBR's separate-occlusion property aliases combined ORM rather than supplying native glTF's separate sampler (`indra\llprimitive\llgltfmaterial.h:96-109`). These CPU findings are supplied by `scene-submission.md`, not independently reviewed here.

HUD PBR is a separate fullbright path: base color + emissive, converted back to sRGB in the shader because HUDs are rendered after gamma correction; opaque HUD A=0. Alpha HUD output instead preserves base-alpha × vertex-alpha. (`S\class1\deferred\pbropaqueF.glsl:124-167`; `S\class2\deferred\pbralphaF.glsl:292-344`.)

### Texture transforms are part of appearance

SL PBR applies SL texture animation first, flips V with `1-v`, applies KHR scale→rotation→offset, and flips back. Terrain uses `-v` instead of `1-v` and has no animation. The helper specifically records prior visible precision discrepancies when CPU-precombining the matrix. Tangent transformation applies inverse normal-texture rotation, scale signs, and animation rotation; handedness is multiplied by the product of KHR scale signs. (`S\class1\deferred\textureUtilV.glsl:26-49,51-91,94-150`.)

**Parity invariant:** do not unify asset-image V inversion, viewport Y inversion, and KHR UV conversion into one unexamined flip. Negative UV scales and rotating normal maps require tangent-frame parity, not merely matching UV coordinates.

### Native glTF object shader is a separate implementation

**Dormant native draw path at this HEAD:** the scene agent established that native VBO allocation/primitive upload and render-list population are inside `#if 0` in `indra\newview\gltf\asset.cpp:623-645,660-687`; the renderer skips empty lists/null buffers at `indra\newview\gltfscenemanager.cpp:674`. Its targeted search found no alternative producer. Assets, material UBO preparation, and picking remain live, but the native shader divergences below are **latent implementation hazards, not demonstrated visible baseline behavior**. This does not disable the separately implemented SL PBR materials on existing prims/meshes.

Its std140 material block is a vec4 array, 12 vec4s/material: five two-vec4 texture transforms, emissive factor, then roughness/metallic/alpha parameters. Transform unpack reads offset from first vec4 W and second vec4 X; second vec4 G selects UV0 or UV1. Native glTF has a **separate occlusion texture and UV**, unlike the SL combined ORM texture. (`S\class1\gltf\pbrmetallicroughnessV.glsl:42-96,138-160,278-287`; `S\class1\gltf\pbrmetallicroughnessF.glsl:38-53,77-86,223-231`.)

The scene agent also traced native CPU material packing at `indra\newview\gltf\asset.cpp:191-237`: five transform/UV pairs, emissive, roughness/metallic, and MASK cutoff or -1, without normal-scale or occlusion-strength encoding. Native base factor is baked into U8 vertex color at `indra\newview\gltf\primitive.cpp:431-439`. This corroborates the packed interface, not current rasterization reachability.

Native node/joint transforms use three packed vec4s per transform, reconstructing a mat4 from XYZ and translation in each vec4 W; skinned input joint indices are `uvec4` and weights are `vec4`. (`S\class1\gltf\pbrmetallicroughnessV.glsl:196-257`.) Manager limits derive from uniform-block byte size: nodes divide by 48 bytes, materials by 192 bytes, raw material vec4 capacity by 16. (`M:292-303`.)

Latent native glTF shader divergences requiring decisions if enabling that draw path, not silent “standard glTF” substitutions:

- Fragment `minimum_alpha` initializes to `-1`, then **subtracts** packed alpha; the shader does not simply assign the advertised cutoff. (`S\class1\gltf\pbrmetallicroughnessF.glsl:33-53,198-201`.)
- Lit alpha multiplies roughness and metallic factors once while assembling ORM, and **again** when selecting lighting parameters. (`...pbrmetallicroughnessF.glsl:228-231,286-287`.)
- Native alpha multiplies vertex alpha into basecolor and multiplies it again for final alpha. (`...pbrmetallicroughnessF.glsl:194-196,320-322`.)
- Native lit alpha explicitly decodes `sunlit` after calling the atmospheric “Linear” function, unlike SL PBR alpha; that atmospheric function already decodes sunlit outside classic mode. (`...pbrmetallicroughnessF.glsl:275-278`; `S\class2\deferred\pbralphaF.glsl:219-222`; `S\class1\windlight\atmosphericsFuncs.glsl:154-164`.)
- Native alpha water clipping is commented out. Native UNLIT writes a single color output rather than the lit G-buffer outputs. Native lit opaque writes RT3 unconditionally, with the `HAS_EMISSIVE` guard commented out. (`...pbrmetallicroughnessF.glsl:169-173,190-192,238-250`.)

These are executable source expressions in a dormant draw path. Determining intended corrections or validating newly enabled native rendering requires the scene/material report and runtime tests; reproducing these quirks is not a requirement for the currently visible SL PBR baseline.

## 4. Lighting equations and HDR boundaries

### PBR BRDF and material conversion

`calcDiffuseSpecular()` uses dielectric F0 `(0.04,0.04,0.04)`:

- diffuse color = baseColor × `(1-F0)` × `(1-metallic)`;
- specular color = mix(F0, baseColor, metallic).

Punctual BRDF is Lambert diffuse plus Schlick Fresnel, Smith-style geometric attenuation, and GGX/Trowbridge-Reitz distribution. Perceptual roughness has a punctual-light floor of `8/255`; alpha roughness is perceptual roughness squared, and the distribution uses its square again. `N·L` clamps to `[.001,1]`; `abs(N·V)` clamps likewise. Diffuse BRDF includes `1/pi` and `(1-F)`. The result is split into diffuse and specular contributions for later recombination. (`S\class1\deferred\deferredUtil.glsl:417-519,565-571`.)

IBL samples the BRDF LUT at `(clamped N·V, 1-perceptualRoughness)`. Diffuse = irradiance × diffuseColor; specular = radiance × `(F0*LUT.r + LUT.g)`; **both** are multiplied by material AO. There is no extra `1/pi` in this IBL combine. LUT generation stores `BRDF(uv.x,1-uv.y)` and uses 1024 Hammersley samples, so its Y inversion is deliberate and matched. (`S\class1\deferred\deferredUtil.glsl:369-395`; `S\class1\deferred\genbrdflutF.glsl:56,111-140`.)

Nonclassic sun contribution is `clamp(NL*(diff+spec),0,10) * sunlit * 3 * shadow`. Then IBL specular and emissive are added. The `additive`/`atten` arguments to `pbrBaseLight()` are not used to fog its returned color: fog is separate. (`S\class1\deferred\deferredUtil.glsl:573-623`.)

### Local lights preserve legacy attenuation

Common attenuation is `2 * (1-clamp((distance/radius + falloff)/(1+falloff),0,1))^2`, rather than inverse-square physical falloff. Deferred PBR point and projected spot lights use gain **3.25**, whereas forward PBR light helper uses **3.0** plus the squared spotlight coefficient. Classic mode further scales local lights by .9. (`S\class1\deferred\deferredUtil.glsl:95-103,521-562`; `S\class3\deferred\pointLightF.glsl:88-117,153-157`; `S\class3\deferred\spotLightF.glsl:174-186,269-276`.)

Deferred multiple point lights repeat the same 3.25 gain; light positions/radii are vec4 data, and RGB/falloff are separate vec4 data. Their loop length is a compile-time `LIGHT_COUNT`. (`S\class3\deferred\multiPointLightF.glsl:102-123`.)

Projected spots:

- Clip against light size and projection; decode projected texture RGB to linear and multiply by texture alpha.
- Read selected spot shadow from lightMap B or A and add/clamp `shadow_fade`.
- Compute diffuse projection LOD from light distance/focus/range.
- PBR projection ambiance is added separately, with gain 3.25, not multiplied by the shadow term in that expression.
- Legacy environment intensity can also reflect a projected texture by intersecting a reflected view ray with the projector plane.

(`S\class1\deferred\deferredUtil.glsl:138-153,193-254`; `S\class3\deferred\spotLightF.glsl:109-122,161-186,239-263`.)

### Legacy BRDF is not PBR with metallic=0

Legacy uses a `lightFunc` lookup indexed by `(N·H, glossiness)`, Fresnel `pow(1-VH,5)*.4+.5`, and a geometry factor based on `min(2NH*NV/VH,2NH*NL/VH)`. Its sun diffuse uses `min(NL, shadow)` rather than multiplying those two factors. Specular angular intensity uses `min(NL*6,1)`. Fullbright blend then mixes lit color to base color. (`S\class3\deferred\softenLightF.glsl:208-275`.)

Legacy glossy environment is intentionally attenuated with two .5 factors, glossiness, squared clamped Fresnel, specular RGB, and fake energy conservation `(1-color)`. Legacy environment reflection follows its own Fresnel and mix and is not BRDF-LUT based. (`S\class3\deferred\reflectionProbeF.glsl:893-913`.)

### Actual numerical HDR contract

`clampHDRRange` converts Inf components to 1, NaN components to 0, then clamps `[0,11.2]`. Base-light output calls it, but local light shaders generally only lower-bound color and add their outputs later. Consequently “11.2 is the global scene maximum” is false without considering pass order. (`S\class1\deferred\deferredUtil.glsl:83-93`; `S\class3\deferred\softenLightF.glsl:283-284`; `S\class3\deferred\pointLightF.glsl:153-157`.)

Explicit `linear_to_srgb()` clamps its input to `[0,1]` before the piecewise transfer; `srgb_to_linear()` does not contain an equivalent upper clamp. This matters inside classic lighting, not just at presentation. (`S\class1\environment\srgbF.glsl:26-40,64-79`.)

## 5. SSAO and shadow intermediate contracts

### Combined screen-space light map

The SSAO sun shader outputs:

- R = directional shadow;
- G = ambient visibility;
- B = projected shadow slot 0;
- A = projected shadow slot 1.

All components clamp `[0,1]`. The non-SSAO sun variant is selected separately. (`S\class2\deferred\sunLightSSAOF.glsl:41-53`; `M:1727-1761`.)

AO uses eight fixed quadratically spaced offsets, reflected by a repeating noise sample; radius is `min(ssao_radius/-viewZ, ssao_max_radius)` and kernel offsets divide by screen resolution. Occluders are estimated by hemisphere/normal offset and inverse squared distance; samples substantially closer than the current surface are excluded from the “points” denominator. (`S\class1\deferred\aoUtil.glsl:60-116`.)

The light-map blur is bilateral-like but tests distance from the current tangent plane, not just depth difference. It uses different kernel weight components for AO versus shadow (`xyxx` swizzle), a depth-scaled radius, and tolerance proportional to viewZ². (`S\class1\deferred\blurLightF.glsl:44-108`.)

Base light applies SSAO only by mixing an irradiance darkening/color-effect transform with original irradiance. It is **not** a final-frame multiply and does not directly darken sun, emissive, or radiance specular. Material ORM AO separately multiplies both PBR IBL diffuse and specular. (`S\class3\deferred\softenLightF.glsl:109-116,134-147,174-188`; `S\class1\deferred\deferredUtil.glsl:390-394`.)

Legacy fullbright A also raises directional shadow via `max(lightMap.r, baseColor.a)`. (`S\class3\deferred\softenLightF.glsl:134-145`.)

### Shadow sampling

Six comparison samplers are logical slots: 0-3 sun/moon cascades; 4-5 projected spot shadows. Shadow positions are eye-space and transformed by six `shadow_matrix` matrices. Directional receiver offset is along the active sun/moon light direction, scaled by `(1-max(NL,0))*shadow_offset*2`; projected receivers move along the normal by `spot_shadow_offset`. (`S\class1\deferred\shadowUtil.glsl:28-52,96-111,197-203`.)

Directional PCF: center weight 4 plus four rotated offsets, total weight 8; Z bias doubles `shadow_bias`; X is snapped/jittered using screen Y. Spot PCF uses five equally weighted taps, different offsets/resolution, and a different bias. (`...shadowUtil.glsl:54-93`.)

Cascades overlap using near/far thresholds `-.75*shadow_clip` and `-1.25*shadow_clip`; contributions normalize by accumulated weights; geometry beyond the last clip is lit, with a far transition term. This is not a single hard split. (`...shadowUtil.glsl:113-194`.)

Opaque shadow fragment shader simply writes white; depth is rasterized. PBR MASK shadows test texture alpha × vertex alpha against the cutoff. PBR BLEND shadows discard below .05 and use a deterministic screen stripe/dither below .88, not alpha-to-coverage or physically fractional transmission. (`S\class1\deferred\shadowF.glsl:26-30`; `S\class1\deferred\pbrShadowAlphaMaskF.glsl:36-45`; `S\class1\deferred\pbrShadowAlphaBlendF.glsl:36-55`.)

## 6. Reflection probes, prefiltering, SSR

### GPU probe data and selection

The class3 shader consumes a std140 `ReflectionProbes` block with 256 box matrices, hero box, 256 sphere vec4s, 256 parameter vec4s, hero sphere, 256 index ivec4s, 1024 neighbor ivec4s, 256 depth-bucket ivec4s, and trailing counts/hero metadata. Sphere/index arrays are not cubemap-array layers: `refIndex[i].x` maps them to layers. Params are irradiance scale, radiance scale, fade, z-near. Negative priority means box influence. (`S\class3\deferred\reflectionProbeF.glsl:43-82`.)

Search starts in a view-depth bucket `floor(-pos.z)` clamped 0..255, finds an influencing probe and traverses its neighbor list. Box influence is tested in transformed unit-box coordinates and suppresses automatic probes. Unless suppressed, void/default probe index 0 is included. Compile-time sample budget is 32. (`...reflectionProbeF.glsl:101-249`; `M:824-848`.)

Manual spheres receive parallax correction at their radius. Automatic probes instead use a 4096-radius sphere to effectively disable visible local sphere parallax. Box radiance uses box intersection; irradiance expands the box-intersection scale to 3. Directions then transform by `env_mat` for cubemap sampling. (`...reflectionProbeF.glsl:501-571`.)

Sphere weight combines inverse distance, fade, and a falloff beginning at half-radius. Manual and automatic colors normalize independently then blend using accumulated manual distance weights. The irradiance result separately mixes toward ambient according to irradiance scale. (`...reflectionProbeF.glsl:474-489,569-690`.)

### Roughness and fallback behavior

Radiance LOD = `(1-glossiness)*max_probe_lod`; PBR sets glossiness=`1-perceptualRoughness`. Classic mode keeps atmospheric ambient instead of sampling probe irradiance, but still samples radiance. Water suppresses automatic probes during selection, restores their sampling flag, and explicitly adds void probe 0. (`...reflectionProbeF.glsl:736-791`.)

The no-probe class2 implementation is **not black/no-IBL**: it uses an environment cubemap, adjusts ambient with `reflection_probe_ambiance`, decodes the PBR cubemap sample, leaves legacy cubemap data sRGB-valued, makes legacy `applyGlossEnv` a no-op, and applies legacy environment blending in sRGB before decoding. (`S\class2\deferred\reflectionProbeF.glsl:26-80`.)

Hero reflections blend after ordinary probes/SSR, only progressively above glossiness .75, using box/sphere influence and a clip-plane fade. Hero cubemap sampling currently selects layer 0 and LOD `(1-glossiness)*heroMipCount`. (`S\class3\deferred\reflectionProbeF.glsl:693-723,771`.)

Radiance generation is importance-sampled GGX, **not ordinary box-filter mipmaps**: roughness = mipLevel/maxLOD, sample count scales with roughness, source LOD is PDF/solid-angle derived with +1 bias. Normal probes compile 32 filter samples, hero probes 4. Irradiance is cosine-weighted Lambert hemisphere integration, fixed 32 samples, LOD bias 2, mean of samples without a later extra pi factor in IBL. (`S\class1\interface\radianceGenF.glsl:98-167`; `M:3577,3589`; `S\class2\interface\irradianceGenF.glsl:44-47,106-118,176-212`.)

**Auxiliary-agent CPU corroboration:** `auxiliary-views.md` documents the CPU block layout at `indra\newview\llreflectionmapmanager.h:58-101`, irradiance-capture ambient/radiance scales of 0/.5 versus radiance-phase 1/1 at `indra\newview\llreflectionmapmanager.cpp:1144-1150`, and regular filtering stopping at 2×2 even though allocation includes 1×1. Hero generation filters only `mipChain.size()/4` output levels (`indra\newview\llheroprobemanager.cpp:461`) while the uploaded `heroMipCount` is the full chain size (`...llheroprobemanager.cpp:542`). This is consistent with the shader's narrow high-gloss hero participation but makes allocated mip count, filtered mip count, and shader roughness scale distinct quantities. CPU code in this handoff was not independently reviewed here.

### SSR is temporal scene sampling, not a reflection-only post-process guarantee

The probe helper incorporates SSR directly into gloss environment. PBR calls it only for glossiness ≥ .9; legacy has no equivalent outer threshold. Both suppress it for `cube_snapshot==1`. Transparency passes request a sharp ray then scale confidence by surface glossiness. (`S\class3\deferred\reflectionProbeF.glsl:752-768,867-883`.)

SSR transforms the current surface position and ray into the stored scene frame with `inv_modelview_delta`, projects ray steps, reconstructs sampled `sceneDepth` into view depth, and samples **global `sceneMap`**, even though a sampler parameter is passed. Search has adaptive/exponential stepping and optional binary refinement. (`S\class3\deferred\screenSpaceReflUtil.glsl:26-35,47-51,69-184`.)

Confidence includes screen-edge, facing, view-distance fade ending at 128, glossiness gating, and hit fraction. `glossySampleCount` overrides the caller's `totalSamples` argument; samples use a fixed 128-entry Poisson array. (`...screenSpaceReflUtil.glsl:187-319,322-393`.)

**Traps:** sceneMap/sceneDepth must represent the same camera frame; matrix delta does not by itself specify history invalidation. Probe capture must not recursively SSR itself. The separate `screenSpaceReflPostF` is not evidence that the primary path is a post-only SSR implementation; it also uses undefined decoded tag W and passes zero glossiness to the helper (`S\class3\deferred\screenSpaceReflPostF.glsl:59-89`).

## 7. Atmosphere, classic skies, water, and fog

### Atmosphere is intentionally nonphysical and mode-dependent

Atmospheric base function computes sun/moon color attenuation, blue/haze density weights, exponential distance attenuation, directional haze glow, and cloud-shadow-adjusted ambient. Its height/altitude conventions use Y in the shader's atmospheric frame. Ambient starts with `pow(tmpAmbient,.9)*.57`; additive haze is bounded at 10. (`S\class1\windlight\atmosphericsFuncs.glsl:50-131`.)

`calcAtmosphericVarsLinear` modifies ambient by `1-(.5*abs(NL))²`. **Only nonclassic mode** decodes ambient and sunlit and desaturates ambient with Rec.709 luminance; both modes then apply separate sky sunlight/ambient scales. Its name does not mean all returned terms are linear in all modes; additive stays in the legacy representation. (`...atmosphericsFuncs.glsl:135-165`.)

Fog application is `light * atten.r + srgb_to_linear(additive*2)*sky_hdr_scale`; RGB attenuation is effectively reduced to its red component. Deferred haze outputs those additive RGB and multiplicative A terms separately, discards depth≥1 sky, and disables atmospheric effect when camera and object are both below the water plane. (`S\class1\windlight\atmosphericsF.glsl:35-46`; `S\class3\deferred\hazeF.glsl:49-99`.)

### CPU environment values materially alter shaders

Classic mode is `sky.canAutoAdjust() && !RenderSkyAutoAdjustLegacy`, **not** a material flag. Nonclassic updates sky tonemap mix from `RenderTonemapMix`; sunlight scale chooses HDR/non-HDR settings. During irradiance capture ambient uniform becomes zero; sky blue horizon/density may be desaturated. Sky HDR scale is `2*sqrt(gamma)` when reflection-probe ambiance is nonzero, a configured automatic scale for auto-adjusted legacy sky, otherwise 1. (`E:688-717,795-861`.)

Scene local-light normalization derives from `2*(.75 + max(RenderSunDynamicRange,.0001)*max(sunDirection.z,0))`. (`E:657-676`.)

Classic base-light differences are explicit:

- Additional sunlit ×1.35 and final base-light ×1.1.
- Legacy diffuse uses `pow(NL,1.2)`, sRGB/linear recombination and .9/.7 factors.
- PBR reconstructs its classic diffuse lighting with the legacy-style transfer operations, pi compensation, shadow factors, and .9/.7/1.1 adjustments.
- Local lights ×.9; probe irradiance bypassed.

(`S\class3\deferred\softenLightF.glsl:157-163,226-237,279-284`; `S\class1\deferred\deferredUtil.glsl:584-615`; `S\class3\deferred\reflectionProbeF.glsl:744-747`.)

**Parity invariant:** “replace legacy shader with modern linear BRDF” changes not only legacy materials but also PBR under classic skies. Sky choice is a rendering mode.

### Water plane/fog

Water and mirror planes are transformed into eye space in settings application; mirror flag is set from the active mirror pass. Water fog receives both original color and explicitly linearized color uniforms, modified underwater density, and `waterFogKS=1/max(lightDirection.z,clamp)`. (`E:1090-1139`.)

Water fog intersects the view ray with the eye-space plane, derives thickness, uses attenuation base .98 and an empirical 1/1.7 power for scattering, returns `(linear fog color * scattering, transmittance)`. Applying it preserves surface alpha and changes RGB by `RGB*transmittance+fogRGB`. Forward `applySkyAndWaterFog` chooses sky versus water fog according to eye/object side; it does not always apply both. (`S\class1\environment\waterFogF.glsl:38-143`.)

Deferred water haze samples exclusion mask, discards excluded above-water pixels, manually depth-tests its water geometry against sampled opaque depth, reconstructs scene position, and raises fog transmittance to 1.7. (`S\class3\deferred\waterHazeF.glsl:40-74`.)

### Water surface

Class3 water hard-defines `WATER_MINIMAL=1`. It blends two normal-map assets and three moving wave samples, uses special Fresnel scale/offset and normal scaling, obtains refraction from screen color/depth when `TRANSPARENT_WATER` is enabled, and rejects a distorted refraction coordinate if it samples geometry more than .05 in front of the surface. Exclusion-mask value modifies refraction fade. Without transparent water it synthesizes background via water fog and discards mask<1. (`S\class3\environment\waterF.glsl:28,144-181,183-289`.)

It treats the specular tint as metallic=1, roughness=`blurMultiplier`; fetches the water-specific probe selection; computes modified-normal punctual sunlight with shadow and atmospheric attenuation; mixes refraction with radiance using special Fresnel terms. This is not the ordinary object PBR equation. Most importantly it **clamps the final RGBA to [0,1]**, even in this HDR pipeline. `spec=min(max(punctual RGB),0)` followed by lower bounding makes normal nonnegative-light output alpha zero. (`...waterF.glsl:291-347`.)

Underwater surface shader has a different path: distort screen/background according to waves and exclusion, then apply unclipped water fog to the water surface position. (`S\class3\environment\underWaterF.glsl:60-86`.)

## 8. Forward alpha, fullbright, and OIT

SL PBR alpha shades directly to linear scene color, not G-buffer: explicit material setup → sun shadow/probe IBL → seven forward local lights (indices 1..7) → sky/water fog → final alpha = texture alpha × vertex alpha. It flips backface normals and clips against the water plane. Its material AO acts through IBL; it does not sample SSAO G merely because lightMap is declared. (`S\class2\deferred\pbralphaF.glsl:182-289`.)

**Slot count is not dynamic-light count:** the shader evaluates indices 1..7, but the parent's CPU trace establishes that `setupHWLights` fills only six nearby-light slots 2..7 with fade-scaled colors (`indra\newview\pipeline.cpp:6500` onward). Deferred lighting may process up to 256 lights and uses `getLightLinearColor()*light_scale` without the nearby-entry fade (`...pipeline.cpp:9999` onward). Further, the only `calcNearbyLights` call occurs in post-deferred work after opaque local lighting (`...pipeline.cpp:4447`). Thus the seven shader invocations are not seven interchangeable nearby-light slots, and forward/deferred light selection, timing, and fades must not be advertised as identical. This CPU corroboration comes from the parent.

Legacy material alpha additionally folds specular glare into coverage as `max(textureAlpha,glare)*vertexAlpha`, after fullbright/glossy-environment and seven forward lights. It has a historical attenuation-radius reconstruction rather than sharing the PBR helper. (`S\class3\deferred\materialF.glsl:151-180,448-495`.)

Fullbright forward non-HUD decodes vertex×texture RGB to linear; alpha variants apply water clipping and fog; HUD leaves its sRGB-valued RGB for post-gamma output. Alpha masking tests texture alpha before final vertex-alpha multiplication. (`S\class1\deferred\fullbrightF.glsl:111-166`.)

### PPLL capture payload

Enabled alpha shaders declare independent GL binding namespaces all numbered 0:

- `r32ui` image head;
- std430 uint SSBO node pool;
- atomic-counter buffer.

Each node is four uints (16 bytes): packed FP16 R/G, packed FP16 B/A, float-bits ordinary window Z, previous index. RGB clamps below zero; A clamps `[0,1]`; sentinel is `0xFFFFFFFF`. Capture does not declare early fragment tests. On allocator overflow it returns false and **falls through to legacy blending**; successful capture discards the normal color output. (`S\class2\deferred\pbralphaF.glsl:26-54,276-288`; `S\class1\deferred\alphaOITResolveF.glsl:38-59`.)

Resolve rejects nodes behind opaque depth using `nodeZ <= opaqueZ`. It exact-sorts up to min(user cap,32) visible nodes in descending depth, traverses at most 256 total, and collapses remaining visible nodes into a coverage-weighted color/transmittance tail. The tail is composited first, followed by exact-sorted nodes. The exact subset consists of the first visible list entries, **not a globally nearest-N selection**. Output is premultiplied RGB and coverage; shader comment requires ONE/ONE_MINUS_SRC_ALPHA with destination alpha protected for glow. (`...alphaOITResolveF.glsl:43-148`.)

Depth-peeling shader modes: 2 writes selected depth, rejecting Z≥prior selected unless first peel; 3 replays only exactly equal selected Z; 4 is legacy tail and rejects Z≥selected. Exact-equality replay is sensitive to any projection/depth precision change between passes. (`S\class2\deferred\pbralphaF.glsl:57-80`.)

**Native Vulkan invariants:** image/SSBO/atomic binding 0 cannot be naively mapped to one descriptor binding; preserve distinct resources. Capture→resolve requires ordered visibility of node writes and head updates. Depth convention changes require reversing selection, rejection, and sorting together. Keep the FP16 node precision and overflow fallback as observable quality behavior unless explicitly replacing the algorithm. Barrier scheduling and fixed-function blend state belong to the backend/pipeline report.

## 9. Post processing: shader-defined dependencies and alpha repurposing

This section gives a **partial dependency graph**, not an invented total CPU frame order. Separate scheduling evidence must decide whether bloom precedes tonemap, where DoF sits relative to AA, and which final copy writes depth in each mode.

**Parent orchestration handoff:** the parent's trace of `indra\newview\pipeline.cpp:9295` (`renderFinalize`) establishes SSR scene copy → luminance/exposure → tonemap (with optional CAS/gamma) → glow from post-gamma color → DoF → FXAA/SMAA → RLVa/vignette/snapshot effects → final noise/depth copy. This CPU ordering is supplied by the parent rather than independently reviewed here. It resolves the cross-stage ordering questions above for that path; individual feature gates remain the parent's scope.

### Exposure

Luminance shader samples a central 60% rectangle, shifted down .1 in Y, scales object diffuse luminance but not HDRI/Windlight sky by testing raw normal tag, adds its `emissiveRect` input, and computes Rec.709 luminance. It does not itself linearize diffuse RGB; callers must supply the intended image. (`S\class1\deferred\luminanceF.glsl:35-66`.)

**The emissive sampler is live, not unused:** line 63 unconditionally adds `texture(emissiveRect,tc).rgb` to the color whose luminance becomes the output at lines 65-66. There is no `HAS_EMISSIVE` guard or multiplication by zero. `gLuminanceProgram` selects this source with cleared permutations (`M:2475-2481`). Given the parent's binding trace at `indra\newview\pipeline.cpp:7753` (`generateLuminance`) that `emissiveRect` receives the old `mGlow[1]`, this shader reads the retained glow contents into exposure calculation; it is not reading the PBR emissive G-buffer merely because the sampler is named emissiveRect. Whether those contents are previous-frame glow, cleared values, or another retained state depends on CPU initialization/update gates. A conforming optimizer cannot generally remove this sample because it affects the output.

Exposure reads luminance **mip 8** at center, clamps to a configurable maximum, normalizes and squares it, then interpolates between configured bright/dark exposure endpoints. Optional history uses exponential adaptation with speed `-log(params.w)/params2.w`; output alpha stores `dt`. This is not histogram/percentile auto exposure. (`S\class1\deferred\exposureF.glsl:30-64`.)

Dependencies: luminance image and complete required mip chain → exposure update; prior exposure → update only for `USE_LAST_EXPOSURE`; exposure texture → tone mapping.

### Tone mapping and gamma

Tone mapping multiplies manual exposure × exposure texture, selects Khronos Neutral (type 0) or ACES Hill (type 1), blends exposed input with mapped result using `tonemap_mix`, and clamps `[0,1]`. ACES Narkowicz exists in the utility but is not selected by this switch. (`S\class1\deferred\tonemapUtilF.glsl:53-114,116-150`.)

The post-tonemap fragment program calls the utility only when **not** `NO_POST`; its `NO_POST` branch simply clamps input and **bypasses exposure too**. This is different from calling the utility's own `NO_POST` branch, which would still multiply exposure. Optional `GAMMA_CORRECT` then does sRGB encoding; optional `LEGACY_GAMMA` applies `1-(1-clamp(color))^gamma`. Alpha is preserved. (`S\class1\deferred\postDeferredTonemap.glsl:43-76`; compare `...tonemapUtilF.glsl:145-147`.)

Separate gamma-only shader likewise encodes sRGB and optionally applies legacy gamma; it does not perform tone mapping. (`S\class1\deferred\postDeferredGammaCorrect.glsl:38-57`.)

### Glow/bloom

Glow extraction derives luminance and warmth smoothsteps and writes A=`max(existing screen A, threshold-derived extractionAlpha)`, preserving RGB except optional noise dither. Its output alpha is a glow weight, not transparency. Eight-tap glow blur preserves weighted alpha and scales RGB by `glowStrength`. A simple combine shader adds diffuse and emissive images. (`S\class1\effects\glowExtractF.glsl:43-61`; `S\class1\effects\glowF.glsl:36-55`; `S\class1\interface\glowcombineF.glsl:28-37`.)

Emissive/glow geometry shaders intentionally output RGB zero and alpha-only contribution: legacy diffuse-alpha×vertex-alpha; PBR glow computes max linear-emissive channel × vertex-emissive-alpha. (`S\class1\deferred\emissiveF.glsl:33-37`; `S\class1\deferred\pbrglowF.glsl:48-65`.)

### AA and sharpening

- FXAA's prep shader preserves RGB and stores luma in alpha with **(.299,.587,.144)**, not the textbook .114 blue coefficient. FXAA defaults to alpha luma. Its actual call uses subpixel .75, edge threshold .07, minimum threshold .03. The final FXAA shader copies sampled depth to `gl_FragDepth`. (`S\class1\interface\glowcombineFXAAF.glsl:36-40`; `S\class1\deferred\fxaaF.glsl:275-294,760-763,2113-2136`.)
- SMAA is color edge detection → edge/area/search texture weight calculation → neighborhood blend. Weight calculation passes zero subsample indices. Manager disables predication and reprojection; there is no velocity input in these active permutations. (`S\class1\deferred\SMAAEdgeDetectF.glsl:49-58`; `...SMAABlendWeightsF.glsl:34-55`; `...SMAANeighborhoodBlendF.glsl:33-61`; `M:2676-2685`.)
- CAS filters first, preserves sampled alpha, **then encodes linear→sRGB**, and optionally applies legacy gamma. It is not a neutral sharpen shader that can be inserted on arbitrary gamma-encoded input. (`S\class1\deferred\CASF.glsl:2549-2573`.)

### Depth of field

CoF pass reconstructs view Z from depth with the OpenGL `2*d-1` mapping; computes signed CoF from focal distance, blur constant, magnification, and pixel angle; clamps ±maxCoF and packs it into **alpha**, with .5 in focus. (`S\class1\deferred\cofF.glsl:46-78`.)

Bokeh blur reads signed CoF from alpha, samples circular rings with different near/far rules, and weights bright samples more strongly using `.25+R+G+B`. Its RGB gets `clampHDRRange`; its alpha remains the filtered CoF. Combine samples scaled low-resolution DoF, obtains blend factor from original CoF alpha and `max_cof*res_scale²`, and smooths the intermediate transition with four taps. (`S\class1\deferred\postDeferredF.glsl:39-126`; `S\class1\deferred\dofCombineF.glsl:43-74`.)

The no-DoF copy is not completely inert: optional procedural noise adds up to .003, HDR clamp applies, and sampled scene depth is written to `gl_FragDepth`. (`S\class1\deferred\postDeferredNoDoFF.glsl:77-91`.)

**Scene-agent corroboration/caveat:** comments in `indra\newview\lldrawpoolalpha.cpp:391-406` describe a final alpha depth-only DoF pass, but that agent's trace of `renderAlpha(depth_only=true)` at lines 861-1347 found `depth_only` suppresses glow without uniformly enforcing depth-write state or a fixed .33 alpha cutoff; normal shader selection can replace the earlier threshold. Those CPU paths were not independently reviewed here. Therefore this shader report makes no claim that transparent surfaces universally enter the DoF depth image at a .33 threshold; consult the scene report for mode-dependent behavior.

**Alpha lifetimes:** G-buffer emissive/fullbright → scene glow channel → possibly FXAA luma or signed CoF → other presentation intermediates. These are distinct image-stage meanings; in-place reuse or generic alpha blending across all of them is invalid.

## 10. Shader classes, permutations, and capabilities

The manager has separate class levels for lighting/object/avatar/environment/interface/effect/Windlight/water/deferred (`MH:71-82`). At this revision it requests lighting/water/deferred=3, interface/environment/object/effect/Windlight=2, then actual avatar class=1 after preparing hardware skinning. Hardware skinning and deferred are hard requirements; this is not a live “fixed-function fallback versus PBR” architecture. (`M:613-636,700-722`.)

Indexed texture channels are forced to **4**, despite saved-setting variables and a commented clamp suggesting broader configurability. (`M:571-577`.)

Common globals/permutations:

| Feature | Source contract |
|---|---|
| Optional emissive buffer | `HAS_EMISSIVE` from `RenderEnableEmissiveBuffer`, both shared helpers and participating programs (`M:271-278,813-818`) |
| Sun/spot helpers | `SUN_SHADOW` for shadow detail≥1, `SPOT_SHADOW` for ≥2; per-program `HAS_SUN_SHADOW` additionally selects call sites (`M:828-837,932-933,2155-2158`) |
| Reflection probes | Enabled only with setting and GL>3.99; choose class3 helper vs class2 fallback; level clamped 0..3 and sample budget32 (`M:824-848,906`) |
| SSR | `SSR` define and class3 helper when enabled; class1 fallback otherwise (`M:820-843,907`) |
| Mirrors | `HERO_PROBES` define (`M:822,851-854`) |
| Terrain | Planar sample count, triplanar blend factor, and detail are compile-time globals (`M:856-864`) |
| Legacy material variants | 32 entries: bits0-1 alpha mode, bit2 specular map, bit3 normal map, bit4 skin (`M:1306-1376`) |
| SL PBR | Opaque/mask, alpha, HUD, glow, terrain; rigged variants copy features/defines and add `HAS_SKIN` (`M:254-268`; declarations `M:239-251`) |
| Native glTF | Cartesian alpha-blend/rigged/unlit/multi-UV flags; UBO limits embedded in defines (`M:282-374`) |
| SSAO | Separate sun fragment selection; `HAS_SSAO` on soften (`M:1727-1761,2160-2164`) |
| Tonemap | Ordinary/NO_POST × gamma/no-gamma × legacy/nonlegacy gamma programs (`M:2516-2609`) |
| FXAA | GL>3.9, quality presets12/23/28/39; shader-failure unloads all four and continues (`M:2613-2663`) |
| SMAA | GL>3.15, low/medium/high/ultra, GLSL3/4 selection, predication=0/reprojection=0; group unload on failure (`M:2666-2775`) |
| CAS | GL>4.05, standard and legacy-gamma variants, optional on failure (`M:2778-2814`) |
| PPLL resolve | Windows/Linux and GL≥4.4; absent on lower capability (`M:3045-3062`) |

Native glTF alpha and legacy material programs temporarily suppress lighting feature flags while compiling, then restore them for uniform/matrix synchronization. Shader compile/link dependency flags and runtime uniform-update flags are therefore not identical concepts. (`M:320-348,1297-1304,1381-1388`.)

Shared helpers are loaded individually, including atmospheric, water fog, sRGB, deferred/G-buffer/global helpers, shadow/AO, terrain, tone-map, probe and SSR helpers. Source files rely on injected code and feature-driven linking, so compiling each disk `.glsl` as an independent SPIR-V module is not a faithful replacement. (`M:879-920`.)

**Backend-agent corroboration:** `indra\llrender\llshadermgr.cpp:68-365` assembles feature helpers: normal programs always attach vertex `textureUtilV` and fragment `globalF`; deferred/probe features attach `deferredUtil`; `hasFullGBuffer` attaches `gbufferUtil`; SSR/probes attach SSR utilities; remaining shadow/probe/AO/atmosphere/terrain/tonemap/water helpers follow their features. `attachNothing` bypasses this assembly. The injected `GBufferInfo` declaration lives at line 769. Source lookup searches requested class down to class1 (`...llshadermgr.cpp:516-557`), while compile retry decrements class (`...llshadermgr.cpp:960-967`). A reported logical class therefore need not equal the directory of the selected source. Program creation also has recursive level fallback, but failed feature attachment returns failure immediately (`indra\llrender\llglslshader.cpp:407-554`, especially 471-475 and 486-501). The disabled error-shader fallback is not an operational recovery path (`...llshadermgr.cpp:496-513`). Consult `backend-contracts.md`; these broader backend lines are supplied by its author rather than independently reviewed here.

Terrain detail levels in shader: 0 includes emissive, -1 retains occlusion, -2 normal, -3 metallic/roughness; lower detail becomes matte plastic. Mapping supports one planar sample or three triplanar samples, and material mixture comes from height/noise ramps or a paint map. (`S\class1\deferred\pbrterrainF.glsl:28-69,184-197,418-429`.)

## 11. Native Vulkan parity checklist versus speculative improvements

### Required to preserve the observed image

1. Preserve material-tagged G-buffer interpretation, attachment-local alpha meanings, optional emissive behavior, and manual color transfers.
2. Preserve eye-space normal/depth/lighting agreement; explicitly validate viewport Y, normal handedness, backfaces, shadow texels, and water-plane signs as one migration.
3. Preserve legacy BRDF and reflection equations separately from PBR; retain classic-sky behavior for **both** material families.
4. Preserve actual roughness floor, inverted LUT Y, probe mip roughness mapping, AO scope, and local-light gain differences.
5. Preserve mask thresholds per shader family, legacy glow/emissive alpha semantics, transparent shadow dither, and forward versus deferred local-light limitations.
6. Preserve comparison sampler behavior, cascade overlaps, bias direction/magnitude, lightMap channel layout, and bilateral blur details.
7. Preserve probe fallback, manual/automatic priorities, void probe contribution, classic irradiance bypass, water probe selection, and SSR camera-history compatibility.
8. Preserve nonstandard water clamping/refraction/exclusion and fog composition; do not render water as ordinary transparent GGX material.
9. Preserve exposure sample region/mip/adaptation and post-process color-space boundaries. Shader availability does not prove pass order.
10. Preserve OIT node packing, ordinary-depth visibility, capped traversal/tail semantics, fallback on overflow, and glow alpha protection.

### Existing source traps to isolate rather than “port by accident”

- Undefined decoded normal W in standalone SSR.
- Normal south-pole singularity and insufficiently bounded mathematical intermediates in some helpers.
- Latent native glTF repeated factors/alpha and subtractive cutoff; glTF node/joint UBO shader arrays use a node-count define although each transform is indexed in three vec4 units (`S\class1\gltf\pbrmetallicroughnessV.glsl:198-221,235-249`; `M:292-294`). These are enabling-time concerns for its dormant draw path, not existing visible SL PBR behavior.
- Terrain RT1 alpha expression uses base-color blue.
- HDRI/emissive-buffer combination and native opaque unconditional RT3 require valid attachment/feature matching.
- FXAA luma blue coefficient .144 and water's zero-producing specular alpha expression.
- Shader uniform/sampler names are frequently historical (`specularMap` carries ORM; `emissiveRect` may mean luminance source; `diffuseRect` can mean already-lit scene color).

### Design options, not source findings

A native implementation could retain a compatibility material path while introducing fully linear buffers, octahedral normals, reversed Z, descriptor indexing, specialization constants, compute exposure/AO, or a different OIT method. None is established here as necessary or currently implemented. Each changes resource/permutation architecture and some change visible output. A parity baseline should first capture legacy classic sky, legacy auto-adjusted sky, PBR sky, masked/mirrored normal maps, metallic roughness sweeps, low-quality probe fallback, refractive/excluded water, and dense transparent stacks. Then evaluate improvements as explicit deltas, not evidence-free “equivalents.”

## 12. Validation limits

Verified repository revision and clean status; traced producer→consumer equations and manager-selected permutations. No repository edits, builds, shader compilation, screenshots, or GPU captures were performed. This document deliberately leaves attachment formats, actual render order, barriers, draw routing, material default CPU assets, history invalidation, and driver-dependent undefined behavior to the parent/backend/scene/auxiliary reports or runtime validation.
