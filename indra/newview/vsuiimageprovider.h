// CPU skin assets published through native UI facades. LGPL-2.1.
#pragma once
#include "llrender2dutils.h"
#include <memory>
class VSUIResources;
class VSUIImageProvider : public LLImageProviderInterface
{
  public:
    explicit VSUIImageProvider(VSUIResources &);
    ~VSUIImageProvider() override;
    LLPointer<LLUIImage> getUIImage(const std::string &, S32 priority = 0) override;
    LLPointer<LLUIImage> getUIImageByID(const LLUUID &, S32 priority = 0) override;
    void cleanUp() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
