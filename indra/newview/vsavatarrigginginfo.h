// Original rigged-avatar spatial envelope, independent of graphics API. LGPL-2.1.
#pragma once
#include "llrigginginfo.h"
#include "vsresourcebudget.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>
class LLVolume;
class LLMeshSkinInfo;
class LLCharacter;
struct VSAvatarRiggingInfo
{
    std::shared_ptr<const VSResourceBudget::Reservation> residency;
    struct Joint {std::string name;LLJointRiggingInfo bounds;};
    std::vector<Joint> joints;
    static std::shared_ptr<const VSAvatarRiggingInfo> create(const LLVolume&,const LLMeshSkinInfo&,
        const std::function<std::shared_ptr<const VSResourceBudget::Reservation>(std::size_t)>&, S32 face = -1);
};
