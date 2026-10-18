// Original LLSkinningUtil rigging-envelope policy on real native skeletons.
// LGPL-2.1. Keep >.2 influence threshold, bind-pose matrices and joint numbering.
#include "llviewerprecompiledheaders.h"
#include "vsavatarrigginginfo.h"
#include "llvolume.h"
#include "llmodel.h"
#include "llcharacter.h"
#include "lljoint.h"
#include "llmatrix4a.h"
#include <array>
#include <cmath>
std::shared_ptr<const VSAvatarRiggingInfo> VSAvatarRiggingInfo::create(const LLVolume& volume,const LLMeshSkinInfo& skin,
    const std::function<std::shared_ptr<const VSResourceBudget::Reservation>(std::size_t)>& reserve, S32 selectedFace)
{
    const auto count=skin.mJointNames.size();
    if(!reserve||!count||count>LL_CHARACTER_MAX_ANIMATED_JOINTS)return {};
    std::size_t bytes=sizeof(VSAvatarRiggingInfo)+sizeof(VSAvatarRiggingInfo::Joint)*count;
    for(const auto& name:skin.mJointNames){if(name.size()>256)return {};bytes+=name.size()+1;}
    auto residency=reserve(bytes);if(!residency)return {};
    auto result=std::make_shared<VSAvatarRiggingInfo>();result->residency=std::move(residency);
    result->joints.resize(count);
    for(unsigned i=0;i<count;++i)result->joints[i].name=skin.mJointNames[i];
    if(selectedFace < -1 || selectedFace >= volume.getNumVolumeFaces()) return {};
    const S32 firstFace = selectedFace < 0 ? 0 : selectedFace;
    const S32 endFace = selectedFace < 0 ? volume.getNumVolumeFaces() : selectedFace + 1;
    for(S32 faceIndex=firstFace;faceIndex<endFace;++faceIndex)
    {
        const auto& face=volume.getVolumeFace(faceIndex);
        if(face.mNumVertices<=0||!face.mWeights||!face.mPositions)continue;
        for(S32 vertex=0;vertex<face.mNumVertices;++vertex)
        {
            const F32* weights=face.mWeights[vertex].getF32ptr();
            for(unsigned component=0;component<4;++component)
            {
                const F32 weight=weights[component];
                if(!std::isfinite(weight)||weight<0||weight>=F32(LL_CHARACTER_MAX_ANIMATED_JOINTS))return {};
                const S32 index=llclamp(S32(std::floor(weight)),0,S32(LL_CHARACTER_MAX_ANIMATED_JOINTS)-1);
                const F32 influence=weight-index;
                if(influence>.2f&&S32(count)>index)
                {
                    auto& joint=result->joints[index].bounds;joint.setIsRiggedTo(true);
                    const auto& matrix=skin.mBindPoseMatrix.size()>index?skin.mBindPoseMatrix[index]:LLMatrix4a::identity();
                    LLVector4a position;matrix.affineTransform(face.mPositions[vertex],position);
                    auto* extents=joint.getRiggedExtents();update_min_max(extents[0],extents[1],position);
                }
            }
        }
    }
    return result;
}
