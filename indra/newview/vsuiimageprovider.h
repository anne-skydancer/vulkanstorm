// CPU skin assets published through native UI facades. LGPL-2.1.
#pragma once
#include "llrender2dutils.h"
#include <memory>
class VSUIResources;
class LLImageRaw;
void vs_pump_ui_images();
void vs_refresh_ui_image(const LLUUID&);
LLPointer<LLUIImage> vs_publish_ui_image(const std::string&, const LLImageRaw&);
void vs_erase_ui_image(const std::string&);
LLPointer<LLImageRaw> vs_ui_image_raw(const LLUUID&);
bool vs_ui_image_ready(const LLUUID&);
bool vs_ui_image_failed(const LLUUID&);
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
    void refreshImage(const LLUUID&);
    LLPointer<LLUIImage> publishImage(const std::string&, const LLImageRaw&);
    void eraseImage(const std::string&);
    LLPointer<LLImageRaw> rawImage(const LLUUID&) const;
    bool imageReady(const LLUUID&) const;
    bool imageFailed(const LLUUID&) const;

  private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
