// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef LL_LLCOMPUTEMESH_H
#define LL_LLCOMPUTEMESH_H
#include "stdtypes.h"
#include "llvertexbuffer.h"
#include "llcomputedependencies.h"
#include <memory>
#include <vector>
class LLFace;
class LLDrawInfo;
class LLVOVolume;
class LLVector3;
namespace LLComputeMesh
{
struct Avatar;
struct Object;
struct Resident
{
    std::weak_ptr<Object> owner;
    std::shared_ptr<Avatar> avatar;
    U32 available_lods = 0, failed_lods = 0, excluded_lods = 0;
    U64 skin_hash = 0;
    LLPointer<LLVertexBuffer> buffer;
    U32 slot = ~0u;
    U32 generation = 0;
    U64 bytes = 0;
    bool valid = false;
    ~Resident();
};
struct Object
{
    LLVOVolume* object = nullptr; // valid only while this object owns the token
    bool pending = false;
    bool blocked = false, resource_waiting = false;
    U32 requested_lod = 0;
    U64 dependency_epoch = 0;
    std::vector<std::shared_ptr<Resident>> faces;
};
std::shared_ptr<Resident> prepareFace(LLFace& face);
bool ownsLOD(LLVOVolume& object);
bool preserveLODOnMeshLoad(LLVOVolume& object);
void invalidateLOD(LLVOVolume& object);
void notifyLODDependency(LLVOVolume& object, unsigned changed);
void shiftLOD(const LLVector3& offset);
void beginLOD();
bool drawLOD(LLDrawInfo& info);
void reloadLOD();
void destroyLOD();
void destroyGL();
}
#endif
