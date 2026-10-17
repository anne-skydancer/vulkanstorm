// Backend-neutral reference teleport admission. LGPL-2.1.
#pragma once
#include "stdtypes.h"
class LLVector3d;
class LLUUID;
bool vs_teleport_location_allowed(const LLVector3d&);
bool vs_teleport_landmark_allowed(const LLUUID&);
bool vs_teleport_cancel_allowed(U32 flags);
