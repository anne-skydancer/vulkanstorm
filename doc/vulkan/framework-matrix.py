import csv,pathlib
root=pathlib.Path('doc/vulkan')
Ddraw='IDeviceContext::SetPipelineState/CommitShaderResources/SetVertexBuffers/SetIndexBuffer/DrawIndexed'
Bdraw='Encoder::setState/setVertexBuffer/setIndexBuffer/setTexture/setUniform/submit'
Dpost='IDeviceContext::SetRenderTargets/SetPipelineState/CommitShaderResources/Draw; TransitionResourceStates'
Bpost='setViewFrameBuffer/setViewOrder; Encoder::setTexture/setUniform/submit'
Dview='ITexture::CreateView; IDeviceContext::SetRenderTargets/SetViewports/SetScissorRects/TransitionResourceStates'
Bview='Attachment/FrameBufferHandle; setViewFrameBuffer/setViewRect/setViewTransform/setViewOrder'
Dres='IRenderDevice::CreateBuffer/CreateTexture; IDeviceContext::UpdateBuffer/UpdateTexture/CopyTexture'
Bres='createVertexBuffer/createTexture2D; copy/makeRef; update(DynamicVertexBufferHandle)/updateTexture2D'
Dread='IDeviceContext::CopyTexture/EnqueueSignal/Flush; IFence::GetCompletedValue/Wait; MapTextureSubresource'
Bread='Encoder::blit; read(TextureRegion); frame(BGFX_FRAME_FLUSH); stable destination until returned frame'
Dshader='IRenderDevice::CreateShader/CreateGraphicsPipelineState/CreatePipelineResourceSignature; IShader::GetResourceDesc'
Bshader='shaderc SPIR-V path; createShader/createProgram/createUniform; setUniform/setTexture/setBuffer/setImage'
rows=[]
def add(id,meaning,d,b,gate,de='DC',be='BV'):
    rows.append(dict(record=id,baseline_contract=meaning,diligent_public_route=d,bgfx_public_route=b,
                     constraint_or_decision_test=gate,diligent_evidence=de,bgfx_evidence=be,
                     status='candidate source/API mapping; no integration or runtime qualification'))
