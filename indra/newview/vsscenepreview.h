// Native targets for the viewer's existing 3D preview controllers. LGPL-2.1.
#pragma once
#include "vsworldrenderer.h"
#include "v3math.h"
#include <functional>
#include <memory>
#include <string>
class VSVulkanContext;
class VSWorldAvatar;
class LLImageRaw;
void vs_retire_scene_preview_floaters();
class VSScenePreview
{
public:
    explicit VSScenePreview(VSVulkanContext&);
    ~VSScenePreview();
    VSWorldRenderer& renderer() const;
    VSWorldAvatar& avatar();
    bool render(unsigned width, unsigned height, const LLVector3& eye, const LLVector3& focus,
        const LLVector3& up, F32 fov, F32 nearClip, F32 farClip,
        const std::vector<VSWorldRenderer::Packet>&, const std::array<float,4>& background);
    const VSUIRenderer::Image& image() const { return mImage; }
    VSUIRenderer::Image sourceImage(const LLImageRaw&);
    bool current() const;
private:
    VSVulkanContext* mContext;
    std::function<bool()> mCurrent;
    std::shared_ptr<VSWorldRenderer> mRenderer;
    std::unique_ptr<VSWorldAvatar> mAvatar;
    VSUIRenderer::Image mImage;
    std::string mSourceKey;
};
