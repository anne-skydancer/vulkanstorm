// Diagnostic-only readback of actual avatar composite publications. LGPL-2.1.
#pragma once
#include "vsworldavatar.h"
#include <memory>
#include <string>
class VSVulkanContext;
void vs_r2_capture_bakes(VSVulkanContext&, std::vector<VSWorldAvatar::LocalBake>, const std::string& directory,
                         const std::shared_ptr<bool>& complete);
