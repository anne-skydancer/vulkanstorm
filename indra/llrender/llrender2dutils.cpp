/**
 * @file llrender2dutils.cpp
 * @brief GL function implementations for immediate-mode gl drawing.
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

#include "linden_common.h"

// Linden library includes
#include "v2math.h"
#include "m3math.h"
#include "v4color.h"
#include "llfontgl.h"
#include "llrender.h"
#include "llrect.h"
#include "llgl.h"
#include "lltexture.h"
#include "llfasttimer.h"

// Project includes
#include "llrender2dutils.h"
#include "lluiimage.h"
#include <stdexcept>

namespace
{
LLRender2D::native_rect_t native_rectangle;
LLRender2D::native_clip_t native_clip;
std::function<void(const LLRender2D::native_triangle_t&)> native_triangle;
std::vector<std::array<F32,2>> native_origins;
F32 native_line_width=1.f;
U8 native_blend=LLRender::BT_ALPHA;
}
void LLRender2D::setNativeUI(native_rect_t rectangle,native_clip_t clip)
{
    if (bool(rectangle)!=bool(clip) || (rectangle && native_rectangle))
        throw std::logic_error("Native UI drawing requires exclusive rectangle/clip ownership");
    native_rectangle=std::move(rectangle);native_clip=std::move(clip);
    native_blend=LLRender::BT_ALPHA;native_line_width=1.f;native_origins.clear();if (native_rectangle) native_origins.push_back({0,0});
}
void LLRender2D::setNativeTriangles(std::function<void(const native_triangle_t&)> callback)
{
    if (callback && (!isNativeUI() || native_triangle)) throw std::logic_error("Native triangles require exclusive drawing ownership");
    native_triangle = std::move(callback);
}
void LLRender2D::nativeTriangle(const native_triangle_t& vertices)
{
    if (!native_triangle) throw std::logic_error("Native UI triangle publication is unavailable");
    auto physical = vertices;
    const auto origin = nativeOrigin();
    for (auto& v : physical)
    {
        v[0] = (v[0] + origin[0]) * LLFontGL::sScaleX;
        v[1] = (v[1] + origin[1]) * LLFontGL::sScaleY;
    }
    native_triangle(physical);
}
bool LLRender2D::isNativeUI() { return bool(native_rectangle); }
void LLRender2D::setSceneBlendType(U8 type)
{
    if (!isNativeUI()) { gGL.setSceneBlendType(static_cast<LLRender::eBlendType>(type));return; }
    if (type!=LLRender::BT_ALPHA && type!=LLRender::BT_ADD && type!=LLRender::BT_ADD_WITH_ALPHA)
        throw std::logic_error("Native UI blend mode is not admitted");
    native_blend=type;
}
U8 LLRender2D::nativeBlend() { return native_blend; }
bool vs_native_ui_active() { return LLRender2D::isNativeUI(); }
std::array<F32,2> LLRender2D::nativeOrigin() { return native_origins.back(); }
void LLRender2D::nativeClip(const LLRect* rect) { native_clip(rect); }
void LLRender2D::nativeRectangle(S32 left,S32 top,S32 right,S32 bottom,const LLColor4& color,bool filled)
{
    const auto draw=[&](S32 l,S32 t,S32 r,S32 b)
    {
        if (r<=l || t<=b) return;
        const auto origin=nativeOrigin();
        native_rectangle(LLRectf((static_cast<F32>(l)+origin[0])*LLFontGL::sScaleX,
            (static_cast<F32>(t)+origin[1])*LLFontGL::sScaleY,(static_cast<F32>(r)+origin[0])*LLFontGL::sScaleX,
            (static_cast<F32>(b)+origin[1])*LLFontGL::sScaleY),color);
    };
    if (right<=left || top<=bottom) return;
    if (filled) draw(left,top,right,bottom);
    else
    {
        draw(left,top,right,top-1);draw(left,bottom+1,right,bottom);
        draw(left,top-1,left+1,bottom+1);draw(right-1,top-1,right,bottom+1);
    }
}


//
// Globals
//
const LLColor4 UI_VERTEX_COLOR(1.f, 1.f, 1.f, 1.f);

//
// Functions
//

bool ui_point_in_rect(S32 x, S32 y, S32 left, S32 top, S32 right, S32 bottom)
{
    if (x < left || right < x) return false;
    if (y < bottom || top < y) return false;
    return true;
}


// Puts GL into 2D drawing mode by turning off lighting, setting to an
// orthographic projection, etc.
void gl_state_for_2d(S32 width, S32 height)
{
    stop_glerror();
    F32 window_width = (F32) width;//gViewerWindow->getWindowWidth();
    F32 window_height = (F32) height;//gViewerWindow->getWindowHeight();

    gGL.matrixMode(LLRender::MM_PROJECTION);
    gGL.loadIdentity();
    gGL.ortho(0.0f, llmax(window_width, 1.f), 0.0f, llmax(window_height,1.f), -1.0f, 1.0f);
    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.loadIdentity();
    stop_glerror();
}


void gl_draw_x(const LLRect& rect, const LLColor4& color)
{
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    gGL.color4fv( color.mV );

    gGL.begin( LLRender::LINES );
        gGL.vertex2i( rect.mLeft,       rect.mTop );
        gGL.vertex2i( rect.mRight,  rect.mBottom );
        gGL.vertex2i( rect.mLeft,       rect.mBottom );
        gGL.vertex2i( rect.mRight,  rect.mTop );
    gGL.end();
}


void gl_rect_2d_offset_local( S32 left, S32 top, S32 right, S32 bottom, const LLColor4 &color, S32 pixel_offset, bool filled)
{
    gGL.color4fv(color.mV);
    gl_rect_2d_offset_local(left, top, right, bottom, pixel_offset, filled);
}

void gl_rect_2d_offset_local( S32 left, S32 top, S32 right, S32 bottom, S32 pixel_offset, bool filled)
{
    gGL.pushUIMatrix();
    left += LLFontGL::sCurOrigin.mX;
    right += LLFontGL::sCurOrigin.mX;
    bottom += LLFontGL::sCurOrigin.mY;
    top += LLFontGL::sCurOrigin.mY;

    gGL.loadUIIdentity();
    gl_rect_2d(llfloor((F32)left * LLRender::sUIGLScaleFactor.mV[VX]) - pixel_offset,
                llfloor((F32)top * LLRender::sUIGLScaleFactor.mV[VY]) + pixel_offset,
                llfloor((F32)right * LLRender::sUIGLScaleFactor.mV[VX]) + pixel_offset,
                llfloor((F32)bottom * LLRender::sUIGLScaleFactor.mV[VY]) - pixel_offset,
                filled);
    gGL.popUIMatrix();
}


void gl_rect_2d(S32 left, S32 top, S32 right, S32 bottom, bool filled )
{
    if (LLRender2D::isNativeUI())
        throw std::logic_error("Native UI rectangle requires explicit color");
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    // Counterclockwise quad will face the viewer
    if( filled )
    {
        gGL.begin( LLRender::TRIANGLES );
            gGL.vertex2i(left, top);
            gGL.vertex2i(left, bottom);
            gGL.vertex2i(right, bottom);

            gGL.vertex2i(left, top);
            gGL.vertex2i(right, bottom);
            gGL.vertex2i(right, top);
        gGL.end();
    }
    else
    {
        top--;
        right--;
        gGL.begin( LLRender::LINE_STRIP );
            gGL.vertex2i(left, top);
            gGL.vertex2i(left, bottom);
            gGL.vertex2i(right, bottom);
            gGL.vertex2i(right, top);
            gGL.vertex2i(left, top);
        gGL.end();
    }
}

void gl_rect_2d(S32 left, S32 top, S32 right, S32 bottom, const LLColor4 &color, bool filled )
{
    if (LLRender2D::isNativeUI()) { LLRender2D::nativeRectangle(left,top,right,bottom,color,filled);return; }
    gGL.color4fv( color.mV );
    gl_rect_2d( left, top, right, bottom, filled );
}


void gl_rect_2d( const LLRect& rect, const LLColor4& color, bool filled )
{
    if (LLRender2D::isNativeUI()) { LLRender2D::nativeRectangle(rect.mLeft,rect.mTop,rect.mRight,rect.mBottom,color,filled);return; }
    gGL.color4fv( color.mV );
    gl_rect_2d( rect.mLeft, rect.mTop, rect.mRight, rect.mBottom, filled );
}

// Given a rectangle on the screen, draws a drop shadow _outside_
// the right and bottom edges of it.  Along the right it has width "lines"
// and along the bottom it has height "lines".
void gl_drop_shadow(S32 left, S32 top, S32 right, S32 bottom, const LLColor4 &start_color, S32 lines)
{
    if (LLRender2D::isNativeUI())
    {
        --right;
        ++bottom;
        ++lines;
        LLColor4 end_color = start_color;
        end_color.mV[VALPHA] = 0.f;
        LLRender2D::nativeTriangle({{ { F32(right), F32(top - lines), start_color.mV[0], start_color.mV[1], start_color.mV[2], start_color.mV[3] }, { F32(right), F32(bottom), start_color.mV[0], start_color.mV[1], start_color.mV[2], start_color.mV[3] }, { F32(right + lines), F32(bottom), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] } }});
        LLRender2D::nativeTriangle({{ { F32(right), F32(top - lines), start_color.mV[0], start_color.mV[1], start_color.mV[2], start_color.mV[3] }, { F32(right + lines), F32(bottom), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] }, { F32(right + lines), F32(top - lines), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] } }});
        LLRender2D::nativeTriangle({{ { F32(right), F32(bottom), start_color.mV[0], start_color.mV[1], start_color.mV[2], start_color.mV[3] }, { F32(left + lines), F32(bottom), start_color.mV[0], start_color.mV[1], start_color.mV[2], start_color.mV[3] }, { F32(left + lines), F32(bottom - lines), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] } }});
        LLRender2D::nativeTriangle({{ { F32(right), F32(bottom), start_color.mV[0], start_color.mV[1], start_color.mV[2], start_color.mV[3] }, { F32(left + lines), F32(bottom - lines), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] }, { F32(right), F32(bottom - lines), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] } }});
        LLRender2D::nativeTriangle({{ { F32(left + lines), F32(bottom), start_color.mV[0], start_color.mV[1], start_color.mV[2], start_color.mV[3] }, { F32(left), F32(bottom), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] }, { F32(left + 1), F32(bottom - lines + 1), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] } }});
        LLRender2D::nativeTriangle({{ { F32(left + lines), F32(bottom), start_color.mV[0], start_color.mV[1], start_color.mV[2], start_color.mV[3] }, { F32(left + 1), F32(bottom - lines + 1), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] }, { F32(left + lines), F32(bottom - lines), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] } }});
        LLRender2D::nativeTriangle({{ { F32(right), F32(bottom), start_color.mV[0], start_color.mV[1], start_color.mV[2], start_color.mV[3] }, { F32(right), F32(bottom - lines), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] }, { F32(right + lines - 1), F32(bottom - lines + 1), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] } }});
        LLRender2D::nativeTriangle({{ { F32(right), F32(bottom), start_color.mV[0], start_color.mV[1], start_color.mV[2], start_color.mV[3] }, { F32(right + lines - 1), F32(bottom - lines + 1), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] }, { F32(right + lines), F32(bottom), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] } }});
        LLRender2D::nativeTriangle({{ { F32(right), F32(top - lines), start_color.mV[0], start_color.mV[1], start_color.mV[2], start_color.mV[3] }, { F32(right + lines), F32(top - lines), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] }, { F32(right + lines - 1), F32(top - 1), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] } }});
        LLRender2D::nativeTriangle({{ { F32(right), F32(top - lines), start_color.mV[0], start_color.mV[1], start_color.mV[2], start_color.mV[3] }, { F32(right + lines - 1), F32(top - 1), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] }, { F32(right), F32(top), end_color.mV[0], end_color.mV[1], end_color.mV[2], end_color.mV[3] } }});
        return;
    }
    stop_glerror();
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    // HACK: Overlap with the rectangle by a single pixel.
    right--;
    bottom++;
    lines++;

    LLColor4 end_color = start_color;
    end_color.mV[VALPHA] = 0.f;

    gGL.begin(LLRender::TRIANGLES);

    // Right edge, CCW faces screen
    gGL.color4fv(start_color.mV);
    gGL.vertex2i(right, top - lines);
    gGL.vertex2i(right, bottom);
    gGL.color4fv(end_color.mV);
    gGL.vertex2i(right + lines, bottom);
    gGL.color4fv(start_color.mV);
    gGL.vertex2i(right, top - lines);
    gGL.color4fv(end_color.mV);
    gGL.vertex2i(right + lines, bottom);
    gGL.vertex2i(right + lines, top - lines);

    // Bottom edge, CCW faces screen
    gGL.color4fv(start_color.mV);
    gGL.vertex2i(right, bottom);
    gGL.vertex2i(left + lines, bottom);
    gGL.color4fv(end_color.mV);
    gGL.vertex2i(left + lines, bottom - lines);
    gGL.color4fv(start_color.mV);
    gGL.vertex2i(right, bottom);
    gGL.color4fv(end_color.mV);
    gGL.vertex2i(left + lines, bottom - lines);
    gGL.vertex2i(right, bottom - lines);

    // bottom left Corner
    gGL.color4fv(start_color.mV);
    gGL.vertex2i(left + lines, bottom);
    gGL.color4fv(end_color.mV);
    gGL.vertex2i(left, bottom);
    // make the bottom left corner not sharp
    gGL.vertex2i(left + 1, bottom - lines + 1);
    gGL.color4fv(start_color.mV);
    gGL.vertex2i(left + lines, bottom);
    gGL.color4fv(end_color.mV);
    gGL.vertex2i(left + 1, bottom - lines + 1);
    gGL.vertex2i(left + lines, bottom - lines);

    // bottom right corner
    gGL.color4fv(start_color.mV);
    gGL.vertex2i(right, bottom);
    gGL.color4fv(end_color.mV);
    gGL.vertex2i(right, bottom - lines);
    // make the rightmost corner not sharp
    gGL.vertex2i(right + lines - 1, bottom - lines + 1);
    gGL.color4fv(start_color.mV);
    gGL.vertex2i(right, bottom);
    gGL.color4fv(end_color.mV);
    gGL.vertex2i(right + lines - 1, bottom - lines + 1);
    gGL.vertex2i(right + lines, bottom);

    // top right corner
    gGL.color4fv(start_color.mV);
    gGL.vertex2i(right, top - lines);
    gGL.color4fv(end_color.mV);
    gGL.vertex2i(right + lines, top - lines);
    // make the corner not sharp
    gGL.vertex2i(right + lines - 1, top - 1);
    gGL.color4fv(start_color.mV);
    gGL.vertex2i(right, top - lines);
    gGL.color4fv(end_color.mV);
    gGL.vertex2i(right + lines - 1, top - 1);
    gGL.vertex2i(right, top);

    gGL.end();
    stop_glerror();
}

void gl_line_2d(S32 x1, S32 y1, S32 x2, S32 y2 )
{
    if (LLRender2D::isNativeUI()) throw std::logic_error("Native UI line requires explicit color");
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    gGL.begin(LLRender::LINES);
        gGL.vertex2i(x1, y1);
        gGL.vertex2i(x2, y2);
    gGL.end();
}

void gl_line_2d(S32 x1, S32 y1, S32 x2, S32 y2, const LLColor4 &color )
{
    if (LLRender2D::isNativeUI())
    {
        // Rasterize widget strokes as triangles; no Vulkan wide-line feature.
        if (x1!=x2 && y1!=y2)
        {
            const float dx=float(x2)-x1, dy=float(y2)-y1;
            const float half=std::max(1.f,native_line_width)*.5f/std::hypot(dx,dy);
            const float nx=-dy*half, ny=dx*half;
            const auto vertex=[&](float x,float y) { return std::array<float,6>{x,y,color.mV[0],color.mV[1],color.mV[2],color.mV[3]}; };
            const auto a=vertex(x1+nx,y1+ny), b=vertex(x2+nx,y2+ny);
            const auto c=vertex(x1-nx,y1-ny), d=vertex(x2-nx,y2-ny);
            LLRender2D::nativeTriangle({a,b,c});
            LLRender2D::nativeTriangle({c,b,d});
            return;
        }
        const S32 width=llmax(1,static_cast<S32>(std::ceil(native_line_width)));
        if (x1==x2) LLRender2D::nativeRectangle(x1,llmax(y1,y2),x1+width,llmin(y1,y2),color,true);
        else LLRender2D::nativeRectangle(llmin(x1,x2),y1+width,llmax(x1,x2),y1,color,true);
        return;
    }
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    gGL.color4fv( color.mV );

    gGL.begin(LLRender::LINES);
        gGL.vertex2i(x1, y1);
        gGL.vertex2i(x2, y2);
    gGL.end();
}

void gl_triangle_2d(S32 x1, S32 y1, S32 x2, S32 y2, S32 x3, S32 y3, const LLColor4& color, bool filled)
{
    if (LLRender2D::isNativeUI())
    {
        if (filled)
        {
            const auto v=[&](float x,float y) { return std::array<float,6>{x,y,color.mV[0],color.mV[1],color.mV[2],color.mV[3]}; };
            LLRender2D::nativeTriangle({v(float(x1),float(y1)),v(float(x2),float(y2)),v(float(x3),float(y3))});
        }
        else
        {
            gl_line_2d(x1,y1,x2,y2,color);
            gl_line_2d(x2,y2,x3,y3,color);
            gl_line_2d(x3,y3,x1,y1,color);
        }
        return;
    }
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    gGL.color4fv(color.mV);

    if (filled)
    {
        gGL.begin(LLRender::TRIANGLES);
    }
    else
    {
        gGL.begin(LLRender::LINE_LOOP);
    }
    gGL.vertex2i(x1, y1);
    gGL.vertex2i(x2, y2);
    gGL.vertex2i(x3, y3);
    gGL.end();
}

void gl_corners_2d(S32 left, S32 top, S32 right, S32 bottom, S32 length, F32 max_frac)
{
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    length = llmin((S32)(max_frac*(right - left)), length);
    length = llmin((S32)(max_frac*(top - bottom)), length);
    gGL.begin(LLRender::LINES);
    gGL.vertex2i(left, top);
    gGL.vertex2i(left + length, top);

    gGL.vertex2i(left, top);
    gGL.vertex2i(left, top - length);

    gGL.vertex2i(left, bottom);
    gGL.vertex2i(left + length, bottom);

    gGL.vertex2i(left, bottom);
    gGL.vertex2i(left, bottom + length);

    gGL.vertex2i(right, top);
    gGL.vertex2i(right - length, top);

    gGL.vertex2i(right, top);
    gGL.vertex2i(right, top - length);

    gGL.vertex2i(right, bottom);
    gGL.vertex2i(right - length, bottom);

    gGL.vertex2i(right, bottom);
    gGL.vertex2i(right, bottom + length);
    gGL.end();
}


void gl_draw_image( S32 x, S32 y, LLTexture* image, const LLColor4& color, const LLRectf& uv_rect )
{
    if (NULL == image)
    {
        LL_WARNS() << "image == NULL; aborting function" << LL_ENDL;
        return;
    }
    gl_draw_scaled_rotated_image( x, y, image->getWidth(0), image->getHeight(0), 0.f, image, color, uv_rect );
}

void gl_draw_scaled_target(S32 x, S32 y, S32 width, S32 height, LLRenderTarget* target, const LLColor4& color, const LLRectf& uv_rect)
{
    gl_draw_scaled_rotated_image(x, y, width, height, 0.f, NULL, color, uv_rect, target);
}

void gl_draw_scaled_image(S32 x, S32 y, S32 width, S32 height, LLTexture* image, const LLColor4& color, const LLRectf& uv_rect)
{
    if (NULL == image)
    {
        LL_WARNS() << "image == NULL; aborting function" << LL_ENDL;
        return;
    }
    gl_draw_scaled_rotated_image( x, y, width, height, 0.f, image, color, uv_rect );
}

void gl_draw_scaled_image_with_border(S32 x, S32 y, S32 border_width, S32 border_height, S32 width, S32 height, LLTexture* image, const LLColor4& color, bool solid_color, const LLRectf& uv_rect, bool scale_inner)
{
    if (NULL == image)
    {
        LL_WARNS() << "image == NULL; aborting function" << LL_ENDL;
        return;
    }

    // scale screen size of borders down
    F32 border_width_fraction = (F32)border_width / (F32)image->getWidth(0);
    F32 border_height_fraction = (F32)border_height / (F32)image->getHeight(0);

    LLRectf scale_rect(border_width_fraction, 1.f - border_height_fraction, 1.f - border_width_fraction, border_height_fraction);
    gl_draw_scaled_image_with_border(x, y, width, height, image, color, solid_color, uv_rect, scale_rect, scale_inner);
}

void gl_draw_scaled_image_with_border(S32 x, S32 y, S32 width, S32 height, LLTexture* image, const LLColor4& color, bool solid_color, const LLRectf& uv_outer_rect, const LLRectf& center_rect, bool scale_inner)
{
    stop_glerror();

    if (NULL == image)
    {
        LL_WARNS() << "image == NULL; aborting function" << LL_ENDL;
        return;
    }

    if (solid_color)
    {
        gSolidColorProgram.bind();
    }

    if (center_rect.mLeft == 0.f
        && center_rect.mRight == 1.f
        && center_rect.mBottom == 0.f
        && center_rect.mTop == 1.f)
    {
        gl_draw_scaled_image(x, y, width, height, image, color, uv_outer_rect);
    }
    else
    {
        // add in offset of current image to current UI translation
        const LLVector3 ui_scale = gGL.getUIScale();
        const LLVector3 ui_translation = (gGL.getUITranslation() + LLVector3((F32)x, (F32)y, 0.f)).scaledVec(ui_scale);

        F32 uv_width = uv_outer_rect.getWidth();
        F32 uv_height = uv_outer_rect.getHeight();

        // shrink scaling region to be proportional to clipped image region
        LLRectf uv_center_rect( uv_outer_rect.mLeft + (center_rect.mLeft * uv_width),
                                uv_outer_rect.mBottom + (center_rect.mTop * uv_height),
                                uv_outer_rect.mLeft + (center_rect.mRight * uv_width),
                                uv_outer_rect.mBottom + (center_rect.mBottom * uv_height));

        F32 image_width = (F32)image->getWidth(0);
        F32 image_height = (F32)image->getHeight(0);

        S32 image_natural_width = ll_round(image_width * uv_width);
        S32 image_natural_height = ll_round(image_height * uv_height);

        LLRectf draw_center_rect(   uv_center_rect.mLeft * image_width,
                                    uv_center_rect.mTop * image_height,
                                    uv_center_rect.mRight * image_width,
                                    uv_center_rect.mBottom * image_height);

        if (scale_inner)
        {
            // scale center region of image to drawn region
            draw_center_rect.mRight += width - image_natural_width;
            draw_center_rect.mTop += height - image_natural_height;

            const F32 border_shrink_width = llmax(0.f, draw_center_rect.mLeft - draw_center_rect.mRight);
            const F32 border_shrink_height = llmax(0.f, draw_center_rect.mBottom - draw_center_rect.mTop);

            const F32 shrink_width_ratio = center_rect.getWidth() == 1.f ? 0.f : border_shrink_width / ((F32)image_natural_width * (1.f - center_rect.getWidth()));
            const F32 shrink_height_ratio = center_rect.getHeight() == 1.f ? 0.f : border_shrink_height / ((F32)image_natural_height * (1.f - center_rect.getHeight()));

            const F32 border_shrink_scale = 1.f - llmax(shrink_width_ratio, shrink_height_ratio);
            draw_center_rect.mLeft *= border_shrink_scale;
            draw_center_rect.mTop = lerp((F32)height, (F32)draw_center_rect.mTop, border_shrink_scale);
            draw_center_rect.mRight = lerp((F32)width, (F32)draw_center_rect.mRight, border_shrink_scale);
            draw_center_rect.mBottom *= border_shrink_scale;
        }
        else
        {
            // keep center region of image at fixed scale, but in same relative position
            F32 scale_factor = llmin((F32)width / draw_center_rect.getWidth(), (F32)height / draw_center_rect.getHeight(), 1.f);
            F32 scaled_width = draw_center_rect.getWidth() * scale_factor;
            F32 scaled_height = draw_center_rect.getHeight() * scale_factor;
            draw_center_rect.setCenterAndSize(uv_center_rect.getCenterX() * width, uv_center_rect.getCenterY() * height, scaled_width, scaled_height);
        }

        draw_center_rect.mLeft   = (F32)ll_round(ui_translation.mV[VX] + (F32)draw_center_rect.mLeft * ui_scale.mV[VX]);
        draw_center_rect.mTop    = (F32)ll_round(ui_translation.mV[VY] + (F32)draw_center_rect.mTop * ui_scale.mV[VY]);
        draw_center_rect.mRight  = (F32)ll_round(ui_translation.mV[VX] + (F32)draw_center_rect.mRight * ui_scale.mV[VX]);
        draw_center_rect.mBottom = (F32)ll_round(ui_translation.mV[VY] + (F32)draw_center_rect.mBottom * ui_scale.mV[VY]);

        LLRectf draw_outer_rect(ui_translation.mV[VX],
                                ui_translation.mV[VY] + height * ui_scale.mV[VY],
                                ui_translation.mV[VX] + width * ui_scale.mV[VX],
                                ui_translation.mV[VY]);

        gGL.getTexUnit(0)->bind(image, true);

        gGL.color4fv(color.mV);

        constexpr S32 NUM_VERTICES = 9 * 2 * 3; // 9 quads, 2 triangles per quad, 3 vertices per triangle
        static thread_local LLVector2 uv[NUM_VERTICES];
        static thread_local LLVector4a pos[NUM_VERTICES];

        S32 index = 0;

        gGL.begin(LLRender::TRIANGLES);
        {
            // draw bottom left triangles
            // 1
            uv[index].set(uv_outer_rect.mLeft, uv_outer_rect.mBottom);
            pos[index].set(draw_outer_rect.mLeft, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_outer_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            // 2
            uv[index].set(uv_outer_rect.mLeft, uv_outer_rect.mBottom);
            pos[index].set(draw_outer_rect.mLeft, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_outer_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            // draw bottom middle triangles
            uv[index].set(uv_center_rect.mLeft, uv_outer_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_outer_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            // 2
            uv[index].set(uv_center_rect.mLeft, uv_outer_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            // draw bottom right triangles
            uv[index].set(uv_center_rect.mRight, uv_outer_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_outer_rect.mBottom);
            pos[index].set(draw_outer_rect.mRight, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_outer_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            // 2
            uv[index].set(uv_center_rect.mRight, uv_outer_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_outer_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_outer_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            // draw left triangles
            uv[index].set(uv_outer_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_outer_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            // 2
            uv[index].set(uv_outer_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_outer_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_outer_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            // draw middle triangles
            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            // 2
            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            // draw right triangles
            uv[index].set(uv_center_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_outer_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_outer_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            // 2
            uv[index].set(uv_center_rect.mRight, uv_center_rect.mBottom);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mBottom, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_outer_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            // draw top left triangles
            uv[index].set(uv_outer_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_outer_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_outer_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_outer_rect.mTop, 0.f);
            index++;

            // 2
            uv[index].set(uv_outer_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_outer_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_outer_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_outer_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mLeft, uv_outer_rect.mTop);
            pos[index].set(draw_outer_rect.mLeft, draw_outer_rect.mTop, 0.f);
            index++;

            // draw top middle triangles
            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_outer_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_outer_rect.mTop, 0.f);
            index++;

            // 2
            uv[index].set(uv_center_rect.mLeft, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_outer_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_outer_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mLeft, uv_outer_rect.mTop);
            pos[index].set(draw_center_rect.mLeft, draw_outer_rect.mTop, 0.f);
            index++;

            // draw top right triangles
            uv[index].set(uv_center_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_outer_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_outer_rect.mTop);
            pos[index].set(draw_outer_rect.mRight, draw_outer_rect.mTop, 0.f);
            index++;

            // 2
            uv[index].set(uv_center_rect.mRight, uv_center_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_center_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_outer_rect.mRight, uv_outer_rect.mTop);
            pos[index].set(draw_outer_rect.mRight, draw_outer_rect.mTop, 0.f);
            index++;

            uv[index].set(uv_center_rect.mRight, uv_outer_rect.mTop);
            pos[index].set(draw_center_rect.mRight, draw_outer_rect.mTop, 0.f);
            index++;

            gGL.vertexBatchPreTransformed(pos, uv, NUM_VERTICES);
        }
        gGL.end();
    }

    if (solid_color)
    {
        gUIProgram.bind();
    }
}

void gl_draw_rotated_image(S32 x, S32 y, F32 degrees, LLTexture* image, const LLColor4& color, const LLRectf& uv_rect)
{
    gl_draw_scaled_rotated_image( x, y, image->getWidth(0), image->getHeight(0), degrees, image, color, uv_rect );
}

void gl_draw_scaled_rotated_image(S32 x, S32 y, S32 width, S32 height, F32 degrees, LLTexture* image, const LLColor4& color, const LLRectf& uv_rect, LLRenderTarget* target)
{
    if (!image && !target)
    {
        LL_WARNS() << "image == NULL; aborting function" << LL_ENDL;
        return;
    }

    if(image != NULL)
    {
        gGL.getTexUnit(0)->bind(image, true);
    }
    else
    {
        gGL.getTexUnit(0)->bind(target);
    }

    gGL.color4fv(color.mV);

    if (degrees == 0.f)
    {
        constexpr S32 NUM_VERTICES = 2 * 3;
        static thread_local LLVector2 uv[NUM_VERTICES +1];
        static thread_local LLVector4a pos[NUM_VERTICES +1];

        gGL.begin(LLRender::TRIANGLES);
        {
            LLVector3 ui_scale = gGL.getUIScale();
            LLVector3 ui_translation = gGL.getUITranslation();
            ui_translation.mV[VX] += x;
            ui_translation.mV[VY] += y;
            ui_translation.scaleVec(ui_scale);
            S32 index = 0;
            S32 scaled_width = ll_round(width * ui_scale.mV[VX]);
            S32 scaled_height = ll_round(height * ui_scale.mV[VY]);

            uv[index].set(uv_rect.mRight, uv_rect.mTop);
            pos[index].set(ui_translation.mV[VX] + scaled_width, ui_translation.mV[VY] + scaled_height, 0.f);
            index++;

            uv[index].set(uv_rect.mLeft, uv_rect.mTop);
            pos[index].set(ui_translation.mV[VX], ui_translation.mV[VY] + scaled_height, 0.f);
            index++;

            uv[index].set(uv_rect.mLeft, uv_rect.mBottom);
            pos[index].set(ui_translation.mV[VX], ui_translation.mV[VY], 0.f);
            index++;

            uv[index].set(uv_rect.mRight, uv_rect.mTop);
            pos[index].set(ui_translation.mV[VX] + scaled_width, ui_translation.mV[VY] + scaled_height, 0.f);
            index++;

            uv[index].set(uv_rect.mLeft, uv_rect.mBottom);
            pos[index].set(ui_translation.mV[VX], ui_translation.mV[VY], 0.f);
            index++;

            uv[index].set(uv_rect.mRight, uv_rect.mBottom);
            pos[index].set(ui_translation.mV[VX] + scaled_width, ui_translation.mV[VY], 0.f);
            index++;

            gGL.vertexBatchPreTransformed(pos, uv, NUM_VERTICES);
        }
        gGL.end();
    }
    else
    {
        gGL.pushUIMatrix();
        gGL.translateUI((F32)x, (F32)y, 0.f);

        F32 offset_x = F32(width/2);
        F32 offset_y = F32(height/2);

        gGL.translateUI(offset_x, offset_y, 0.f);

        LLMatrix3 quat(0.f, 0.f, degrees*DEG_TO_RAD);

        if(image != NULL)
        {
            gGL.getTexUnit(0)->bind(image, true);
        }
        else
        {
            gGL.getTexUnit(0)->bind(target);
        }

        gGL.color4fv(color.mV);

        gGL.begin(LLRender::TRIANGLES);
        {
            LLVector3 v;

            v = LLVector3(offset_x, offset_y, 0.f) * quat;
            gGL.texCoord2f(uv_rect.mRight, uv_rect.mTop);
            gGL.vertex2f(v.mV[0], v.mV[1] );

            v = LLVector3(-offset_x, offset_y, 0.f) * quat;
            gGL.texCoord2f(uv_rect.mLeft, uv_rect.mTop);
            gGL.vertex2f(v.mV[0], v.mV[1] );

            v = LLVector3(-offset_x, -offset_y, 0.f) * quat;
            gGL.texCoord2f(uv_rect.mLeft, uv_rect.mBottom);
            gGL.vertex2f(v.mV[0], v.mV[1] );

            v = LLVector3(offset_x, offset_y, 0.f) * quat;
            gGL.texCoord2f(uv_rect.mRight, uv_rect.mTop);
            gGL.vertex2f(v.mV[0], v.mV[1]);

            v = LLVector3(-offset_x, -offset_y, 0.f) * quat;
            gGL.texCoord2f(uv_rect.mLeft, uv_rect.mBottom);
            gGL.vertex2f(v.mV[0], v.mV[1]);

            v = LLVector3(offset_x, -offset_y, 0.f) * quat;
            gGL.texCoord2f(uv_rect.mRight, uv_rect.mBottom);
            gGL.vertex2f(v.mV[0], v.mV[1] );
        }
        gGL.end();
        gGL.popUIMatrix();
    }
}

void gl_line_3d( const LLVector3& start, const LLVector3& end, const LLColor4& color)
{
    gGL.color4f(color.mV[VRED], color.mV[VGREEN], color.mV[VBLUE], color.mV[VALPHA]);

    gGL.flush();
    gGL.setLineWidth(2.5f); // <FS> Line width OGL core profile fix by Rye Mutt

    gGL.begin(LLRender::LINES);
    {
        gGL.vertex3fv( start.mV );
        gGL.vertex3fv( end.mV );
    }
    gGL.end();

    LLRender2D::setLineWidth(1.f);
}

void gl_arc_2d(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled, F32 start_angle, F32 end_angle)
{
    if (end_angle < start_angle)
    {
        end_angle += F_TWO_PI;
    }

    gGL.pushUIMatrix();
    {
        gGL.translateUI(center_x, center_y, 0.f);

        // Inexact, but reasonably fast.
        F32 delta = (end_angle - start_angle) / steps;
        F32 sin_delta = sin( delta );
        F32 cos_delta = cos( delta );
        F32 x = cosf(start_angle) * radius;
        F32 y = sinf(start_angle) * radius;

        if (filled)
        {
            gGL.begin(LLRender::TRIANGLE_FAN);
            gGL.vertex2f(0.f, 0.f);
            // make sure circle is complete
            steps += 1;
        }
        else
        {
            gGL.begin(LLRender::LINE_STRIP);
        }

        while( steps-- )
        {
            // Successive rotations
            gGL.vertex2f( x, y );
            F32 x_new = x * cos_delta - y * sin_delta;
            y = x * sin_delta +  y * cos_delta;
            x = x_new;
        }
        gGL.end();
    }
    gGL.popUIMatrix();
}

void gl_circle_2d(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled)
{
    gGL.pushUIMatrix();
    {
        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.translateUI(center_x, center_y, 0.f);

        // Inexact, but reasonably fast.
        F32 delta = F_TWO_PI / steps;
        F32 sin_delta = sin( delta );
        F32 cos_delta = cos( delta );
        F32 x = radius;
        F32 y = 0.f;

        if (filled)
        {
            gGL.begin(LLRender::TRIANGLE_FAN);
            gGL.vertex2f(0.f, 0.f);
            // make sure circle is complete
            steps += 1;
        }
        else
        {
            gGL.begin(LLRender::LINE_LOOP);
        }

        while( steps-- )
        {
            // Successive rotations
            gGL.vertex2f( x, y );
            F32 x_new = x * cos_delta - y * sin_delta;
            y = x * sin_delta +  y * cos_delta;
            x = x_new;
        }
        gGL.end();
    }
    gGL.popUIMatrix();
}

// Renders a ring with sides (tube shape)
void gl_deep_circle( F32 radius, F32 depth, S32 steps )
{
    F32 x = radius;
    F32 y = 0.f;
    F32 angle_delta = F_TWO_PI / (F32)steps;
    gGL.begin( LLRender::TRIANGLE_STRIP  );
    {
        S32 step = steps + 1; // An extra step to close the circle.
        while( step-- )
        {
            gGL.vertex3f( x, y, depth );
            gGL.vertex3f( x, y, 0.f );

            F32 x_new = x * cosf(angle_delta) - y * sinf(angle_delta);
            y = x * sinf(angle_delta) +  y * cosf(angle_delta);
            x = x_new;
        }
    }
    gGL.end();
}

void gl_ring( F32 radius, F32 width, const LLColor4& center_color, const LLColor4& side_color, S32 steps, bool render_center )
{
    gGL.pushUIMatrix();
    {
        gGL.translateUI(0.f, 0.f, -width / 2);
        if( render_center )
        {
            gGL.color4fv(center_color.mV);
            gGL.diffuseColor4fv(center_color.mV);
            gl_deep_circle( radius, width, steps );
        }
        else
        {
            gGL.diffuseColor4fv(side_color.mV);
            gl_washer_2d(radius, radius - width, steps, side_color, side_color);
            gGL.translateUI(0.f, 0.f, width);
            gl_washer_2d(radius - width, radius, steps, side_color, side_color);
        }
    }
    gGL.popUIMatrix();
}

// Draw gray and white checkerboard with black border
void gl_rect_2d_checkerboard(const LLRect& rect, GLfloat alpha)
{
    //polygon stipple is deprecated, use "Checker" texture
    LLPointer<LLUIImage> img = LLRender2D::getInstance()->getUIImage("Checker");
    gGL.getTexUnit(0)->bind(img->getImage());
    gGL.getTexUnit(0)->setTextureAddressMode(LLTexUnit::TAM_WRAP);
    gGL.getTexUnit(0)->setTextureFilteringOption(LLTexUnit::TFO_POINT);

    LLColor4 color(1.f, 1.f, 1.f, alpha);
    LLRectf uv_rect(0, 0, rect.getWidth()/32.f, rect.getHeight()/32.f);

    gl_draw_scaled_image(rect.mLeft, rect.mBottom, rect.getWidth(), rect.getHeight(), img->getImage(), color, uv_rect);

    gGL.flush();
}


// Draws the area between two concentric circles, like
// a doughnut or washer.
void gl_washer_2d(F32 outer_radius, F32 inner_radius, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color)
{
    const F32 DELTA = F_TWO_PI / steps;
    const F32 SIN_DELTA = sin( DELTA );
    const F32 COS_DELTA = cos( DELTA );

    F32 x1 = outer_radius;
    F32 y1 = 0.f;
    F32 x2 = inner_radius;
    F32 y2 = 0.f;

    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    gGL.begin( LLRender::TRIANGLE_STRIP  );
    {
        steps += 1; // An extra step to close the circle.
        while( steps-- )
        {
            gGL.color4fv(outer_color.mV);
            gGL.vertex2f( x1, y1 );
            gGL.color4fv(inner_color.mV);
            gGL.vertex2f( x2, y2 );

            F32 x1_new = x1 * COS_DELTA - y1 * SIN_DELTA;
            y1 = x1 * SIN_DELTA +  y1 * COS_DELTA;
            x1 = x1_new;

            F32 x2_new = x2 * COS_DELTA - y2 * SIN_DELTA;
            y2 = x2 * SIN_DELTA +  y2 * COS_DELTA;
            x2 = x2_new;
        }
    }
    gGL.end();
}

// Draws the area between two concentric circles, like
// a doughnut or washer.
void gl_washer_segment_2d(F32 outer_radius, F32 inner_radius, F32 start_radians, F32 end_radians, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color)
{
    const F32 DELTA = (end_radians - start_radians) / steps;
    const F32 SIN_DELTA = sin( DELTA );
    const F32 COS_DELTA = cos( DELTA );

    F32 x1 = outer_radius * cos( start_radians );
    F32 y1 = outer_radius * sin( start_radians );
    F32 x2 = inner_radius * cos( start_radians );
    F32 y2 = inner_radius * sin( start_radians );

    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    gGL.begin( LLRender::TRIANGLE_STRIP  );
    {
        steps += 1; // An extra step to close the circle.
        while( steps-- )
        {
            gGL.color4fv(outer_color.mV);
            gGL.vertex2f( x1, y1 );
            gGL.color4fv(inner_color.mV);
            gGL.vertex2f( x2, y2 );

            F32 x1_new = x1 * COS_DELTA - y1 * SIN_DELTA;
            y1 = x1 * SIN_DELTA +  y1 * COS_DELTA;
            x1 = x1_new;

            F32 x2_new = x2 * COS_DELTA - y2 * SIN_DELTA;
            y2 = x2 * SIN_DELTA +  y2 * COS_DELTA;
            x2 = x2_new;
        }
    }
    gGL.end();
}

void gl_rect_2d_simple_tex( S32 width, S32 height )
{
    gGL.begin( LLRender::TRIANGLES );

        gGL.texCoord2f(1.f, 1.f);
        gGL.vertex2i(width, height);

        gGL.texCoord2f(0.f, 1.f);
        gGL.vertex2i(0, height);

        gGL.texCoord2f(0.f, 0.f);
        gGL.vertex2i(0, 0);

        gGL.texCoord2f(1.f, 1.f);
        gGL.vertex2i(width, height);

        gGL.texCoord2f(0.f, 0.f);
        gGL.vertex2i(0, 0);

        gGL.texCoord2f(1.f, 0.f);
        gGL.vertex2i(width, 0);

    gGL.end();
}

void gl_rect_2d_simple( S32 width, S32 height )
{
    gGL.begin( LLRender::TRIANGLES );
        gGL.vertex2i(width, height);
        gGL.vertex2i(0, height);
        gGL.vertex2i(0, 0);

        gGL.vertex2i(width, height);
        gGL.vertex2i(0, 0);
        gGL.vertex2i(width, 0);
    gGL.end();
}

void gl_segmented_rect_2d_tex(const S32 left,
                              const S32 top,
                              const S32 right,
                              const S32 bottom,
                              const S32 texture_width,
                              const S32 texture_height,
                              const S32 border_size,
                              const U32 edges)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    S32 width = llabs(right - left);
    S32 height = llabs(top - bottom);

    gGL.pushUIMatrix();

    gGL.translateUI((F32)left, (F32)bottom, 0.f);
    LLVector2 border_uv_scale((F32)border_size / (F32)texture_width, (F32)border_size / (F32)texture_height);

    if (border_uv_scale.mV[VX] > 0.5f)
    {
        border_uv_scale *= 0.5f / border_uv_scale.mV[VX];
    }
    if (border_uv_scale.mV[VY] > 0.5f)
    {
        border_uv_scale *= 0.5f / border_uv_scale.mV[VY];
    }

    F32 border_scale = llmin((F32)border_size, (F32)width * 0.5f, (F32)height * 0.5f);
    LLVector2 border_width_left = ((edges & (~(U32)ROUNDED_RECT_RIGHT)) != 0) ? LLVector2(border_scale, 0.f) : LLVector2::zero;
    LLVector2 border_width_right = ((edges & (~(U32)ROUNDED_RECT_LEFT)) != 0) ? LLVector2(border_scale, 0.f) : LLVector2::zero;
    LLVector2 border_height_bottom = ((edges & (~(U32)ROUNDED_RECT_TOP)) != 0) ? LLVector2(0.f, border_scale) : LLVector2::zero;
    LLVector2 border_height_top = ((edges & (~(U32)ROUNDED_RECT_BOTTOM)) != 0) ? LLVector2(0.f, border_scale) : LLVector2::zero;
    LLVector2 width_vec((F32)width, 0.f);
    LLVector2 height_vec(0.f, (F32)height);

    gGL.begin(LLRender::TRIANGLES);
    {
        // draw bottom left
        gGL.texCoord2f(0.f, 0.f);
        gGL.vertex2f(0.f, 0.f);

        gGL.texCoord2f(border_uv_scale.mV[VX], 0.f);
        gGL.vertex2fv(border_width_left.mV);

        gGL.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
        gGL.vertex2fv((border_width_left + border_height_bottom).mV);

        gGL.texCoord2f(0.f, 0.f);
        gGL.vertex2f(0.f, 0.f);

        gGL.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
        gGL.vertex2fv((border_width_left + border_height_bottom).mV);

        gGL.texCoord2f(0.f, border_uv_scale.mV[VY]);
        gGL.vertex2fv(border_height_bottom.mV);

        // draw bottom middle
        gGL.texCoord2f(border_uv_scale.mV[VX], 0.f);
        gGL.vertex2fv(border_width_left.mV);

        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 0.f);
        gGL.vertex2fv((width_vec - border_width_right).mV);

        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec - border_width_right + border_height_bottom).mV);

        gGL.texCoord2f(border_uv_scale.mV[VX], 0.f);
        gGL.vertex2fv(border_width_left.mV);

        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec - border_width_right + border_height_bottom).mV);

        gGL.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
        gGL.vertex2fv((border_width_left + border_height_bottom).mV);

        // draw bottom right
        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 0.f);
        gGL.vertex2fv((width_vec - border_width_right).mV);

        gGL.texCoord2f(1.f, 0.f);
        gGL.vertex2fv(width_vec.mV);

        gGL.texCoord2f(1.f, border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec + border_height_bottom).mV);

        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 0.f);
        gGL.vertex2fv((width_vec - border_width_right).mV);

        gGL.texCoord2f(1.f, border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec + border_height_bottom).mV);

        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec - border_width_right + border_height_bottom).mV);

        // draw left
        gGL.texCoord2f(0.f, border_uv_scale.mV[VY]);
        gGL.vertex2fv(border_height_bottom.mV);

        gGL.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
        gGL.vertex2fv((border_width_left + border_height_bottom).mV);

        gGL.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((border_width_left + height_vec - border_height_top).mV);

        gGL.texCoord2f(0.f, border_uv_scale.mV[VY]);
        gGL.vertex2fv(border_height_bottom.mV);

        gGL.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((border_width_left + height_vec - border_height_top).mV);

        gGL.texCoord2f(0.f, 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((height_vec - border_height_top).mV);

        // draw middle
        gGL.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
        gGL.vertex2fv((border_width_left + border_height_bottom).mV);

        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec - border_width_right + border_height_bottom).mV);

        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec - border_width_right + height_vec - border_height_top).mV);

        gGL.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
        gGL.vertex2fv((border_width_left + border_height_bottom).mV);

        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec - border_width_right + height_vec - border_height_top).mV);

        gGL.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((border_width_left + height_vec - border_height_top).mV);

        // draw right
        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec - border_width_right + border_height_bottom).mV);

        gGL.texCoord2f(1.f, border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec + border_height_bottom).mV);

        gGL.texCoord2f(1.f, 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec + height_vec - border_height_top).mV);

        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec - border_width_right + border_height_bottom).mV);

        gGL.texCoord2f(1.f, 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec + height_vec - border_height_top).mV);

        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec - border_width_right + height_vec - border_height_top).mV);

        // draw top left
        gGL.texCoord2f(0.f, 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((height_vec - border_height_top).mV);

        gGL.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((border_width_left + height_vec - border_height_top).mV);

        gGL.texCoord2f(border_uv_scale.mV[VX], 1.f);
        gGL.vertex2fv((border_width_left + height_vec).mV);

        gGL.texCoord2f(0.f, 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((height_vec - border_height_top).mV);

        gGL.texCoord2f(border_uv_scale.mV[VX], 1.f);
        gGL.vertex2fv((border_width_left + height_vec).mV);

        gGL.texCoord2f(0.f, 1.f);
        gGL.vertex2fv((height_vec).mV);

        // draw top middle
        gGL.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((border_width_left + height_vec - border_height_top).mV);

        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec - border_width_right + height_vec - border_height_top).mV);

        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f);
        gGL.vertex2fv((width_vec - border_width_right + height_vec).mV);

        gGL.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((border_width_left + height_vec - border_height_top).mV);

        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f);
        gGL.vertex2fv((width_vec - border_width_right + height_vec).mV);

        gGL.texCoord2f(border_uv_scale.mV[VX], 1.f);
        gGL.vertex2fv((border_width_left + height_vec).mV);

        // draw top right
        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec - border_width_right + height_vec - border_height_top).mV);

        gGL.texCoord2f(1.f, 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec + height_vec - border_height_top).mV);

        gGL.texCoord2f(1.f, 1.f);
        gGL.vertex2fv((width_vec + height_vec).mV);

        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
        gGL.vertex2fv((width_vec - border_width_right + height_vec - border_height_top).mV);

        gGL.texCoord2f(1.f, 1.f);
        gGL.vertex2fv((width_vec + height_vec).mV);

        gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f);
        gGL.vertex2fv((width_vec - border_width_right + height_vec).mV);
    }
    gGL.end();

    gGL.popUIMatrix();
}

void gl_segmented_rect_2d_fragment_tex(const LLRect& rect,
    const S32 texture_width,
    const S32 texture_height,
    const S32 border_size,
    const F32 start_fragment,
    const F32 end_fragment,
    const U32 edges)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    const S32 left = rect.mLeft;
    const S32 right = rect.mRight;
    const S32 top = rect.mTop;
    const S32 bottom = rect.mBottom;
    S32 width = llabs(right - left);
    S32 height = llabs(top - bottom);

    gGL.pushUIMatrix();

    gGL.translateUI((F32)left, (F32)bottom, 0.f);
    LLVector2 border_uv_scale((F32)border_size / (F32)texture_width, (F32)border_size / (F32)texture_height);

    if (border_uv_scale.mV[VX] > 0.5f)
    {
        border_uv_scale *= 0.5f / border_uv_scale.mV[VX];
    }
    if (border_uv_scale.mV[VY] > 0.5f)
    {
        border_uv_scale *= 0.5f / border_uv_scale.mV[VY];
    }

    F32 border_scale = llmin((F32)border_size, (F32)width * 0.5f, (F32)height * 0.5f);
    LLVector2 border_width_left = ((edges & (~(U32)ROUNDED_RECT_RIGHT)) != 0) ? LLVector2(border_scale, 0.f) : LLVector2::zero;
    LLVector2 border_width_right = ((edges & (~(U32)ROUNDED_RECT_LEFT)) != 0) ? LLVector2(border_scale, 0.f) : LLVector2::zero;
    LLVector2 border_height_bottom = ((edges & (~(U32)ROUNDED_RECT_TOP)) != 0) ? LLVector2(0.f, border_scale) : LLVector2::zero;
    LLVector2 border_height_top = ((edges & (~(U32)ROUNDED_RECT_BOTTOM)) != 0) ? LLVector2(0.f, border_scale) : LLVector2::zero;
    LLVector2 width_vec((F32)width, 0.f);
    LLVector2 height_vec(0.f, (F32)height);

    F32 middle_start = border_scale / (F32)width;
    F32 middle_end = 1.f - middle_start;

    F32 u_min;
    F32 u_max;
    LLVector2 x_min;
    LLVector2 x_max;

    gGL.begin(LLRender::TRIANGLES);
    {
        if (start_fragment < middle_start)
        {
            u_min = (start_fragment / middle_start)         * border_uv_scale.mV[VX];
            u_max = llmin(end_fragment / middle_start, 1.f) * border_uv_scale.mV[VX];
            x_min = (start_fragment / middle_start)         * border_width_left;
            x_max = llmin(end_fragment / middle_start, 1.f) * border_width_left;

            // draw bottom left
            gGL.texCoord2f(u_min, 0.f);
            gGL.vertex2fv(x_min.mV);

            gGL.texCoord2f(border_uv_scale.mV[VX], 0.f);
            gGL.vertex2fv(x_max.mV);

            gGL.texCoord2f(u_max, border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + border_height_bottom).mV);

            gGL.texCoord2f(u_min, 0.f);
            gGL.vertex2fv(x_min.mV);

            gGL.texCoord2f(u_max, border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + border_height_bottom).mV);

            gGL.texCoord2f(u_min, border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + border_height_bottom).mV);

            // draw left
            gGL.texCoord2f(u_min, border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + border_height_bottom).mV);

            gGL.texCoord2f(u_max, border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + border_height_bottom).mV);

            gGL.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + height_vec - border_height_top).mV);

            gGL.texCoord2f(u_min, border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + border_height_bottom).mV);

            gGL.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + height_vec - border_height_top).mV);

            gGL.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + height_vec - border_height_top).mV);

            // draw top left
            gGL.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + height_vec - border_height_top).mV);

            gGL.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + height_vec - border_height_top).mV);

            gGL.texCoord2f(u_max, 1.f);
            gGL.vertex2fv((x_max + height_vec).mV);

            gGL.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + height_vec - border_height_top).mV);

            gGL.texCoord2f(u_max, 1.f);
            gGL.vertex2fv((x_max + height_vec).mV);

            gGL.texCoord2f(u_min, 1.f);
            gGL.vertex2fv((x_min + height_vec).mV);
        }

        if (end_fragment > middle_start || start_fragment < middle_end)
        {
            x_min = border_width_left + ((llclamp(start_fragment, middle_start, middle_end) - middle_start)) * width_vec;
            x_max = border_width_left + ((llclamp(end_fragment, middle_start, middle_end) - middle_start)) * width_vec;

            // draw bottom middle
            gGL.texCoord2f(border_uv_scale.mV[VX], 0.f);
            gGL.vertex2fv(x_min.mV);

            gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 0.f);
            gGL.vertex2fv((x_max).mV);

            gGL.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + border_height_bottom).mV);

            gGL.texCoord2f(border_uv_scale.mV[VX], 0.f);
            gGL.vertex2fv(x_min.mV);

            gGL.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + border_height_bottom).mV);

            gGL.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + border_height_bottom).mV);

            // draw middle
            gGL.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + border_height_bottom).mV);

            gGL.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + border_height_bottom).mV);

            gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + height_vec - border_height_top).mV);

            gGL.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + border_height_bottom).mV);

            gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + height_vec - border_height_top).mV);

            gGL.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + height_vec - border_height_top).mV);

            // draw top middle
            gGL.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + height_vec - border_height_top).mV);

            gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + height_vec - border_height_top).mV);

            gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f);
            gGL.vertex2fv((x_max + height_vec).mV);

            gGL.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + height_vec - border_height_top).mV);

            gGL.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f);
            gGL.vertex2fv((x_max + height_vec).mV);

            gGL.texCoord2f(border_uv_scale.mV[VX], 1.f);
            gGL.vertex2fv((x_min + height_vec).mV);
        }

        if (end_fragment > middle_end)
        {
            u_min = 1.f         - ((1.f - llmax(0.f, (start_fragment - middle_end) / middle_start)) * border_uv_scale.mV[VX]);
            u_max = 1.f         - ((1.f - ((end_fragment - middle_end) / middle_start)) * border_uv_scale.mV[VX]);
            x_min = width_vec   - ((1.f - llmax(0.f, (start_fragment - middle_end) / middle_start)) * border_width_right);
            x_max = width_vec   - ((1.f - ((end_fragment - middle_end) / middle_start)) * border_width_right);

            // draw bottom right
            gGL.texCoord2f(u_min, 0.f);
            gGL.vertex2fv((x_min).mV);

            gGL.texCoord2f(u_max, 0.f);
            gGL.vertex2fv(x_max.mV);

            gGL.texCoord2f(u_max, border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + border_height_bottom).mV);

            gGL.texCoord2f(u_min, 0.f);
            gGL.vertex2fv((x_min).mV);

            gGL.texCoord2f(u_max, border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + border_height_bottom).mV);

            gGL.texCoord2f(u_min, border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + border_height_bottom).mV);

            // draw right
            gGL.texCoord2f(u_min, border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + border_height_bottom).mV);

            gGL.texCoord2f(u_max, border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + border_height_bottom).mV);

            gGL.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + height_vec - border_height_top).mV);

            gGL.texCoord2f(u_min, border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + border_height_bottom).mV);

            gGL.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + height_vec - border_height_top).mV);

            gGL.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + height_vec - border_height_top).mV);

            // draw top right
            gGL.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + height_vec - border_height_top).mV);

            gGL.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_max + height_vec - border_height_top).mV);

            gGL.texCoord2f(u_max, 1.f);
            gGL.vertex2fv((x_max + height_vec).mV);

            gGL.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
            gGL.vertex2fv((x_min + height_vec - border_height_top).mV);

            gGL.texCoord2f(u_max, 1.f);
            gGL.vertex2fv((x_max + height_vec).mV);

            gGL.texCoord2f(u_min, 1.f);
            gGL.vertex2fv((x_min + height_vec).mV);
        }
    }
    gGL.end();

    gGL.popUIMatrix();
}

void gl_segmented_rect_3d_tex(const LLRectf& clip_rect, const LLRectf& center_uv_rect, const LLRectf& center_draw_rect,
                             const LLVector3& width_vec, const LLVector3& height_vec)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    gGL.begin(LLRender::TRIANGLES);
    {
        // draw bottom left
        gGL.texCoord2f(clip_rect.mLeft, clip_rect.mBottom);
        gGL.vertex3f(0.f, 0.f, 0.f);

        gGL.texCoord2f(center_uv_rect.mLeft, clip_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(clip_rect.mLeft, clip_rect.mBottom);
        gGL.vertex3f(0.f, 0.f, 0.f);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(clip_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mBottom * height_vec).mV);

        // draw bottom middle
        gGL.texCoord2f(center_uv_rect.mLeft, clip_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, clip_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, clip_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        // draw bottom right
        gGL.texCoord2f(center_uv_rect.mRight, clip_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, clip_rect.mBottom);
        gGL.vertex3fv(width_vec.mV);

        gGL.texCoord2f(clip_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, clip_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

        // draw left
        gGL.texCoord2f(clip_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(clip_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(clip_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mTop * height_vec).mV);

        // draw middle
        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        // draw right
        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight* width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

        // draw top left
        gGL.texCoord2f(clip_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, clip_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + height_vec).mV);

        gGL.texCoord2f(clip_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, clip_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + height_vec).mV);

        gGL.texCoord2f(clip_rect.mLeft, clip_rect.mTop);
        gGL.vertex3fv((height_vec).mV);

        // draw top middle
        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, clip_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, clip_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight* width_vec + height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, clip_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + height_vec).mV);

        // draw top right
        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, clip_rect.mTop);
        gGL.vertex3fv((width_vec + height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight* width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, clip_rect.mTop);
        gGL.vertex3fv((width_vec + height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, clip_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + height_vec).mV);
    }
    gGL.end();

}

LLRender2D::LLRender2D(LLImageProviderInterface* image_provider)
{
    mImageProvider = image_provider;
    if(mImageProvider)
    {
        mImageProvider->addOnRemovalCallback(resetProvider);
    }
}

LLRender2D::~LLRender2D()
{
    if(mImageProvider)
    {
        mImageProvider->cleanUp();
        mImageProvider->deleteOnRemovalCallback(resetProvider);
    }
}

// static
void LLRender2D::translate(F32 x, F32 y, F32 z)
{
    if (isNativeUI() && z!=0.f) throw std::logic_error("World depth is not admitted in native UI");
    if (!isNativeUI()) gGL.translateUI(x,y,z);
    else { native_origins.back()[0]+=x;native_origins.back()[1]+=y; }
    LLFontGL::sCurOrigin.mX += (S32) x;
    LLFontGL::sCurOrigin.mY += (S32) y;
    LLFontGL::sCurDepth += z;
}

// static
void LLRender2D::pushMatrix()
{
    if (!isNativeUI()) gGL.pushUIMatrix();
    else native_origins.push_back(native_origins.back());
    LLFontGL::sOriginStack.push_back(std::make_pair(LLFontGL::sCurOrigin, LLFontGL::sCurDepth));
}

// static
void LLRender2D::popMatrix()
{
    if (isNativeUI() && LLFontGL::sOriginStack.empty()) throw std::logic_error("Unbalanced native UI matrix stack");
    if (!isNativeUI()) gGL.popUIMatrix();
    else { if (native_origins.size()<=1) throw std::logic_error("Unbalanced native primitive matrix stack");native_origins.pop_back(); }
    LLFontGL::sCurOrigin = LLFontGL::sOriginStack.back().first;
    LLFontGL::sCurDepth = LLFontGL::sOriginStack.back().second;
    LLFontGL::sOriginStack.pop_back();
}

// static
void LLRender2D::loadIdentity()
{
    if (!isNativeUI()) gGL.loadUIIdentity();
    else native_origins.back()={0,0};
    LLFontGL::sCurOrigin.mX = 0;
    LLFontGL::sCurOrigin.mY = 0;
    LLFontGL::sCurDepth = 0.f;
}

// static
void LLRender2D::setLineWidth(F32 width)
{
    if (isNativeUI())
    {
        if (!std::isfinite(width) || width<=0 || width>16) throw std::logic_error("Invalid native UI line width");
        native_line_width=width;return;
    }
    // <FS> Line width OGL core profile fix by Rye Mutt
    //gGL.flush();
    //// If outside the allowed range, glLineWidth fails with "invalid value".
    //// On Darwin, the range is [1, 1].
    //static GLfloat range[2]{0.0};
    //if (range[1] == 0)
    //{
    //    glGetFloatv(GL_SMOOTH_LINE_WIDTH_RANGE, range);
    //}
    //width *= lerp(LLRender::sUIGLScaleFactor.mV[VX], LLRender::sUIGLScaleFactor.mV[VY], 0.5f);
    //glLineWidth(llclamp(width, range[0], range[1]));
    gGL.setLineWidth(width * lerp(LLRender::sUIGLScaleFactor.mV[VX], LLRender::sUIGLScaleFactor.mV[VY], 0.5f));
}

LLPointer<LLUIImage> LLRender2D::getUIImageByID(const LLUUID& image_id, S32 priority)
{
    if (mImageProvider)
    {
        return mImageProvider->getUIImageByID(image_id, priority);
    }
    else
    {
        return NULL;
    }
}

LLPointer<LLUIImage> LLRender2D::getUIImage(const std::string& name, S32 priority)
{
    if (!name.empty() && mImageProvider)
        return mImageProvider->getUIImage(name, priority);
    else
        return NULL;
}

// static
void LLRender2D::resetProvider()
{
    if (LLRender2D::instanceExists())
    {
        LLRender2D::getInstance()->mImageProvider = NULL;
    }
}

// class LLImageProviderInterface

LLImageProviderInterface::~LLImageProviderInterface()
{
    for (callback_list_t::iterator iter = mCallbackList.begin(); iter != mCallbackList.end();)
    {
        callback_list_t::iterator curiter = iter++;
        (*curiter)();
    }
}

void LLImageProviderInterface::addOnRemovalCallback(callback_t func)
{
    if (!func)
    {
        return;
    }
    mCallbackList.push_back(func);
}

void LLImageProviderInterface::deleteOnRemovalCallback(callback_t func)
{
    callback_list_t::iterator iter = std::find(mCallbackList.begin(), mCallbackList.end(), func);
    if (iter != mCallbackList.end())
    {
        mCallbackList.erase(iter);
    }
}

