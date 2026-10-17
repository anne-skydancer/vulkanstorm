// Backend-neutral CPU avatar access for ordinary viewer services. LGPL-2.1.
#pragma once
#include "lluuid.h"
#include "v3dmath.h"
#include <vector>
#include <string>
#include <utility>
#include <map>
class LLCharacter;
class LLAvatarAppearance;
LLCharacter*                         vs_avatar_character();
LLAvatarAppearance*                  vs_avatar_appearance();
const std::map<LLUUID, S32>&         vs_avatar_signaled_animations();
const std::multimap<LLUUID, LLUUID>& vs_avatar_animation_sources();
LLUUID                               vs_avatar_root_id();
bool                                 vs_avatar_animation_from_seat(const LLUUID& source, const LLUUID& seat);
bool                                 vs_avatar_stop_motion(const LLUUID&, bool immediate = false);

std::vector<LLUUID> vs_avatar_wearable_items(unsigned type);

std::vector<std::pair<unsigned, std::string>> vs_avatar_attachment_points();

struct VSAvatarAttachment
{
    unsigned point;
    LLUUID   object, item;
};
std::vector<VSAvatarAttachment> vs_avatar_attachments();

bool vs_avatar_attachment_name(const LLUUID& item, std::string& name);

bool vs_avatar_precise_location(const LLUUID&,LLVector3d&);
struct VSAvatarLocation { LLUUID id; LLVector3d position; };
std::vector<VSAvatarLocation> vs_avatar_locations(const LLVector3d& relativeTo, F32 radius);
