// Scoped native UI primitive, clip and font publication. LGPL-2.1.
#pragma once
#include "vsuifontbridge.h"
class VSUIResources;
class VSUIDrawBridge
{
  public:
    VSUIDrawBridge(VSUIResources &, float dpi);
    ~VSUIDrawBridge();
    static VSUIResources* resources();
    VSUIDrawBridge(const VSUIDrawBridge &) = delete;
    VSUIDrawBridge &operator=(const VSUIDrawBridge &) = delete;

  private:
    inline static VSUIResources* sResources = nullptr;
    VSUIFontBridge mFont;
    float mScaleX, mScaleY, mDepth;
    int mOriginX, mOriginY;
};
