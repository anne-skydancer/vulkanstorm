// Scoped connection of existing viewer font geometry to native packets. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsuifontbridge.h"
#include "vsuiresources.h"
#include "llfontgl.h"
#include "llfontfreetype.h"
#include "llgl.h"
#include <stdexcept>

VSUIFontBridge::VSUIFontBridge(VSUIResources& resources) : mResources(resources)
{
    if (gGLManager.mInited || LLFontGL::hasNativeDraw())
        throw std::logic_error("Native font bridge requires exclusive pre-GL ownership");
    mOwnsFontManager=gFontManagerp==nullptr;
    if (mOwnsFontManager) LLFontManager::initClass();
    try { LLFontGL::setNativeDraw(
        [&resources](LLImageRaw* raw,S32 generation,const LLVector4a* p,const LLVector2* uv,const LLColor4U* color,S32 count)
        { resources.fontBatch(raw,generation,p,uv,color,count); },
        [&resources](const LLRectf& r,const LLColor4& color) { resources.rectangle(r,color); }); }
    catch (...)
    {
        if (mOwnsFontManager) LLFontManager::cleanupClass();
        throw;
    }
}
VSUIFontBridge::~VSUIFontBridge()
{
    LLFontGL::setNativeDraw({});
    mResources.releaseFontPages();
    if (mOwnsFontManager) LLFontManager::cleanupClass();
}
