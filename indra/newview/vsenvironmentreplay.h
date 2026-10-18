// Actual current-setting sky pixel replay; not full environment shader parity. LGPL-2.1.
#pragma once
class VSVulkanContext;
class LLSD;
bool vs_qualify_environment_pixels(VSVulkanContext&,const char* tag,LLSD&);
