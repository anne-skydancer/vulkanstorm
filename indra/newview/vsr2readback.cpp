// Compare actual GPU texture bytes against bottom-row-first compositor pixels.
// No OpenGL context or surrogate texture publication is involved. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsr2readback.h"
#include "vsvulkancontext.h"
#include "llimage.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#pragma push_macro("Bool")
#pragma push_macro("False")
#pragma push_macro("True")
#pragma push_macro("MAP_TYPE")
#undef Bool
#undef False
#undef True
#undef MAP_TYPE
#include <DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <DiligentCore/Common/interface/RefCntAutoPtr.hpp>
#pragma pop_macro("MAP_TYPE")
#pragma pop_macro("True")
#pragma pop_macro("False")
#pragma pop_macro("Bool")
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* failure)
{
    if (!condition)
        throw std::runtime_error(failure);
}
void write(const std::filesystem::path& path, const std::vector<U8>& bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    require(output.good(), "R2 composite readback artifact write failed");
}
} // namespace

void vs_r2_capture_bakes(VSVulkanContext& owner, std::vector<VSWorldAvatar::LocalBake> bakes, const std::string& directory,
                         const std::shared_ptr<bool>& complete)
{
    owner.setFrameObserver(
        [bakes = std::move(bakes), directory, complete](auto* device, auto* context, auto*, auto, auto, auto, const auto&)
        {
            if (*complete)
                return;
            using namespace Diligent;
            LLSD report;
            for (const auto& bake : bakes)
            {
                require(bake.image && bake.color && bake.color->getComponents() == 4, "R2 composite lacks an actual RGBA publication");
                require(VSUIRenderer::generation(bake.image) == bake.generation, "R2 composite image generation changed before readback");
                auto* source = VSUIRenderer::imageView(bake.image)->GetTexture();
                auto  desc   = source->GetDesc();
                require(desc.Width == U32(bake.color->getWidth()) && desc.Height == U32(bake.color->getHeight()) && desc.Width <= 2048 &&
                            desc.Height <= 2048 && desc.Format == TEX_FORMAT_RGBA8_UNORM,
                        "R2 composite GPU format/extent differs from its CPU producer");
                desc.Name           = "R2 avatar composite actual GPU readback";
                desc.Usage          = USAGE_STAGING;
                desc.BindFlags      = BIND_NONE;
                desc.CPUAccessFlags = CPU_ACCESS_READ;
                RefCntAutoPtr<ITexture> staging;
                device->CreateTexture(desc, nullptr, &staging);
                require(staging != nullptr, "R2 composite staging allocation failed");
                context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                CopyTextureAttribs copy{};
                copy.pSrcTexture              = source;
                copy.pDstTexture              = staging;
                copy.SrcTextureTransitionMode = copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                context->CopyTexture(copy);
                context->WaitForIdle();
                MappedTextureSubresource mapped{};
                context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
                require(mapped.pData != nullptr, "R2 composite GPU readback mapping failed");
                std::vector<U8> actual(std::size_t(desc.Width) * desc.Height * 4), expected(actual.size());
                for (unsigned y = 0; y < desc.Height; ++y)
                {
                    const auto row = std::size_t(desc.Width) * 4;
                    std::copy_n(static_cast<const U8*>(mapped.pData) + y * mapped.Stride, row, actual.data() + y * row);
                    std::copy_n(bake.color->getData() + (desc.Height - 1 - y) * row, row, expected.data() + y * row);
                }
                context->UnmapTextureSubresource(staging, 0, 0);
                const auto prefix = std::filesystem::path(directory) / ("bake-" + std::to_string(bake.index));
                write(prefix.string() + "-actual.rgba", actual);
                write(prefix.string() + "-expected.rgba", expected);
                LLSD row;
                row["index"]      = S32(bake.index);
                row["width"]      = S32(desc.Width);
                row["height"]     = S32(desc.Height);
                row["generation"] = ll_sd_from_U64(bake.generation);
                row["row_order"]  = "top-first";
                row["channels"]   = "RGBA";
                row["bytes"]      = S32(actual.size());
                row["passed"]     = actual == expected;
                report["bakes"].append(row);
                std::ofstream output(std::filesystem::path(directory) / "bake-readbacks.xml");
                LLSDSerialize::toPrettyXML(report, output);
                require(output.good(), "R2 composite GPU report write failed");
                require(actual == expected, "R2 actual composite GPU pixels differ from CPU pixels or orientation");
            }
            *complete = true;
        });
}