frames=[
('F01','Frame/startup/disconnect/snapshot orchestration','IEngineFactoryVk::CreateDeviceAndContextsVk; IDeviceContext::Flush/FinishFrame','init/begin/end/frame; ViewId scheduler','Extract GL callbacks; bgfx commands execute by frame class rather than CPU call order.','DC/DP','BV/BH'),
('F02','Nested main/auxiliary/HUD/capture views',Dview,Bview,'No mutable global camera during recording; each bgfx pass gets dedicated configured view.','DC/DU','BV'),
('F03','MRT encodings/shared depth/water mask/post targets',Dview+'; GraphicsPipelineDesc RTVFormats/DSVFormat',Bview+'; isTextureValid/getCaps','Query actual format blend/sample/depth usages; padding and attachment order must preserve encodings.','DC/DU','BV/BU'),
('F04','Deferred sun/local/projector lighting',Dpost,Bpost+'; setState additive blend','Preserve inside/outside light volume branches and mixed G-buffer interpretation.','DC/DS','BV/BS'),
('F05','Sun/spot shadow fitting and masked draws',Dview+'; rasterizer depth bias; '+Ddraw,Bview+'; setDepthControl; '+Bdraw,'Preserve bias/clamp/compare policy; qualify depth-control and shadow filter semantics.','DC/DU','BV/BS'),
('F06','Probe face/mip capture/convolution/partial publication',Dview+'; CopyTexture/GenerateMips',Bview+'; createTextureCube; blit/dispatch','Diligent partial states explicit; bgfx whole-image state requires serialized compatible usage.','DU/DC','BV/BU'),
('F07','Hero mirrors/rates/clip/radiance filtering',Dview+'; '+Dpost,Bview+'; '+Bpost,'Respect reflected winding/clip and rate schedule; avoid history/state leakage.','DC/DU','BV/BU'),
('F08','SSR/exposure/glow/DoF/AA/RLVa/post ordering',Dpost,Bpost+'; dispatch in separate views','No generic FX substitution; bgfx compute sorts before graphics within a view.','DC/DS','BV/BS'),
('F09','Previous scene/matrices/exposure/glow history',Dres+'; explicit input SRB generations',Bres+'; persistent TextureHandle/setTexture','History follows view serial, not backend flight slot or flush-only transaction.','DC/DH','BV/BH'),
('F10','Selection/debug/wire/line/highlight overlays',Ddraw+'; BeginDebugGroup/InsertDebugLabel',Bdraw+'; setMarker/setDebug','Preserve state/blend/depth; unsupported geometry/wide-line mechanisms need bounded equivalent geometry.','DC','BV/BS')]
for r in frames:add(*r)
draws=[
('D01','Legacy simple static/rigged opaque','Bounded texture-index fetch and palette interpretation.'),
('D02','Legacy alpha-mask opaque','Material threshold/UV/depth and shadow matching.'),
('D03','Fullbright world/HUD/rigged','Exposure cancellation/gamma differs from generic unlit; preserve glow alpha.'),
('D04','Fullbright mask and separate glTF unlit route','Distinct material producer and HUD math; no route loss.'),
('D05','Grass cutout/nonindexed texture','Preserve cutoff, wind/LOD and matching shadows.'),
('D06','Tree cutout/transform/shadow','Keep CPU generation and shadow bias/cutoff.'),
('D07','Four-layer legacy/PBR terrain','Measure realized program samplers including environment/shadows; bgfx binding-stage limit and packed layer data gate.'),
('D08','Bump/shiny and generated normals','Separate callback-generated image publication; no implicit texture matrix state.'),
('D09','Legacy material permutations','Generate realized opaque/alpha routes and spec/normal/UV fields, not source-file count.'),
('D10','PBR/glTF/HUD and glow','Double-sided tangent/front-face/ORM/alpha math; distinct glTF scene routing.'),
('D11','Avatar/control/impostor/body shadows','Morph/joints remain CPU; packed palettes and conditional body/shadow routes.'),
('D12','Sorted/PPLL capture/resolve/depth replay/residual','Fragment atomic format and graphics write/read dependency proof required; bgfx real pass boundary gate.'),
('D13','Transparent/opaque/underwater/void water','Detached color/depth copy, clipping and haze policy; no active attachment feedback.'),
('D14','Water exclusion mask and private depth','Clear/mask/depth behavior and multi-plane clipping; preserve attachment writes/read ordering.'),
('D15','Alpha-only glow + bloom extraction/filter/combine','Independent RGB/A masks/factors; preserve alpha through PPLL resolve and post.'),
('D16','Dormant empty old-sky pool','No required output or active migration; reconsider only with source reachability evidence.'),
('D17','EEP sky/celestial/cloud/HDRI','Environment/view branches, depth-clear boundary, source cube orientation and exposure scaling.')]
for id,meaning,gate in draws:
    if id=='D12':d=Ddraw+'; PixelUAVWritesAndAtomics; UAV TransitionResourceStates';b=Bdraw+'; setBuffer/setImage; distinct-FBO pass-boundary candidate';de,be='DC/DS/DU','BV/BS/BU'
    elif id=='D16':d=b='No active output; no framework facility required';de=be='baseline D16'
    elif id in ('D13','D14','D15'):d=Ddraw+'; CopyTexture/SetRenderTargets';b=Bdraw+'; blit/setViewFrameBuffer';de,be='DC/DU','BV/BU'
    else:d,b,de,be=Ddraw,Bdraw,'DC/DS','BV/BS'
    add(id,meaning,d,b,gate,de,be)
