/**
 * @file lljoystickbutton.h
 * @brief LLJoystick class definition
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

#ifndef LL_LLJOYSTICKBUTTON_H
#define LL_LLJOYSTICKBUTTON_H

#include "llbutton.h"
#include "llcoord.h"
#include "llviewertexture.h"
#include "llquaternion.h"

typedef enum e_joystick_quadrant
{
    JQ_ORIGIN,
    JQ_UP,
    JQ_DOWN,
    JQ_LEFT,
    JQ_RIGHT
} EJoystickQuadrant;

struct QuadrantNames : public LLInitParam::TypeValuesHelper<EJoystickQuadrant, QuadrantNames>
{
    static void declareValues();
};

class LLJoystick
:   public LLButton
{
public:
    struct Params
    :   public LLInitParam::Block<Params, LLButton::Params>
    {
        Optional<EJoystickQuadrant, QuadrantNames> quadrant;

        Params()
        :   quadrant("quadrant", JQ_ORIGIN)
        {
            changeDefault(label, "");
        }
    };
    LLJoystick(const Params&);

    virtual bool    handleMouseDown(S32 x, S32 y, MASK mask);
    virtual bool    handleMouseUp(S32 x, S32 y, MASK mask);
    virtual bool    handleHover(S32 x, S32 y, MASK mask);

    virtual void    onMouseUp() {}
    virtual void    onHeldDown() = 0;
    F32             getElapsedHeldDownTime();

    static void     onBtnHeldDown(void *userdata);      // called by llbutton callback handler
    void            setInitialQuadrant(EJoystickQuadrant initial) { mInitialQuadrant = initial; };

    /**
     * Checks if click location is inside joystick circle.
     *
     * Image containing circle is square and this square has adherent points with joystick
     * circle. Make sure to change method according to shape other than square.
     */
    bool    pointInCircle(S32 x, S32 y) const;
    // <FS:Beq> FIRE-30414 Camera control arrows not clickable
    // bool pointInCenterDot(S32 x, S32 y, S32 radius) const;
    bool    pointInCenterDot(S32 x, S32 y) const;
    // </FS:Beq>
    static std::string nameFromQuadrant(const EJoystickQuadrant quadrant);
    static EJoystickQuadrant quadrantFromName(const std::string& name);
    static EJoystickQuadrant selectQuadrant(LLXMLNodePtr node);


protected:
    virtual void    updateSlop();                   // recompute slop margins

protected:
    EJoystickQuadrant   mInitialQuadrant;           // mousedown = click in this quadrant
    LLCoordGL           mInitialOffset;             // pretend mouse started here
    LLCoordGL           mLastMouse;                 // where was mouse on last hover event
    LLCoordGL           mFirstMouse;                // when mouse clicked, where was it
    S32                 mVertSlopNear;              // where the slop regions end
    S32                 mVertSlopFar;               // where the slop regions end
    S32                 mHorizSlopNear;             // where the slop regions end
    S32                 mHorizSlopFar;              // where the slop regions end
    bool                mHeldDown;
    LLFrameTimer        mHeldDownTimer;
};


// Turn agent left and right, move forward and back
class LLJoystickAgentTurn
:   public LLJoystick
{
public:
    struct Params : public LLJoystick::Params {};
    LLJoystickAgentTurn(const Params& p) : LLJoystick(p) {}
    virtual void    onHeldDown();
};


// Slide left and right, move forward and back
class LLJoystickAgentSlide
:   public LLJoystick
{
public:
    struct Params : public LLJoystick::Params {};
    LLJoystickAgentSlide(const Params& p) : LLJoystick(p) {}

    virtual void    onHeldDown();
    virtual void    onMouseUp();
};


