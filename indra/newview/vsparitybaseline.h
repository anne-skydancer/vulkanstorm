// Development-only captures of normal viewer frames. LGPL-2.1.
#pragma once
namespace Diligent { struct IRenderDevice; struct IDeviceContext; struct ITextureView; }
void vs_parity_baseline_frame_begin();
void vs_parity_baseline_before_render();
void vs_parity_baseline_capture_gl();
void vs_parity_baseline_capture_vk(Diligent::IRenderDevice*, Diligent::IDeviceContext*,
                                  Diligent::ITextureView*, unsigned, unsigned);
