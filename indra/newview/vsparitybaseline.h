// Development-only captures of normal viewer frames. LGPL-2.1.
#pragma once
#include <chrono>
namespace Diligent { struct IRenderDevice; struct IDeviceContext; struct ITextureView; }
class LLSD;
void vs_parity_baseline_metrics(const LLSD&);
class VSParityBaselineTimer
{
public:
    explicit VSParityBaselineTimer(const char*);
    ~VSParityBaselineTimer();
private:
    const char* mName;
    std::chrono::steady_clock::time_point mStart;
};
void vs_parity_baseline_frame_begin();
void vs_parity_baseline_before_render();
void vs_parity_baseline_after_present(double milliseconds);
void vs_parity_baseline_capture_gl();
void vs_parity_baseline_capture_vk(Diligent::IRenderDevice*, Diligent::IDeviceContext*,
                                  Diligent::ITextureView*, unsigned, unsigned);
