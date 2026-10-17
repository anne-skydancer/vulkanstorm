/**
 * @file llwindowheadless.h
 * @brief Headless definition of LLWindow class
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLWINDOWHEADLESS_H
#define LL_LLWINDOWHEADLESS_H

#include "llwindow.h"

class LLWindowHeadless : public LLWindow
{
public:
    /*virtual*/ void show() override {}
    /*virtual*/ void hide() override {}
    /*virtual*/ void close() override {}
    /*virtual*/ bool getVisible() override {return false;}
    /*virtual*/ bool getMinimized() override {return mMinimized;}
    /*virtual*/ bool getMaximized() override {return false;}
    /*virtual*/ bool maximize() override {return false;}
    /*virtual*/ void minimize() override {mMinimized=true;}
    /*virtual*/ void restore() override {mMinimized=false;}
    // TODO: LLWindow::getFullscreen() is (intentionally?) NOT virtual.
    // Apparently the coder of LLWindowHeadless didn't realize that. Is it a
    // mistake to shadow the base-class method with an LLWindowHeadless
    // override when called on the subclass, yet call the base-class method
    // when indirecting through a polymorphic pointer or reference?
    bool getFullscreen() {return false;}
    /*virtual*/ bool getPosition(LLCoordScreen *position) override { if (!position) return false;*position=mPosition;return true; }
    /*virtual*/ bool getSize(LLCoordScreen *size) override { if (!size) return false;*size=LLCoordScreen(mSize.mX,mSize.mY);return true; }
    /*virtual*/ bool getSize(LLCoordWindow *size) override { if (!size) return false;*size=mSize;return true; }
    /*virtual*/ bool setPosition(LLCoordScreen position) override {mPosition=position;return true;}
    /*virtual*/ bool setSizeImpl(LLCoordScreen size) override { return setSizeImpl(LLCoordWindow(size.mX,size.mY)); }
    /*virtual*/ bool setSizeImpl(LLCoordWindow size) override { if(size.mX<0 || size.mY<0)return false;mSize=size;return true; }
    /*virtual*/ bool switchContext(bool fullscreen, const LLCoordScreen &size, bool enable_vsync, const LLCoordScreen * const posp = NULL) override {return false;}
    void* createSharedContext() override  { return nullptr; }
    void makeContextCurrent(void*) override  {}
    void destroySharedContext(void*) override  {}
    /*virtual*/ void toggleVSync(bool enable_vsync) override { }
    /*virtual*/ bool setCursorPosition(LLCoordWindow position) override {mCursor=position;return true;}
    /*virtual*/ bool getCursorPosition(LLCoordWindow *position) override {if(!position)return false;*position=mCursor;return true;}
#if LL_WINDOWS
    /*virtual*/ bool getCursorDelta(LLCoordCommon* delta) override { return false; }
#endif
    /*virtual*/ bool isWrapMouse() const override { return true; }
    /*virtual*/ void showCursor() override {mCursorHidden=false;mHideCursorPermanent=false;}
    /*virtual*/ void hideCursor() override {mCursorHidden=true;mHideCursorPermanent=true;}
    /*virtual*/ void showCursorFromMouseMove() override {if(!mHideCursorPermanent)showCursor();}
    /*virtual*/ void hideCursorUntilMouseMove() override {if(!mHideCursorPermanent)mCursorHidden=true;}
    /*virtual*/ bool isCursorHidden() override {return mCursorHidden;}
    /*virtual*/ void updateCursor() override {mCurrentCursor=mNextCursor;}
    //virtual ECursorType getCursor() override { return mCurrentCursor; }
    /*virtual*/ void captureMouse() override {}
    /*virtual*/ void releaseMouse() override {}
    /*virtual*/ void setMouseClipping( bool b ) override {}
    /*virtual*/ bool isClipboardTextAvailable() override {return !mClipboard.empty(); }
    /*virtual*/ bool pasteTextFromClipboard(LLWString &dst) override {dst=mClipboard;return true; }
    /*virtual*/ bool copyTextToClipboard(const LLWString &src) override {mClipboard=src;return true; }
    /*virtual*/ void flashIcon(F32 seconds) override {}
    /*virtual*/ F32 getGamma() override {return 1.0f; }
    /*virtual*/ bool setGamma(const F32 gamma) override {return false; } // Set the gamma
    /*virtual*/ void setFSAASamples(const U32 fsaa_samples) override { }
    /*virtual*/ U32 getFSAASamples() override { return 0; }
    /*virtual*/ bool restoreGamma() override {return false; }   // Restore original gamma table (before updating gamma)
    //virtual ESwapMethod getSwapMethod() override { return mSwapMethod; }
    /*virtual*/ void gatherInput() override {}
    /*virtual*/ void delayInputProcessing() override {}
    /*virtual*/ void swapBuffers() override;


    // handy coordinate space conversion routines
    /*virtual*/ bool convertCoords(LLCoordScreen from, LLCoordWindow *to) override { if(!to)return false;*to=LLCoordWindow(from.mX-mPosition.mX,from.mY-mPosition.mY);return true; }
    /*virtual*/ bool convertCoords(LLCoordWindow from, LLCoordScreen *to) override { if(!to)return false;*to=LLCoordScreen(from.mX+mPosition.mX,from.mY+mPosition.mY);return true; }
    /*virtual*/ bool convertCoords(LLCoordWindow from, LLCoordGL *to) override { if(!to)return false;*to=LLCoordGL(from.mX,mSize.mY-from.mY-1);return true; }
    /*virtual*/ bool convertCoords(LLCoordGL from, LLCoordWindow *to) override { if(!to)return false;*to=LLCoordWindow(from.mX,mSize.mY-from.mY-1);return true; }
    /*virtual*/ bool convertCoords(LLCoordScreen from, LLCoordGL *to) override { LLCoordWindow intermediate;return convertCoords(from,&intermediate)&&convertCoords(intermediate,to); }
    /*virtual*/ bool convertCoords(LLCoordGL from, LLCoordScreen *to) override { LLCoordWindow intermediate;return convertCoords(from,&intermediate)&&convertCoords(intermediate,to); }

    /*virtual*/ LLWindowResolution* getSupportedResolutions(S32 &num_resolutions) override { return NULL; }
    /*virtual*/ F32 getNativeAspectRatio() override { return 1.0f; }
    /*virtual*/ F32 getPixelAspectRatio() override { return 1.0f; }
    /*virtual*/ void setNativeAspectRatio(F32 ratio) override {}

    /*virtual*/ void *getPlatformWindow() override { return 0; }
    /*virtual*/ void bringToFront() override {}

    LLWindowHeadless(LLWindowCallbacks* callbacks,
        const std::string& title, const std::string& name,
        S32 x, S32 y,
        S32 width, S32 height,
        U32 flags,  bool fullscreen, bool clear_background,
        bool enable_vsync, bool use_gl, bool ignore_pixel_depth);
    virtual ~LLWindowHeadless();

private:
    bool mMinimized=false;
    LLCoordWindow mSize,mCursor;
    LLCoordScreen mPosition;
    LLWString mClipboard; // Process-local clipboard; never touches the desktop.
};

class LLSplashScreenHeadless : public LLSplashScreen
{
public:
    LLSplashScreenHeadless() {}
    virtual ~LLSplashScreenHeadless() {}

    /*virtual*/ void showImpl() override {}
    /*virtual*/ void updateImpl(const std::string& mesg) override {}
    /*virtual*/ void hideImpl() override {}

};

#endif //LL_LLWINDOWHEADLESS_H

