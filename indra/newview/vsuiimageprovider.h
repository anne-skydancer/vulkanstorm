// CPU skin assets published through native UI facades. LGPL-2.1.
#pragma once
#include "llrender2dutils.h"
#include "lluiimage.h"
#include "vsuirenderer.h"
#include "vstexturedebug.h"
#include <memory>
#include <utility>
class VSUIResources;
class LLImageRaw;
class LLSD;
LLSD vs_ui_texture_cache_evidence(bool includePixels=false);
void vs_pump_ui_images();
std::vector<VSTextureDebugInfo> vs_native_texture_debug();
LLPointer<LLUIImage> vs_request_world_image(const LLUUID&,unsigned dimension,bool exact=false);
struct VSMapImage { LLPointer<LLUIImage> image; bool missing=false; };
VSMapImage vs_request_map_image(const std::string&, bool load=true);
void vs_retry_map_image(const std::string&);
void vs_set_map_image_visible(const std::string&,bool);
void vs_refresh_ui_image(const LLUUID&);
bool vs_publish_local_image(const LLUUID&,const LLImageRaw&);
bool vs_publish_local_images(const std::vector<std::pair<LLUUID,LLPointer<LLImageRaw>>>&);
void vs_retire_local_image(const LLUUID&);
bool vs_evict_world_image(const LLUUID& preserve);
LLUUID vs_native_image_id(const LLUUID&);
LLPointer<LLUIImage> vs_publish_ui_image(const std::string&, const LLImageRaw&,VSUIRenderer::Sampling sampling=VSUIRenderer::Sampling::Linear);
void vs_erase_ui_image(const std::string&);
std::shared_ptr<const void> vs_reserve_ui_image_bytes(std::size_t);
LLPointer<LLImageRaw> vs_ui_image_raw(const LLUUID&);
LLPointer<LLImageRaw> vs_ui_image_aux(const LLUUID&);
bool vs_ui_image_ready(const LLUUID&);
VSUIRenderer::Image vs_ui_image_snapshot(const LLUUID&);
VSUIRenderer::Image vs_ui_named_image_snapshot(const std::string&);
void vs_draw_ui_image(const VSUIRenderer::Image&,const LLRectf&,const LLColor4&);
std::optional<float> vs_ui_world_depth();
void vs_set_ui_world_depth(std::optional<float>);
class VSUIWorldDepthScope
{
    std::optional<float> mPrevious;
public:
    explicit VSUIWorldDepthScope(std::optional<float> value):mPrevious(vs_ui_world_depth()) {vs_set_ui_world_depth(value);}
    ~VSUIWorldDepthScope(){vs_set_ui_world_depth(mPrevious);}
    VSUIWorldDepthScope(const VSUIWorldDepthScope&)=delete;
    VSUIWorldDepthScope& operator=(const VSUIWorldDepthScope&)=delete;
};

bool vs_ui_image_failed(const LLUUID&);
S32 vs_ui_image_discard(const LLUUID&);
S32 vs_ui_image_components(const LLUUID&);
// Zero means the normal decoded component-to-format mapping; GL_ALPHA is an explicit local source override.
LLGLenum vs_ui_image_source_format(const LLUUID&);
std::pair<S32,S32> vs_ui_image_dimensions(const LLUUID&);
class VSUIImageProvider : public LLImageProviderInterface
{
  public:
    explicit VSUIImageProvider(VSUIResources &);
    ~VSUIImageProvider() override;
    LLPointer<LLUIImage> getUIImage(const std::string &, S32 priority = 0) override;
    LLPointer<LLUIImage> getUIImageByID(const LLUUID &, S32 priority = 0) override;
    void cleanUp() override;
    void resetAccount();
    void pumpImages();
    LLPointer<LLUIImage> requestWorldImage(const LLUUID&,unsigned dimension,bool exact=false);
    VSMapImage requestMapImage(const std::string&,bool load);
    void retryMapImage(const std::string&);
    void setMapImageVisible(const std::string&,bool);
    void refreshImage(const LLUUID&);
    bool publishLocalImage(const LLUUID&,const LLImageRaw&);
    bool publishLocalImages(const std::vector<std::pair<LLUUID,LLPointer<LLImageRaw>>>&);
    void retireLocalImage(const LLUUID&);
    bool evictWorldImage(const LLUUID& preserve);
    LLUUID imageID(const LLUUID&) const;
    LLPointer<LLUIImage> publishImage(const std::string&, const LLImageRaw&,VSUIRenderer::Sampling sampling=VSUIRenderer::Sampling::Linear);
    void eraseImage(const std::string&);
    std::shared_ptr<const void> reserveImageBytes(std::size_t);
    LLPointer<LLImageRaw> rawImage(const LLUUID&) const;
    LLPointer<LLImageRaw> auxiliaryImage(const LLUUID&) const;
    bool imageReady(const LLUUID&) const;
    VSUIRenderer::Image imageSnapshot(const LLUUID&) const;
    VSUIRenderer::Image namedImageSnapshot(const std::string&);
    void drawImage(const VSUIRenderer::Image&,const LLRectf&,const LLColor4&);
    std::optional<float> worldDepth() const;
    void setWorldDepth(std::optional<float>);
    bool imageFailed(const LLUUID&) const;
    S32 imageDiscard(const LLUUID&) const;
    S32 imageComponents(const LLUUID&) const;
    std::pair<S32,S32> imageDimensions(const LLUUID&) const;
    LLSD textureCacheEvidence(bool includePixels=false) const;
    std::vector<VSTextureDebugInfo> textureDebug() const;

  private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
