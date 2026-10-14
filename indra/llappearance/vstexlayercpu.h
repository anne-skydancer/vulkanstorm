// CPU compositor asset access and lifetime-aware allocation. LGPL-2.1.
#pragma once
#include "llimage.h"
#include "lluuid.h"
#include <functional>
#include <string>

using LLTexLayerCPUAllocate = std::function<LLPointer<LLImageRaw>(S32, S32, S32)>;
struct LLTexLayerCPUTexture
{
    std::function<LLPointer<LLImageRaw>(const LLUUID&)> load;
    LLTexLayerCPUAllocate                               allocate;
    std::function<void(const std::string&)>             reportFailure;
    LLPointer<LLImageRaw>                               operator()(const LLUUID& id) const { return load(id); }
    bool                                                fail(const std::string& reason) const
    {
        if (reportFailure)
            reportFailure(reason);
        return false;
    }
};
