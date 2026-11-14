// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// LEAP API "ProjectorShadows" (development builds only, -DVKS_GL_STATS=ON); see llprojectorshadowstatsapi.cpp.
// The header declares nothing: the API registers itself when the file is compiled in. It includes llglsubmitstats.h only
// because that defines VKS_GL_STATS for the .cpp guard.
#pragma once

#include "llglsubmitstats.h"
