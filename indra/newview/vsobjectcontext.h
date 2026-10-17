// Real scene-object values for shared controllers, without GL object stand-ins.
// LGPL-2.1, like the viewer.
#pragma once
#include "llhost.h"
#include "lluuid.h"
#include "v3dmath.h"
#include <functional>
#include <optional>
class LLViewerObject;

struct VSObjectContext
{
    LLUUID id,rootEditID,rootID;
    LLHost host;
    U32 localID=0,flags=0;
    U8 pcode=0,material=0,clickAction=0;
    LLVector3d position;
    bool attachment=false,hud=false,youOwner=false;
    std::function<bool()> current;

    static bool ownedByAgent(U32 rootFlags);
    static VSObjectContext fromLegacy(const LLViewerObject*);
    // Resolves the active renderer's actual object and current ancestry.
    static std::optional<VSObjectContext> resolve(const LLUUID&);
};
