/** Linux GLVND selection for the private Mesa/Zink runtime. */
#ifndef LL_LLLINUXZINK_H
#define LL_LLLINUXZINK_H

#include <array>
#include <cstdlib>
#include <dlfcn.h>
#include <string>

namespace LLLinuxZink
{
// GLVND finds an already-open provider by its unique SONAME. Loading an absolute
// path avoids changing LD_LIBRARY_PATH or replacing the system Mesa provider.
// Keep the handle for the process lifetime: GL dispatch retains its entrypoints.
inline bool activate(const std::string& directory, std::string& error)
{
    const std::string library = directory + "/libGLX_vulkanstorm.so.0";
    void* provider = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!provider)
    {
        error = dlerror();
        return false;
    }
    if (!dlsym(provider, "__glx_Main"))
    {
        error = "Bundled Mesa provider is missing the GLVND entrypoint";
        dlclose(provider);
        return false;
    }
    return true;
}

class Environment
{
public:
    Environment() = default;
    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;
    ~Environment() { restore(); }

    void apply()
    {
        if (mApplied) return;
        for (auto& setting : mSettings)
        {
            const char* value = std::getenv(setting.name);
            setting.present = value != nullptr;
            setting.previous = value ? value : "";
            setenv(setting.name, setting.value, 1);
        }
        mApplied = true;
    }

    void restore()
    {
        if (!mApplied) return;
        for (const auto& setting : mSettings)
        {
            if (setting.present) setenv(setting.name, setting.previous.c_str(), 1);
            else unsetenv(setting.name);
        }
        mApplied = false;
    }

private:
    struct Setting
    {
        const char* name;
        const char* value;
        bool present = false;
        std::string previous{};
    };
    std::array<Setting, 5> mSettings{{
        {"__GLX_VENDOR_LIBRARY_NAME", "vulkanstorm"},
        {"MESA_LOADER_DRIVER_OVERRIDE", "zink"},
        {"GALLIUM_DRIVER", "zink"},
        {"SDL_VIDEODRIVER", "x11"},
        {"SDL_VIDEO_X11_FORCE_EGL", "0"},
    }};
    bool mApplied = false;
};
}
#endif
