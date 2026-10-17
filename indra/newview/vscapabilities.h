// Shared simulator capability catalogue. LGPL-2.1.
#pragma once
#include "llsd.h"
void vs_build_capability_names(LLSD& capabilityNames);
// Includes the native service names historically requested regardless of HTTP inventory policy.
LLSD vs_native_capability_names();
