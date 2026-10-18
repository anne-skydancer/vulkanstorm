// Real native world-owner snapshot for the shared reference focus kernel. LGPL-2.1.
#pragma once
#include "llbbox.h"
#include "m4math.h"
#include "v3math.h"
#include <functional>
struct VSCameraFocusObject
{
    LLMatrix4 renderMatrix;
    LLQuaternion renderRotation;
    LLVector3 renderPosition, rawScale, characterAgentPosition;
    LLBBox officialBounds;
    bool isAvatar=false, isRoot=false, isAnimated=false, hasControlAvatar=false;
    std::function<bool()> valid;
    bool current()const{return valid && valid();}
    LLMatrix4 matrix()const{return renderMatrix;}
    LLQuaternion rotation()const{return renderRotation;}
    LLVector3 position()const{return renderPosition;}
    bool avatar()const{return isAvatar;}
    bool root()const{return isRoot;}
    bool animated()const{return isAnimated;}
    bool controlAvatar()const{return hasControlAvatar;}
    LLVector3 characterPosition()const{return characterAgentPosition;}
    LLVector3 scale()const{return rawScale;}
    LLBBox bounds()const{return officialBounds;}
};
