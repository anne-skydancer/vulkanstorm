// Transpose display_update_camera's policy without entering a GL world.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vscameraview.h"
#include "vsregionlimits.h"
#include "llagentcamera.h"
#include "llviewercontrol.h"
#include "llviewercamera.h"
#include "llviewertexture.h"
#include "llmemory.h"
#include "llframetimer.h"
F32 vs_camera_far_clip(bool cubeSnapshot)
{
    // Cut draw distance in half when customizing avatar,
    // but on the viewer only.
    F32 final_far = gAgentCamera.mDrawDistance;

    // <FS:TJ> [FIRE-35748] Only enable the LL Draw Distance VRAM optimization when the setting is enabled
    static LLCachedControl<bool> use_vram_optimization(gSavedSettings, "FSDrawDistanceVRAMOptimization", false);
    // </FS:TJ>

    if (cubeSnapshot)
    {
        static LLCachedControl<F32> reflection_probe_draw_distance(gSavedSettings, "RenderReflectionProbeDrawDistance", 64.f);
        final_far = reflection_probe_draw_distance();
    }
    else if (CAMERA_MODE_CUSTOMIZE_AVATAR == gAgentCamera.getCameraMode())
    {
        final_far *= 0.5f;
    }
    // <FS:TJ> [FIRE-35748] Only enable the LL Draw Distance VRAM optimization when the setting is enabled
    //else if (LLViewerTexture::sDesiredDiscardBias > 2.f)
    else if (use_vram_optimization && LLViewerTexture::sDesiredDiscardBias > 2.f)
    // </FS:TJ>
    {
        final_far = llmax(32.f, final_far / (LLViewerTexture::sDesiredDiscardBias - 1.f));
    }
// <FS:CR> Aurora sim
    if(vs_region_limits().getLockedDrawDistance())
    {
        //Reset the draw distance and do not update with the new val
        final_far = LLViewerCamera::getInstance()->getFar();
    }
// </FS:CR> Aurora sim
    return final_far;
}

F32 vs_agent_draw_distance(bool prioritizeNearby)
{
    static F32 last_draw_disatance_step = 1024;
    F32 memory_limited_draw_distance = gAgentCamera.mDrawDistance;

    if (LLViewerTexture::isSystemMemoryCritical())
    {
        // If we are low on memory, reduce requested draw distance
        memory_limited_draw_distance = llmax(gAgentCamera.mDrawDistance / LLMemory::getSystemMemoryBudgetFactor(), gAgentCamera.mDrawDistance / 2.f);
    }

    if (prioritizeNearby)
    {
        // Inform interest list, prioritize closer area.
        // Reason: currently server doesn't distance sort attachments, by restricting range
        // we reduce the number of attachments sent to the viewer, thus prioritizing
        // closer ones.
        // Todo: revise and remove once server gets distance sorting.
        last_draw_disatance_step = llmax((F32)(gAgentCamera.mDrawDistance / 2.f), 50.f);
        last_draw_disatance_step = llmin(last_draw_disatance_step, memory_limited_draw_distance);
        return last_draw_disatance_step;
    }
    else if (last_draw_disatance_step < memory_limited_draw_distance)
    {
        static LLFrameTimer last_step_time;
        if (last_step_time.getElapsedTimeF32() > 1.f)
        {
            // gradually increase draw distance
            last_step_time.reset();
            F32 step = memory_limited_draw_distance * 0.1f;
            last_draw_disatance_step = llmin(last_draw_disatance_step + step, memory_limited_draw_distance);
        }
        return last_draw_disatance_step;
    }
    else
    {
        last_draw_disatance_step = memory_limited_draw_distance;
        return memory_limited_draw_distance;
    }

}