for r in [
('R01','Drawing/state facade',Ddraw,Bdraw,'Extract complete explicit state; ambient gGL cache cannot remain native producer.','DC','BV'),
('R02','Vertex/index buffers and streaming',Dres,Bres,'Packed vertex/index ABI and bounded stream capacity; library owns backend allocations/retirement.','DC/DS/DH','BS/BH'),
('R03','Images/residency/eviction/destruction',Dres+'; resource reference lifetime',Bres+'; destroy','Library backend retirement plus CPU generation pinning; no fixed-frame custom deletes.','DH','BH'),
('R04','Worker upload readiness/publication',Dres+'; EnqueueSignal/Flush/DeviceWaitForFence',Bres+'; release callback/frame-generation ownership','CPU callback != GPU completion; plugin bytes retained; serialized library submission first.','DH','BH'),
('R05','Shader wrapper/linkage/state ABI',Dshader,Bshader,'Generate entry/permutation/reflection manifests; Diligent remapping vs bgfx container/HLSL conversion.','DS','BS'),
('R06','Render targets/borrowed depth/nested state',Dview,Bview,'Views borrow library texture generations; preserve shared depth/read-only layouts/load-store.','DC/DU','BV/BU'),
('R07','Cube/cube-array/mip generation',Dview+'; TEXTURE_VIEW_DESC/CopyTexture',Bview+'; createTextureCube/updateTextureCube/blit','Face indices and partial visibility; no automatic complete-cube temporal redesign.','DU','BV/BU'),
('R08','Compressed/raw image readback',Dread,Bread+'; read(BufferRegion)','Pixel/packed/compressed output contract and stride; bgfx whole-mip/dedicated readback limits.','DH','BH'),
('R09','Media dirty image publication',Dres+'; completion-ready generation',Bres+'; borrowed memory release owner','Copy shared memory or retain producer ownership; preserve untouched pixels after dirty update.','DH','BH'),
('P01','Device/capability/window initialization','CreateDeviceAndContextsVk/CreateSwapChainVk; device format/features','Init/PlatformData/getCaps/isTextureValid','Native handles and actual atomics formats; no vendor-ID-only tier selection.','DP/DU','BU/BH'),
('P02','Swap/pacing/resize','ISwapChain::Present/Resize','reset/frame; recreate/rebind framebuffers/views','Library WSI path plus generation cancellation; present vs graphics completion differs.','DP','BV/BH'),
('P03','Shutdown/loss/recovery','IDeviceContext::WaitForIdle; library resource release; restart policy','CallbackI::fatal(Fatal::DeviceLost); shutdown; restart policy','No automatic loss reconstruction proven; terminate pending waits/consumers and producer jobs.','DP/DH','BH'),
('B01','Autobuild/dependency/toolchain/staging','Pinned Diligent CMake Vulkan core/module package; offline SPIR-V/reflection assets','Pinned bgfx/bx/bimg/shaderc package; shader binary/container assets','No SDK installation/build here; closed manifest/notices/platform runtime staging must qualify.','Diligent pinned CMake/license','bgfx pinned config/license')]:add(*r)
scenes=[
('S01','Scene primitives/sculpt/flexible geometry','Keep simulation/geometry semantics; versioned uploads and origin shifts.'),
('S02','Downloaded mesh/LOD/batches','Keep missing-LOD fallback/face IDs/order; library buffer layout/update capacity.'),
('S03','Culling/occlusion/render masks','CPU culling retained; library queries asynchronous; no false rejection after teleport.'),
('S04','Avatar morph/joints/attachments/rigging','CPU joint/morph outputs; palette reflection and rigged/HUD/impostor identity.'),
('S05','Terrain mesh/layer/seam generation','CPU patch topology; custom shaders and descriptor pressure.'),
('S06','Particles/ribbons/HUD clouds','CPU permissions/simulation; source custom blends and deterministic order.'),
('S07','Fetch/decode/sculpt raw/residency','Network/cache unchanged; CPU alpha masks retained through texture generations.'),
('S08','TE/material/PBR overrides/UV animation','Versioned material bindings and draw-class changes; no generic material substitution.'),
('S09','Separate glTF scenes/nodes/skins','Keep node/primitive identities; storage palette/material slices and accepted extension gap.'),
('S10','Local avatar composite/bake RGBA+morph mask','Preserve synchronous legacy consumer and five-channel wire encoding; bgfx frame bridge gate.')]
for id,meaning,gate in scenes:
    if id=='S10':d,b,de,be=Dpost+'; '+Dread,Bpost+'; '+Bread,'DC/DH','BV/BH'
    elif id=='S03':d,b,de,be=Ddraw+'; BeginQuery/EndQuery/IQuery::GetData',Bdraw+'; createOcclusionQuery/getResult/setCondition','DC/DH','BV/BH'
    else:d,b,de,be=Dres+'; '+Ddraw,Bres+'; '+Bdraw,'DC/DS/DH','BV/BS/BH'
    add(id,meaning,d,b,gate,de,be)
