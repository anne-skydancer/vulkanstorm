// Match the viewer's Autobuild-installed tinygltf implementation. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_USE_CPP14
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tinygltf/tiny_gltf.h"
#include "llcontrol.h"
#include <stdexcept>

// Linux LTO retains UI/font registration from the viewer's static libraries.
// Supply their application-owned settings without linking the viewer main.
LLControlGroup gSavedSettings("GeometryTestSettings");

class LLAvatarName;
std::string rlvGetAnonym(const LLAvatarName&)
{
    // Geometry producers have no reason to invoke name presentation. A call
    // here is a test failure, rather than a substitute implementation.
    throw std::logic_error("Geometry qualification invoked viewer name presentation");
}
