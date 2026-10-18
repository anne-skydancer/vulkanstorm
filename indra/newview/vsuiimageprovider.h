// CPU skin assets published through native UI facades. LGPL-2.1.
#pragma once
#include "llrender2dutils.h"
#include "lluiimage.h"
#include <memory>
#include <utility>
class VSUIResources;
class LLImageRaw;
class LLSD;
LLSD vs_ui_texture_cache_evidence(bool includePixels=false);
void vs_pump_ui_images();
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
LLPointer<LLUIImage> vs_publish_ui_image(const std::string&, const LLImageRaw&);
void vs_erase_ui_image(const std::string&);
LLPointer<LLImageRaw> vs_ui_image_raw(const LLUUID&);
LLPointer<LLImageRaw> vs_ui_image_aux(const LLUUID&);
bool vs_ui_image_ready(const LLUUID&);
bool vs_ui_image_failed(const LLUUID&);
S32 vs_ui_image_discard(const LLUUID&);
S32 vs_ui_image_components(const LLUUID&);
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
    LLPointer<LLUIImage> publishImage(const std::string&, const LLImageRaw&);
    void eraseImage(const std::string&);
    LLPointer<LLImageRaw> rawImage(const LLUUID&) const;
    LLPointer<LLImageRaw> auxiliaryImage(const LLUUID&) const;
    bool imageReady(const LLUUID&) const;
    bool imageFailed(const LLUUID&) const;
    S32 imageDiscard(const LLUUID&) const;
    S32 imageComponents(const LLUUID&) const;
    std::pair<S32,S32> imageDimensions(const LLUUID&) const;
    LLSD textureCacheEvidence(bool includePixels=false) const;

  private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
