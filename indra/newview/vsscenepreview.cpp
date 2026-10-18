// Original preview cameras and CPU avatar geometry, rendered on Vulkan.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsscenepreview.h"
#include "vsvulkancontext.h"
#include "vsworldavatar.h"
#include "vsnativeim.h"
#include "llviewerwindow.h"
#include "llavatarappearance.h"
#include "vsuiresources.h"
#include "llrender.h"
#include "llfloaterreg.h"
#include "llfloater.h"
#include "llmaterialeditor.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

void vs_retire_scene_preview_floaters()
{
    // Own-avatar animation previews hold motion/pause handles. Release them
    // while the original account character and tool owners still exist.
    for (const char* name : {"upload_anim_bvh", "upload_anim_anim", "upload_image", "upload_model",
        "gestures", "material_editor", "live_material_editor"})
    {
        std::vector<LLHandle<LLFloater>> owners;
        for (auto* view : LLFloaterReg::getFloaterList(name)) owners.push_back(view->getHandle());
        for (const auto& owner : owners)
            if (auto* view = owner.get())
            {
                const LLSD key = view->getKey();
                view->closeFloater(true);
                if (owner.get()) LLFloaterReg::destroyInstance(name, key);
            }
    }
    LLFloaterComboOptions::retireNativeAccount();
}

VSScenePreview::VSScenePreview(VSVulkanContext& context) :
    mContext(&context), mCurrent(vs_native_im_guard()), mRenderer(context.createPreviewRenderer())
{
    LLUUID source;
    source.generate();
    mSourceKey = "preview-source:" + source.asString();
}
VSScenePreview::~VSScenePreview()
{
    if (gViewerWindow && gViewerWindow->nativeContext() == mContext)
        mContext->resources().erase(mSourceKey);
}
VSUIRenderer::Image VSScenePreview::sourceImage(const LLImageRaw& raw)
{
    if (!current()) return {};
    if (!mContext->resources().publish(mSourceKey, raw))
    {
        mContext->resources().deferDraw();
        return {};
    }
    return mContext->resources().image(mSourceKey);
}
bool VSScenePreview::current() const
{
    return mCurrent() && gViewerWindow && gViewerWindow->nativeContext() == mContext;
}
VSWorldRenderer& VSScenePreview::renderer() const { return *mRenderer; }
VSWorldAvatar& VSScenePreview::avatar()
{
    if (!current()) throw std::logic_error("Retired native preview owner");
    if (!mAvatar)
    {
        LLUUID identity;
        identity.generate();
        mAvatar = std::make_unique<VSWorldAvatar>(identity, false, 0, *mRenderer, mContext->resources(),
            [](const LLVector3& point, LLVector3& ground, LLVector3& normal)
            { ground = LLVector3(point.mV[0], point.mV[1], 0); normal = LLVector3::z_axis; return true; },
            std::shared_ptr<const void>{}, false, true);
        std::vector<S32> meshes;
        using namespace LLAvatarAppearanceDefines;
        for (const auto& [index, entry] : LLAvatarAppearance::getDictionary()->getMeshEntries())
            if (index != MESH_ID_HAIR && index != MESH_ID_SKIRT) meshes.push_back(index);
        mAvatar->configurePreview(meshes);
    }
    return *mAvatar;
}
bool VSScenePreview::render(unsigned width, unsigned height, const LLVector3& eye, const LLVector3& focus,
    const LLVector3& up, F32 fov, F32 nearClip, F32 farClip,
    const std::vector<VSWorldRenderer::Packet>& packets, const std::array<float,4>& background)
{
    if (!current() || !width || !height || !eye.isFinite() || !focus.isFinite() || !up.isFinite() ||
        !std::isfinite(fov) || fov <= 0 || fov >= F_PI || !std::isfinite(nearClip) || !std::isfinite(farClip) ||
        nearClip <= 0 || farClip <= nearClip || (focus-eye).lengthSquared() == 0 || up.lengthSquared() == 0)
        return false;
    const auto vector = [](const LLVector3& value) { return glm::vec3(value.mV[0], value.mV[1], value.mV[2]); };
    const auto projection = glm::perspectiveRH_NO(fov, F32(width) / height, nearClip, farClip) *
        glm::lookAtRH(vector(eye), vector(focus), vector(up));
    VSWorldRenderer::View view;
    view.preserveOrder = true;
    view.glow = false; // Original RGB dynamic previews have no glow attachment.
    std::copy_n(glm::value_ptr(projection), 16, view.projection.data());
    auto rgbPackets = packets;
    for (auto& packet : rgbPackets)
    {
        if (!packet.blend)
        {
            VSWorldRenderer::Blend blend;
            blend.source = packet.alpha ? LLRender::BF_SOURCE_ALPHA : LLRender::BF_ONE;
            blend.destination = packet.alpha ? LLRender::BF_ONE_MINUS_SOURCE_ALPHA : LLRender::BF_ZERO;
            packet.blend = blend;
        }
        packet.blend->writeMask &= 7; // Keep cleared alpha=1, as the original RGB target does.
    }
    auto clear = background;
    clear[3] = 1;
    const auto replacement = mContext->renderPreview(*mRenderer, width, height, view, rgbPackets, clear);
    if (!replacement)
    {
        mContext->resources().deferDraw();
        return false;
    }
    mImage = replacement;
    return true;
}
