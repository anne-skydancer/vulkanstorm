// Scoped native UI primitive, clip and font publication. LGPL-2.1.
#include "vsuidrawbridge.h"
#include "llfontgl.h"
#include "llrender2dutils.h"
#include "llviewerprecompiledheaders.h"
#include "vsuiresources.h"
#include <cmath>
#include <stdexcept>
VSUIDrawBridge::VSUIDrawBridge(VSUIResources &resources, float dpi)
    : mFont(resources), mScaleX(LLFontGL::sScaleX), mScaleY(LLFontGL::sScaleY), mDepth(LLFontGL::sCurDepth),
      mOriginX(LLFontGL::sCurOrigin.mX), mOriginY(LLFontGL::sCurOrigin.mY)
{
    if (sResources || !std::isfinite(dpi) || dpi <= 0 || !LLFontGL::sOriginStack.empty())
        throw std::logic_error("Invalid native UI scale or inherited matrix stack");
    LLRender2D::setNativeUI([&resources](const LLRectf &r, const LLColor4 &c) { resources.rectangle(r, c); },
                            [&resources](const LLRect *r) { resources.screenClip(r); });
    LLRender2D::setNativeTriangles([&resources](const LLRender2D::native_triangle_t& v) { resources.triangle(v); });
    sResources = &resources;
    LLFontGL::sScaleX = LLFontGL::sScaleY = dpi;
    LLRender2D::loadIdentity();
}
VSUIDrawBridge::~VSUIDrawBridge()
{
    LLRender2D::setNativeTriangles({});
    sResources = nullptr;
    LLRender2D::setNativeUI({}, {});
    LLFontGL::sScaleX = mScaleX;
    LLFontGL::sScaleY = mScaleY;
    LLFontGL::sCurDepth = mDepth;
    LLFontGL::sCurOrigin.mX = mOriginX;
    LLFontGL::sCurOrigin.mY = mOriginY;
    LLFontGL::sOriginStack.clear();
}

VSUIResources* VSUIDrawBridge::resources() { return sResources; }
