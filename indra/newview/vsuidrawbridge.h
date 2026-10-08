// Scoped native UI primitive, clip and font publication. LGPL-2.1.
#pragma once
#include "vsuifontbridge.h"
class VSUIResources;
class VSUIDrawBridge
{
  public:
    VSUIDrawBridge(VSUIResources &, float dpi);
    ~VSUIDrawBridge();
    VSUIDrawBridge(const VSUIDrawBridge &) = delete;
    VSUIDrawBridge &operator=(const VSUIDrawBridge &) = delete;

  private:
    VSUIFontBridge mFont;
    float mScaleX, mScaleY, mDepth;
    int mOriginX, mOriginY;
};
