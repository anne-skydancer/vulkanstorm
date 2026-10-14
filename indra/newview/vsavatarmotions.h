// Shared CPU avatar motion factories for peer renderers. LGPL-2.1.
#pragma once
#include "lluuid.h"
class LLCharacter;
extern const LLUUID ANIM_AGENT_BODY_NOISE;
extern const LLUUID ANIM_AGENT_BREATHE_ROT;
extern const LLUUID ANIM_AGENT_PHYSICS_MOTION;
extern const LLUUID ANIM_AGENT_EDITING;
extern const LLUUID ANIM_AGENT_EYE;
extern const LLUUID ANIM_AGENT_FLY_ADJUST;
extern const LLUUID ANIM_AGENT_HAND_MOTION;
extern const LLUUID ANIM_AGENT_HEAD_ROT;
extern const LLUUID ANIM_AGENT_PELVIS_FIX;
extern const LLUUID ANIM_AGENT_TARGET;
extern const LLUUID ANIM_AGENT_WALK_ADJUST;
void                vs_register_avatar_motions(LLCharacter&);
void                vs_start_avatar_default_motions(LLCharacter&);
LLUUID              vs_remap_avatar_motion(const LLUUID&, bool female);
