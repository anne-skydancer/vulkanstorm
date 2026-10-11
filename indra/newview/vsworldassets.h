// Native immutable CPU mesh assets. LGPL-2.1.
#pragma once
#include "llvolume.h"
#include "llsd.h"
#include <memory>
#include <string>
class VSWorldAssets
{
public:
    enum class State { Missing, Pending, Ready, Unavailable, Error };
    struct Geometry
    {
        // Treat volume as immutable. Face positions, normals, UVs, indices and
        // packed joint weights are retained by the shared decoder.
        LLPointer<LLVolume> volume;
        LLSD skin;
        S32 lod = 0;
        size_t bytes = 0;
    };
    VSWorldAssets();
    ~VSWorldAssets();
    State request(const LLVolumeParams&, S32 lod);
    std::shared_ptr<const Geometry> geometry(const LLUUID&, S32 lod) const;
    std::string error(const LLUUID&, S32 lod) const;
    void pump();
    void reset();
    // Pure format/geometry admission; also used by headless regression fixtures.
    static std::shared_ptr<const Geometry> decode(const LLVolumeParams&, S32 lod,
                                                  const LLSD::Binary&, std::string& error);
private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