// Rotate camera around the focus point
class LLJoystickCameraRotate
:   public LLJoystick
{
public:
    struct Params
    :   public LLInitParam::Block<Params, LLJoystick::Params>
    {
        Params()
        {
            changeDefault(held_down_delay.seconds, 0.0);
        }
    };

    LLJoystickCameraRotate(const LLJoystickCameraRotate::Params&);

    // <VulkanStorm> GL-free description of what draw() paints: the unselected
    // base image filling the local rect, then either the center image or the
    // selected image rotated per active quadrant (top=0, right=1, bottom=2,
    // left=3 quarter-turns CCW, matching drawRotatedImage()).
    struct VkDrawState
    {
        std::string base_image;     // image_unselected name (never null in draw())
        std::string selected_image; // image_selected name, quadrant overlays
        std::string center_image;   // mCenterImageName
        bool in_center = false;
        bool in_top = false;
        bool in_right = false;
        bool in_bottom = false;
        bool in_left = false;
        LLRect rect;                // screen space; images fill/anchor to this
        LLColor4 color;             // UI_VERTEX_COLOR modulated by alpha
    };
    VkDrawState getVkDrawState(F32 alpha) const;
    // </VulkanStorm>

    virtual void    setToggleState( bool left, bool top, bool right, bool bottom );

    virtual bool    handleMouseDown(S32 x, S32 y, MASK mask);
    virtual bool    handleMouseUp(S32 x, S32 y, MASK mask);
    virtual bool    handleHover(S32 x, S32 y, MASK mask);
    virtual void    onHeldDown();
    virtual void    resetJoystickCamera();
    virtual void    draw();

protected:
    F32             getOrbitRate();
    virtual void    updateSlop();
    void            drawRotatedImage( LLPointer<LLUIImage> image, S32 rotations );

protected:
    bool            mInLeft;
    bool            mInTop;
    bool            mInRight;
    bool            mInBottom;
    bool            mInCenter;

    std::string     mCenterImageName;
};


// Track the camera focus point forward/backward and side to side
class LLJoystickCameraTrack
:   public LLJoystickCameraRotate
{
public:
    struct Params
    :   public LLInitParam::Block<Params, LLJoystickCameraRotate::Params>
    {
        Params();
    };

    LLJoystickCameraTrack(const LLJoystickCameraTrack::Params&);
    virtual void    onHeldDown();
    virtual void    resetJoystickCamera();
};

//
class LLJoystickQuaternion :
    public LLJoystick
{
public:
    struct Params :
        public LLInitParam::Block<Params, LLJoystick::Params>
    {
        Params();
    };

    LLJoystickQuaternion(const LLJoystickQuaternion::Params &);

    // <VulkanStorm> GL-free description of what draw() paints: the unselected
    // base image at native size, the selected image rotated per active
    // quadrant (top=0, right=1, bottom=2, left=3 quarter-turns CCW, matching
    // drawRotatedImage()), then the rotation indicator circle.
    struct VkDrawState
    {
        std::string base_image;         // image_unselected name
        std::string selected_image;     // image_selected name, quadrant overlays
        bool in_top = false;
        bool in_right = false;
        bool in_bottom = false;
        bool in_left = false;
        S32 base_width = 0;             // native size (drawn at 0,0); 0 if unknown
        S32 base_height = 0;
        LLRect rect;                    // screen space
        LLColor4 color;                 // UI_VERTEX_COLOR modulated by alpha
        LLVector3 draw_point;           // indicator position, LOCAL coords
        F32 circle_x = 0.f;             // indicator position, screen space
        F32 circle_y = 0.f;
        F32 circle_radius = 4.f;        // gl_circle_2d(draw_point, 4, 8, filled)
        S32 circle_segments = 8;
        bool circle_filled = false;     // draw_point.mV[mZAxisIndex] >= 0
    };
    VkDrawState getVkDrawState(F32 alpha) const;
    // </VulkanStorm>

    virtual void    setToggleState(bool left, bool top, bool right, bool bottom);

    virtual bool    handleMouseDown(S32 x, S32 y, MASK mask);
    virtual bool    handleMouseUp(S32 x, S32 y, MASK mask);
    virtual void    onHeldDown();
    virtual void    draw();

    void            setRotation(const LLQuaternion &value);
    LLQuaternion    getRotation() const;

protected:
    F32             getOrbitRate();
    virtual void    updateSlop();
    void            drawRotatedImage(LLPointer<LLUIImage> image, S32 rotations);

    bool            mInLeft;
    bool            mInTop;
    bool            mInRight;
    bool            mInBottom;

    S32             mXAxisIndex;
    S32             mYAxisIndex;
    S32             mZAxisIndex;

    LLVector3       mVectorZero;
    LLQuaternion    mRotation;
    LLVector3       mUpDnAxis;
    LLVector3       mLfRtAxis;
};

#endif  // LL_LLJOYSTICKBUTTON_H
