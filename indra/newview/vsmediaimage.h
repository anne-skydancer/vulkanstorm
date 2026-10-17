// Shared native publication of borrowed media plugin pixels. LGPL-2.1.
#pragma once
#include "lluiimage.h"
#include <string>
class LLPluginClassMedia;
class VSUIResources;
// Keeps the previous generation and plugin dirtiness on publication refusal.
LLUIImagePtr vs_update_media_image(VSUIResources&, const std::string&, LLPluginClassMedia&);
