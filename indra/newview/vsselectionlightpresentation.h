// Genuine selection-owned light-radius presentation. LGPL-2.1.
#pragma once
#include "vsworldpaint.h"
class VSWorldScene;
class VSNativeSession;
class VSSelectionLightPresentation
{
public:
    VSSelectionLightPresentation(VSWorldRenderer&,VSWorldScene&);
    void append(VSNativeSession&,bool showHUD,std::vector<VSWorldRenderer::Packet>&);
    void reset();
private:
    VSWorldScene&mWorld;std::function<bool()>mCurrent;VSWorldPaint mPaint;
};