for r in [
('U01','Ordered UI/widgets/images/scissor',Ddraw+'; SetScissorRects',Bdraw+'; setViewMode(Sequential)/setScissor','Contiguous compatible batching only; XUI behavior untouched.','DC','BV'),
('U02','FreeType/glyph atlas/text shadows',Dres+'; '+Ddraw,Bres+'; '+Bdraw,'CPU metrics/rasterization unchanged; atlas dirty generations/format/color emoji.','DC/DH','BV/BH'),
('U03','World labels/HUD icons/text',Ddraw+'; view/depth policy',Bdraw+'; dedicated view transform','CPU labels/visibility retained; distinct world depth vs screen-only paths.','DC','BV'),
('U04','3D HUD attachment/selection view',Dview+'; '+Ddraw,Bview+'; '+Bdraw,'Separate HUD depth/material/particle/RLVa zoom policy; not flat UI.','DC','BV'),
('U05','Minimap/worldmap/image tiles',Dres+'; '+Ddraw,Bres+'; '+Bdraw,'CPU raster/tile ownership/coordinate restrictions; ordered icons/lines.','DC/DH','BV/BH'),
('U06','Browser/media texture generations',Dres,Bres,'Plugin shared memory copied/retained beyond lock; orientation/channel/dirty-region conversion.','DH','BH'),
('A01','Color/depth/tiled snapshots',Dview+'; '+Dread,Bview+'; '+Bread,'No normal present required; row order/tile/UI/no-post policy and depth copy/packing.','DC/DH','BV/BH'),
('A02','CPU picking + GPU depth consumers','CPU ray intersection retained; '+Dread,'CPU ray intersection retained; '+Bread,'No generic GPU ID replacement; asynchronous depth latency versus exact caller contract.','DH','BH'),
('A03','Dynamic previews/PBR thumbnails',Dview+'; '+Ddraw,Bview+'; '+Bdraw,'Flatten mutable callbacks before recording; preserve camera/scissor/depth and selected texture.','DC/DH','BV/BH'),
('A04','Avatar impostor/profile images',Dview+'; '+Ddraw,Bview+'; '+Bdraw,'Retain silhouette/framing/update hysteresis and offscreen avatar/attachment policies; live appearance hints are A03/S10 consumers.','DC/DH','BV/BH'),
('A05','HUD selection/debug auxiliary paths',Ddraw+'; BeginDebugGroup/InsertDebugLabel',Bdraw+'; setMarker/setDebug','Preserve state/widths/depth; bounded geometry replacement only when needed.','DC','BV/BS'),
('A06','Scene loading monitor capture/comparison/query',Dpost+'; BeginQuery/EndQuery/IQuery::GetData; '+Dread,Bpost+'; createOcclusionQuery/getResult; '+Bread,'Preserve pre-finalization sampled stage, frozen-scene and comparison timing; query/reduction results completion-driven.','DC/DH','BV/BH')]:add(*r)
families=[
('H01','Avatar helper/palette shaders','Palette packing, weights and all rigged/shadow consumers.'),
('H02','Deferred/material/light/alpha/post shader family','Realized permutations, MRT encodings, PPLL stage/resources and post quality.'),
('H03','Glow extraction/blur/combine','Reference taps/noise/alpha/color and downsample extents.'),
('H04','Water/fog/conversion shader family','Detached scene/depth, plane/time/EEP inputs and underwater ordering.'),
('H05','glTF node/material/joint blocks','Exact block layout/range/binding plus alpha/unlit/multi-UV routes; bgfx conversion gate.'),
('H06','UI/debug/aux/probe/bake/occlusion shader family','Split by semantic role; optional geometry debug alternatives.'),
('H07','Classic lighting linked helper family','Explicit light/view inputs and original selection/attenuation.'),
('H08','Objects/indexed texture/preview shaders','Current active indexed texture channels are four; preserve packed bit carrier or qualify repacking.'),
('H09','Vignette/snapshot frame post helpers','Capture inclusion and normalized-vs-pixel coordinates.'),
('H10','Dormant root error-entry shaders','No required production route; optional diagnostic program only as deliberate design.'),
('H11','WindLight/environment shared functions','View-specific environment/math/varyings; no state leakage between main/probe/capture.')]
for id,meaning,gate in families:add(id,meaning,Dshader,Bshader,gate,'DS/DU' if id=='H02' else 'DS','BS/BU' if id=='H02' else 'BS')
expected={r['id'] for r in csv.DictReader((root/'coverage-ledger.csv').open())}
assert len(rows)==len({r['record'] for r in rows})==73
assert {r['record'] for r in rows}==expected
with (root/'framework-contract-matrix.csv').open('w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
print('Mapped all 73 existing IDs to both public API routes and explicit remaining gates.')
