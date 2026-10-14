// Native immutable CPU mesh assets. LGPL-2.1.
#pragma once
#include "llvolume.h"
#include "llsd.h"
#include "vsresourcebudget.h"
#include <memory>
#include <array>
#include <string>
class VSNativeSession;
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
        std::array<S32, 4>                                   lodBytes{};
        S32 lod = 0;
        size_t bytes = 0;
        U64 generation = 0;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
    };
    VSWorldAssets();
    ~VSWorldAssets();
    State request(const LLVolumeParams&, S32 lod);
    std::shared_ptr<const Geometry> geometry(const LLUUID&, S32 lod) const;
    std::string error(const LLUUID&, S32 lod) const;
    void pump();
    void reset();
    // Invalidate one local asset generation without invalidating other assets.
    // Old decoded/GPU owners remain valid until their actual users retire.
    void replace(const LLUUID&);
    LLSD evidence() const;
    // Pure format/geometry admission; also used by headless regression fixtures.
    static std::shared_ptr<const Geometry> decode(const LLVolumeParams&, S32 lod,
                                                  const LLSD::Binary&, std::string& error);
private:
    void synchronizeOwner(const std::shared_ptr<VSNativeSession>&);
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
