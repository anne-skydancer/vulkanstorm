// SPDX-License-Identifier: LGPL-2.1-or-later
#include "llviewerprecompiledheaders.h"
#include "llcomputemesh.h"
#include "llvovolume.h"
#include "llspatialpartition.h"
#include "llviewercontrol.h"
#include "llviewercamera.h"
#include "llmeshrepository.h"
#include "llmeshstreaming.h"
#include "llvolumemgr.h"
#include "llface.h"
#include "llappviewer.h"
#include "lldir.h"
#include "pipeline.h"
#include <array>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <unordered_map>
#include <cstring>
#include <deque>

namespace LLComputeMesh
{
struct Avatar
{
    LLPointer<LLVOAvatar> object;
    U32 slot = ~0u;
    U32 generation = 0;
    ~Avatar();
};
}
namespace
{
constexpr U32 AVATAR_CAPACITY = 1024;
struct AvatarInput { F32 center[4] = {}; F32 minimum[4] = {}; F32 maximum[4] = {}; };
static_assert(sizeof(AvatarInput) == 48, "std430 avatar layout");
std::array<AvatarInput, AVATAR_CAPACITY> avatar_inputs;
std::array<LLComputeMesh::Avatar*, AVATAR_CAPACITY> avatar_owners = {};
std::unordered_map<LLVOAvatar*, std::weak_ptr<LLComputeMesh::Avatar>> avatar_lookup;
std::vector<U32> available_avatars;
U32 avatar_extent = 0;
bool avatars_dirty = false;
U32 rigged_draws = 0, avatar_dispatches = 0, rigged_bypasses = 0;
constexpr U32 CAPACITY = 8192;
U64 byteBudget()
{
    static LLCachedControl<U32> mib(gSavedSettings, "RenderGLComputeMeshBudgetMiB", 1024);
    return U64(llclamp(U32(mib), 64u, 2048u)) * 1024 * 1024;
}
struct Entry { F32 centerRadius[4] = {}; U32 ranges[4][4] = {}; U32 owner[4] = {}; };
static_assert(sizeof(Entry) == 96, "std430 mesh layout");
std::array<Entry, CAPACITY> entries;
std::array<LLComputeMesh::Resident*, CAPACITY> owners = {};
std::vector<U32> available;
U32 extent = 0, generation = 1;
U32 dirty_begin = CAPACITY, dirty_end = 0;
U32 dispatched_frame = ~0u;
U32 camera_frame = ~0u;
LLVector3 lod_camera;
F32 lod_policy[4] = {};
U64 resident_bytes = 0;
GLuint program = 0, buffers[5] = {};
GLuint demand_readback = 0;
GLsync demand_fence = nullptr;
LLTimer demand_timer;
std::array<std::weak_ptr<LLComputeMesh::Object>, CAPACITY> demand_owners;
U32 demand_extent = 0;
GLint count_uniform = -1, camera_uniform = -1, policy_uniform = -1, phase_uniform = -1, avatar_count_uniform = -1;
bool failed = false;
LLTimer report_timer;
U32 draws = 0, dispatches = 0, bypasses = 0;
U64 uploaded = 0;

bool enabled()
{
    static LLCachedControl<bool> lod(gSavedSettings, "RenderGLComputeLOD", true);
    static LLCachedControl<bool> mesh(gSavedSettings, "RenderGLComputeMesh", true);
    return lod && mesh && !failed && gGLManager.mGLVersion >= 4.3f;
}
void dirty(U32 slot)
{
    dirty_begin = llmin(dirty_begin, slot);
    dirty_end = llmax(dirty_end, slot + 1);
    dispatched_frame = ~0u;
}
struct Bindings
{
    GLint shader, generic, indexed[5];
    GLint64 start[5], size[5];
    Bindings()
    {
        glGetIntegerv(GL_CURRENT_PROGRAM, &shader);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &generic);
        for (U32 i=0; i<5; ++i)
        {
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, i, &indexed[i]);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START, i, &start[i]);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE, i, &size[i]);
        }
    }
    ~Bindings()
    {
        glUseProgram(shader);
        for (U32 i=0; i<5; ++i)
        {
            if (indexed[i] && size[i]) glBindBufferRange(GL_SHADER_STORAGE_BUFFER, i, indexed[i], start[i], size[i]);
            else glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, indexed[i]);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, generic);
    }
};
bool initialize()
{
    if (program) return true;
    if (!enabled() || !glDispatchCompute || !glMemoryBarrier || !glMultiDrawElementsIndirect) return false;
    failed = true;
    std::ifstream file(gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "shaders", "class1/objects/meshLODC.glsl"));
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (source.empty()) return false;
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    char log[4096] = {};
    if (!ok)
    {
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LL_WARNS("ComputeLOD") << log << LL_ENDL;
        glDeleteShader(shader);
        return false;
    }
    GLuint linked = glCreateProgram();
    glAttachShader(linked, shader);
    glLinkProgram(linked);
    glDeleteShader(shader);
    glGetProgramiv(linked, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        glGetProgramInfoLog(linked, sizeof(log), nullptr, log);
        LL_WARNS("ComputeLOD") << log << LL_ENDL;
        glDeleteProgram(linked);
        return false;
    }
    Bindings saved;
    glGenBuffers(5, buffers);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(entries), entries.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, CAPACITY * 5 * sizeof(U32), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[2]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(avatar_inputs), avatar_inputs.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[3]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, AVATAR_CAPACITY * sizeof(U32), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[4]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, CAPACITY * sizeof(U32), nullptr, GL_DYNAMIC_DRAW);
    glGenBuffers(1, &demand_readback);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, demand_readback);
    glBufferData(GL_SHADER_STORAGE_BUFFER, CAPACITY * sizeof(U32), nullptr, GL_STREAM_READ);
    if (glGetError() != GL_NO_ERROR)
    {
        glDeleteProgram(linked);
        glDeleteBuffers(1, &demand_readback); demand_readback = 0;
        glDeleteBuffers(5, buffers);
        buffers[0] = buffers[1] = buffers[2] = buffers[3] = buffers[4] = 0;
        return false;
    }
    program = linked;
    count_uniform = glGetUniformLocation(program, "candidateCount");
    camera_uniform = glGetUniformLocation(program, "cameraOrigin");
    policy_uniform = glGetUniformLocation(program, "policy");
    phase_uniform = glGetUniformLocation(program, "phase");
    avatar_count_uniform = glGetUniformLocation(program, "avatarCount");
    failed = false;
    LL_INFOS("ComputeLOD") << "Resident four-level indirect LOD initialized; budget_mib=" << byteBudget()/1048576 << " capacity=" << CAPACITY << LL_ENDL;
    return true;
}

AvatarInput avatarInput(LLVOAvatar& avatar)
{
    AvatarInput input;
    if (avatar.isDead() || !avatar.mDrawable ||
        (!avatar.isControlAvatar() && !avatar.isFullyLoaded())) return input;
    const LLVector3 center(avatar.mDrawable->getPositionGroup().getF32ptr());
    const LLVector3* bounds = avatar.getLastAnimExtents();
    std::copy(center.mV, center.mV+3, input.center);
    input.center[3] = avatar.isControlAvatar() ? 0.5f : 1.f;
    std::copy(bounds[0].mV, bounds[0].mV+3, input.minimum);
    std::copy(bounds[1].mV, bounds[1].mV+3, input.maximum);
    input.minimum[3] = 1.f;
    return input;
}

std::shared_ptr<LLComputeMesh::Avatar> acquireAvatar(LLVOAvatar* object)
{
    if (auto found = avatar_lookup.find(object); found != avatar_lookup.end())
        if (auto avatar = found->second.lock()) return avatar;
    if (available_avatars.empty() && avatar_extent == AVATAR_CAPACITY) return {};
    auto avatar = std::make_shared<LLComputeMesh::Avatar>();
    avatar->object = object;
    avatar->slot = available_avatars.empty() ? avatar_extent++ : available_avatars.back();
    if (!available_avatars.empty()) available_avatars.pop_back();
    avatar->generation = generation;
    avatar_owners[avatar->slot] = avatar.get();
    avatar_inputs[avatar->slot] = avatarInput(*object);
    avatar_lookup[object] = avatar;
    avatars_dirty = true;
    dispatched_frame = ~0u;
    return avatar;
}

bool eligible(LLVOVolume& object)
{
    auto* drawable = object.mDrawable.get();
    static LLCachedControl<bool> debug_lods(gSavedSettings, "DebugObjectLODs", false);
    const bool lod_debug = debug_lods ||
        gPipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_LOD_INFO) ||
        gPipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_TRIANGLE_COUNT);
    if (lod_debug || !enabled() || !drawable || object.isDead() || !object.isMesh() ||
        S32(object.mComputeLODRetryFrame - gFrameCount) > 0 ||
        object.isHUDAttachment() || object.isSelected() || object.mGLTFAsset) return false;
    if (drawable->isState(LLDrawable::RIGGED))
    {
        auto* avatar = object.getAvatar();
        return avatar && !avatar->isDead() && avatar->mDrawable &&
            (avatar->isControlAvatar() || avatar->isFullyLoaded()) && object.getSkinInfo();
    }
    return !object.isAttachment() && !object.isAnimatedObject() && !drawable->isActive() &&
        !drawable->isState(LLDrawable::ANIMATED_CHILD);
}

bool dispatch()
{
    if (camera_frame != gFrameCount) return false;
    if (dispatched_frame == gFrameCount) return true;
    if (!initialize()) return false;
    LL_PROFILE_ZONE_NAMED("GL resident LOD dispatch");
    gGL.flush();
    Bindings saved;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
    if (dirty_begin < dirty_end)
    {
        const U32 end = llmin(dirty_end, extent);
        if (dirty_begin < end)
        {
            const U64 bytes = (end - dirty_begin) * sizeof(Entry);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, dirty_begin * sizeof(Entry), bytes, entries.data() + dirty_begin);
            uploaded += bytes;
        }
        dirty_begin = CAPACITY; dirty_end = 0;
    }
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffers[0]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, buffers[1]);
    if (avatars_dirty)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[2]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, avatar_extent * sizeof(AvatarInput), avatar_inputs.data());
        uploaded += avatar_extent * sizeof(AvatarInput);
        avatars_dirty = false;
    }
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, buffers[2]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, buffers[3]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, buffers[4]);
    glUseProgram(program);
    glUniform1ui(count_uniform, extent);
    glUniform1ui(avatar_count_uniform, avatar_extent);
    glUniform3fv(camera_uniform, 1, lod_camera.mV);
    glUniform4fv(policy_uniform, 1, lod_policy);
    // Order reuse after previous indirect reads; make the new commands visible
    // to the command processor. There is no CPU result buffer or fence wait.
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    if (avatar_extent)
    {
        glUniform1ui(phase_uniform, 0);
        glDispatchCompute((avatar_extent + 63) / 64, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        ++avatar_dispatches;
    }
    glUniform1ui(phase_uniform, 1);
    glDispatchCompute((extent + 63) / 64, 1, 1);
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
    // Streaming feedback is asynchronous and infrequent. Draws consume GPU
    // commands immediately; no render pass depends on this CPU readback.
    if (!demand_fence && demand_timer.getElapsedTimeF32() >= 0.25f)
    {
        GLint read_binding = 0, write_binding = 0;
        glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &read_binding);
        glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &write_binding);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        glBindBuffer(GL_COPY_READ_BUFFER, buffers[4]);
        glBindBuffer(GL_COPY_WRITE_BUFFER, demand_readback);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, extent*sizeof(U32));
        glBindBuffer(GL_COPY_READ_BUFFER, read_binding);
        glBindBuffer(GL_COPY_WRITE_BUFFER, write_binding);
        demand_extent = extent;
        for (U32 i=0; i<extent; ++i) demand_owners[i] = owners[i] ? owners[i]->owner : std::weak_ptr<LLComputeMesh::Object>{};
        demand_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        demand_timer.reset();
    }
    if (glGetError() != GL_NO_ERROR)
    {
        failed = true;
        LL_WARNS("ComputeLOD") << "Resident dispatch failed; retaining direct draws" << LL_ENDL;
        return false;
    }
    // This module owns the indirect binding; direct indexed draws do not use it.
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buffers[1]);
    if (!dispatches && !draws)
        LL_DEBUGS("ComputeLOD") << "Dispatching " << extent << " resident LOD slots" << LL_ENDL;
    ++dispatches;
    dispatched_frame = gFrameCount;
    LL_PROFILE_PLOT("Resident LOD slots", static_cast<int64_t>(extent - available.size()));
    LL_PROFILE_PLOT("Resident LOD geometry bytes", static_cast<int64_t>(resident_bytes));
    return true;
}
}

LLComputeMesh::Avatar::~Avatar()
{
    if (generation != ::generation || slot == ~0u) return;
    avatar_lookup.erase(object.get());
    avatar_owners[slot] = nullptr;
    avatar_inputs[slot] = AvatarInput{};
    available_avatars.push_back(slot);
    avatars_dirty = true;
    dispatched_frame = ~0u;
}

LLComputeMesh::Resident::~Resident()
{
    resident_bytes -= bytes;
    if (generation != ::generation || slot == ~0u) return;
    owners[slot] = nullptr;
    entries[slot] = Entry{};
    available.push_back(slot);
    dirty(slot);
}

void LLComputeMesh::invalidateLOD(LLVOVolume& object)
{
    if (object.mComputeLOD)
    {
        for (auto& face : object.mComputeLOD->faces) if (face) face->valid = false;
        object.mComputeLOD.reset();
    }
}

bool LLComputeMesh::ownsLOD(LLVOVolume& object)
{
    if (!object.mComputeLOD) return false;
    if (!eligible(object) || object.mDrawable->isState(LLDrawable::REBUILD_ALL))
    {
        invalidateLOD(object);
        return false;
    }
    for (const auto& face : object.mComputeLOD->faces)
        if (!face || !face->valid) return false;
    ++bypasses;
    if (object.mDrawable->isState(LLDrawable::RIGGED)) ++rigged_bypasses;
    return true;
}

namespace
{
struct StagedFace
{
    LLPointer<LLVertexBuffer> buffer;
    Entry entry;
    U64 bytes = 0;
    U32 vertex_offset = 0, index_offset = 0;
    ~StagedFace() { resident_bytes -= bytes; }
};
struct BuildJob
{
    LLPointer<LLVOVolume> object;
    std::weak_ptr<LLComputeMesh::Object> owner;
    LLVolume* volumes[4] = {};
    std::vector<std::shared_ptr<StagedFace>> faces;
    U32 target = 0, ready_mask = 0, failed_mask = 0, face_index = 0, lod = 0;
    bool initialized = false;
    ~BuildJob()
    {
        for (auto* volume : volumes)
            if (volume) LLPrimitive::getVolumeManager()->unrefVolume(volume);
        if (auto token = owner.lock()) token->pending = false;
    }
};
std::deque<std::shared_ptr<BuildJob>> build_jobs;
U32 preparation_frame = ~0u;
U32 prepared_steps = 0, cancelled_jobs = 0;
U64 geometry_upload_bytes = 0, geometry_copy_bytes = 0, geometry_allocation_bytes = 0;
F32 preparation_ms = 0.f, preparation_max_ms = 0.f;
enum class BuildResult { WAIT, MORE, DONE, DROP, RESOURCE_LIMIT };

void enqueueBuild(const std::shared_ptr<LLComputeMesh::Object>& owner, U32 target)
{
    if (owner->pending || build_jobs.size() >= CAPACITY) return;
    auto job = std::make_shared<BuildJob>();
    job->object = owner->object;
    job->owner = owner;
    job->target = llmin(target, 3u);
    owner->pending = true;
    build_jobs.push_back(std::move(job));
}

BuildResult advanceJob(BuildJob& job)
{
    auto* object = job.object.get();
    auto owner = job.owner.lock();
    if (!owner || object->isDead() || object->mComputeLOD != owner || !eligible(*object)) return BuildResult::DROP;
    if (object->mDrawable->isState(LLDrawable::REBUILD_ALL)) return BuildResult::WAIT;
    auto* current = object->getVolume();
    if (!current || !current->isMeshAssetLoaded()) return BuildResult::WAIT;
    const S32 count = current->getNumVolumeFaces();
    if (count <= 0 || count != object->mDrawable->getNumFaces() || size_t(count) != owner->faces.size()) return BuildResult::DROP;
    const bool rigged = object->mDrawable->isState(LLDrawable::RIGGED);
    if (!job.initialized)
    {
        for (S32 f=0; f<count; ++f)
        {
            auto* face = object->mDrawable->getFace(f);
            auto* te = object->getTE(f);
            auto* material = te ? te->getGLTFRenderMaterial() : nullptr;
            if (!owner->faces[f] || !face || !face->getVertexBuffer() || !te ||
                face->isState(LLFace::TEXTURE_ANIM) || face->hasMedia()) return BuildResult::DROP;
            if (!rigged && (!material || material->mAlphaMode != LLGLTFMaterial::ALPHA_MODE_OPAQUE || te->getGlow() != 0.f)) return BuildResult::DROP;
            if (rigged && (!face->mAvatar || !face->mSkinInfo ||
                !(face->getVertexBuffer()->getTypeMask() & LLVertexBuffer::MAP_WEIGHT4))) return BuildResult::DROP;
        }
        U32 previous_levels = 0;
        for (const auto& record : owner->faces)
            if (record->valid) previous_levels |= record->available_lods;
        bool requested = false;
        for (U32 lod=0; lod<4; ++lod)
        {
            const S32 actual = gMeshRepo.getActualMeshLOD(current->getParams(), lod);
            if (actual < 0) return BuildResult::DROP;
            if (!job.volumes[lod]) job.volumes[lod] = LLPrimitive::getVolumeManager()->refVolume(current->getParams(), actual);
            auto* source = job.volumes[lod];
            if (source && source->isMeshAssetUnavaliable()) job.failed_mask |= 1u<<lod;
            if (source && source->isMeshAssetLoaded())
            {
                if (LLMeshStreaming::retainLoadedLevel(lod, job.target, previous_levels, job.ready_mask))
                    job.ready_mask |= 1u << lod;
            }
            else if (lod<=job.target && !requested && source && !source->isMeshAssetUnavaliable())
            {
                // loadMesh resolves the first missing prerequisite, so a cold
                // request for High still starts with Lowest. One request per job.
                gMeshRepo.loadMesh(object, current->getParams(), job.target, object->getLOD());
                requested = true;
            }
        }
        if (!job.ready_mask) return job.failed_mask == 15 ? BuildResult::DROP : BuildResult::WAIT;
        bool adds_detail = false;
        for (const auto& record : owner->faces)
            adds_detail |= !record->valid || (job.ready_mask & ~record->available_lods);
        if (!adds_detail) return BuildResult::WAIT; // keep rendering; asset is still pending
        // The legacy face converter uses 16-bit vertex offsets. If keeping
        // every cached level would exceed that range, retain the finer levels
        // that fit instead of permanently refusing a requested High upgrade.
        for (;;)
        {
            bool fits = true;
            for (S32 f=0; f<count; ++f)
            {
                U32 vertices = 0;
                for (U32 lod=0; lod<4; ++lod)
                {
                    if (!(job.ready_mask & (1u<<lod))) continue;
                    auto* source = job.volumes[lod];
                    if (source->getNumVolumeFaces() != count) return BuildResult::DROP;
                    bool alias = false;
                    for (U32 previous=0; previous<lod; ++previous)
                        alias |= (job.ready_mask & (1u<<previous)) && job.volumes[previous] == source;
                    if (!alias) vertices += (source->getVolumeFace(f).mNumVertices+3)&~3u;
                }
                fits &= vertices <= 65535;
            }
            if (fits) break;
            const U32 reduced = job.ready_mask & (job.ready_mask-1);
            if (!reduced) return BuildResult::DROP;
            job.ready_mask = reduced;
        }
        U64 estimate = 0;
        U32 new_slots = 0;
        for (S32 f=0; f<count; ++f)
        {
            U32 vertices = 0, indices = 0;
            for (U32 lod=0; lod<4; ++lod)
            {
                if (!(job.ready_mask & (1u<<lod))) continue;
                auto* source = job.volumes[lod];
                if (source->getNumVolumeFaces() != count) return BuildResult::DROP;
                const auto& vf = source->getVolumeFace(f);
                if (vf.mNumVertices < 0 || vf.mNumIndices < 0 || (!vf.mNumVertices && vf.mNumIndices) ||
                    (rigged && vf.mNumVertices && !vf.mWeights)) return BuildResult::DROP;
                bool alias = false;
                for (U32 previous=0; previous<lod; ++previous)
                    alias |= (job.ready_mask & (1u<<previous)) && job.volumes[previous] == source;
                if (!alias) { vertices += (vf.mNumVertices+3)&~3u; indices += vf.mNumIndices; }
            }
            if (vertices > 65535) return BuildResult::DROP;
            estimate += U64(llmax(vertices, 4u))*llmax(128u, LLVertexBuffer::calcVertexSize(object->mDrawable->getFace(f)->getVertexBuffer()->getTypeMask())) + U64(llmax(indices, 1u))*2 + 512;
            new_slots += owner->faces[f]->slot == ~0u;
        }
        // Include the still-visible old buffers while preparing replacements.
        if (new_slots > CAPACITY-extent+available.size() || resident_bytes+estimate > byteBudget()) return BuildResult::RESOURCE_LIMIT;
        if (!initialize()) return BuildResult::DROP;
        job.faces.resize(count);
        job.initialized = true;
        return BuildResult::MORE;
    }
    if (job.face_index < U32(count))
    {
        const S32 face_index = job.face_index;
        LLFace& face = *object->mDrawable->getFace(face_index);
        const U32 mask = face.getVertexBuffer()->getTypeMask();
        auto& record = owner->faces[face_index];
        if (!job.faces[face_index])
        {
            auto staged = std::make_shared<StagedFace>();
            U32 vertices = 0, indices = 0;
            for (U32 lod=0; lod<4; ++lod)
            {
                if (!(job.ready_mask & (1u<<lod))) continue;
                bool alias = false;
                for (U32 previous=0; previous<lod; ++previous)
                    alias |= (job.ready_mask & (1u<<previous)) && job.volumes[previous] == job.volumes[lod];
                if (!alias)
                {
                    const auto& vf = job.volumes[lod]->getVolumeFace(face_index);
                    vertices += (vf.mNumVertices+3)&~3u; indices += vf.mNumIndices;
                }
            }
            if (resident_bytes+U64(llmax(vertices, 4u))*llmax(128u, LLVertexBuffer::calcVertexSize(mask))+U64(llmax(indices, 1u))*2+512 > byteBudget()) return BuildResult::RESOURCE_LIMIT;
            staged->buffer = new LLVertexBuffer(mask);
            if (!staged->buffer->allocateBuffer(llmax(vertices, 4u), llmax(indices, 1u))) return BuildResult::DROP;
            staged->bytes = staged->buffer->getSize()+staged->buffer->getIndicesSize();
            resident_bytes += staged->bytes;
            geometry_allocation_bytes += staged->bytes;
            if (rigged && !record->avatar)
            {
                record->avatar = acquireAvatar(face.mAvatar);
                if (!record->avatar) return BuildResult::DROP;
                record->skin_hash = face.getSkinHash();
            }
            staged->entry.owner[0] = record->avatar ? record->avatar->slot+1 : 0;
            job.faces[face_index] = std::move(staged);
            return BuildResult::MORE;
        }
        auto& staged = *job.faces[face_index];
        auto& entry = staged.entry;
        auto& vertex_offset = staged.vertex_offset;
        auto& index_offset = staged.index_offset;
        for (U32 lod=job.lod; lod<4; ++lod)
        {
            job.lod = lod+1;
            if (!(job.ready_mask & (1u<<lod))) continue;
            auto* source = job.volumes[lod];
            S32 alias = -1;
            for (U32 previous=0; previous<lod; ++previous)
                if ((job.ready_mask & (1u<<previous)) && job.volumes[previous] == source) { alias=previous; break; }
            if (alias >= 0)
            {
                std::copy(entry.ranges[alias], entry.ranges[alias]+4, entry.ranges[lod]);
                continue;
            }
            const auto& vf = source->getVolumeFace(face_index);
            if (!vf.mNumVertices) continue;
            const bool reuse = record->valid && (record->available_lods & (1u<<lod)) &&
                record->buffer->getTypeMask() == mask;
            if (reuse)
            {
                const auto& old_range = entries[record->slot].ranges[lod];
                staged.buffer->copyResidentRange(
                    *record->buffer, old_range[3], old_range[1],
                    vf.mNumVertices, vf.mNumIndices, vertex_offset, index_offset);
                geometry_copy_bytes += U64(vf.mNumVertices)*LLVertexBuffer::calcVertexSize(mask);
            }
            else
            {
                source->genTangents(face_index);
                LLFace temporary(object->mDrawable, object);
                temporary.setTEOffset(face_index);
                temporary.setPoolType(face.getPoolType());
                if (face.isState(LLFace::FULLBRIGHT)) temporary.setState(LLFace::FULLBRIGHT);
                if (face.isState(LLFace::USE_FACE_COLOR)) temporary.setFaceColor(face.getFaceColor());
                if (rigged)
                {
                    temporary.setState(LLFace::RIGGED);
                    temporary.mAvatar = face.mAvatar;
                    temporary.mSkinInfo = face.mSkinInfo;
                }
                // Use the real face's texture index and texture-dependent bump inputs.
                // setTextureIndex normally marks REBUILD_POSITION; constructing an
                // alternate range must not introduce an extra drawable rebuild.
                const bool position_dirty = object->mDrawable->isState(LLDrawable::REBUILD_POSITION);
                temporary.setTextureIndex(face.getTextureIndex());
                if (!position_dirty) object->mDrawable->clearState(LLDrawable::REBUILD_POSITION);
                for (U32 channel=0; channel<LLRender::NUM_TEXTURE_CHANNELS; ++channel)
                    temporary.setTexture(channel, face.getTexture(channel));
                temporary.setSize(vf.mNumVertices, vf.mNumIndices, true);
                temporary.setGeomIndex(vertex_offset);
                temporary.setIndicesIndex(index_offset);
                temporary.setVertexBuffer(staged.buffer);
                // The established conversion applies bind shape and inverse-transpose
                // exactly once for rigged faces and copies this LOD's skin weights.
                if (!temporary.getGeometryVolume(*source, face_index, object->getRelativeXform(),
                        object->getRelativeXformInvTrans(), vertex_offset, true)) return BuildResult::DROP;

            }
            entry.ranges[lod][0] = vf.mNumIndices;
            entry.ranges[lod][1] = index_offset;
            entry.ranges[lod][2] = 1;
            entry.ranges[lod][3] = vertex_offset; // host-side copy origin; indices are already rebased
            vertex_offset += (vf.mNumVertices+3)&~3u; index_offset += vf.mNumIndices;
            staged.buffer->unmapBuffer();
            geometry_upload_bytes += (reuse ? 0 : U64(vf.mNumVertices)*LLVertexBuffer::calcVertexSize(mask))+U64(vf.mNumIndices)*2;
            return BuildResult::MORE;
        }
        ++job.face_index; job.lod = 0;
        return BuildResult::MORE;
    }
    U32 new_slots = 0;
    for (const auto& record : owner->faces) new_slots += record->slot == ~0u;
    if (new_slots > CAPACITY-extent+available.size()) return BuildResult::RESOURCE_LIMIT;
    // Publish one complete mesh generation at the main-world frame boundary.
    // All its material/depth/shadow passes see the same available-level mapping.
    for (S32 f=0; f<count; ++f)
    {
        auto& staged = *job.faces[f];
        auto& record = owner->faces[f];
        for (U32 lod=0; lod<4; ++lod)
        {
            if (job.ready_mask & (1u<<lod)) continue;
            const U32 fallback = LLMeshStreaming::residentLevel(job.ready_mask, lod);
            std::copy(staged.entry.ranges[fallback], staged.entry.ranges[fallback]+4, staged.entry.ranges[lod]);
        }
        resident_bytes -= record->bytes;
        record->buffer = staged.buffer;
        record->bytes = staged.bytes; staged.bytes = 0;
        const LLVector3 center(object->mDrawable->getPositionGroup().getF32ptr());
        std::copy(center.mV, center.mV+3, staged.entry.centerRadius);
        staged.entry.centerRadius[3] = current->mLODScaleBias.scaledVec(object->getScale()).length();
        if (record->slot == ~0u)
        {
            if (available.empty() && extent == CAPACITY) return BuildResult::DROP;
            record->slot = available.empty() ? extent++ : available.back();
            if (!available.empty()) available.pop_back();
            record->generation = generation;
            owners[record->slot] = record.get();
        }
        entries[record->slot] = staged.entry;
        dirty(record->slot);
        record->available_lods = job.ready_mask;
        record->failed_lods = job.failed_mask;
        record->valid = true;
    }
    return BuildResult::DONE;
}

void consumeDemand()
{
    if (!demand_fence) return;
    const GLenum status = glClientWaitSync(demand_fence, 0, 0);
    if (status == GL_TIMEOUT_EXPIRED) return;
    glDeleteSync(demand_fence); demand_fence = nullptr;
    if (status == GL_WAIT_FAILED) return;
    std::array<U32, CAPACITY> desired = {};
    GLint binding = 0;
    glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &binding);
    glBindBuffer(GL_COPY_READ_BUFFER, demand_readback);
    glGetBufferSubData(GL_COPY_READ_BUFFER, 0, demand_extent*sizeof(U32), desired.data());
    glBindBuffer(GL_COPY_READ_BUFFER, binding);
    for (U32 i=0; i<demand_extent; ++i)
    {
        auto owner = demand_owners[i].lock();
        auto* record = owners[i];
        if (!owner || !record || !record->valid || record->owner.lock() != owner || desired[i]>3) continue;
        if (!((record->available_lods | record->failed_lods) & (1u<<desired[i]))) enqueueBuild(owner, desired[i]);
    }
}

void processBuildJobs()
{
    if (preparation_frame == gFrameCount) return;
    preparation_frame = gFrameCount;
    LL_PROFILE_ZONE_NAMED("GL resident LOD bounded preparation");
    static LLCachedControl<F32> budget_ms(gSavedSettings, "RenderGLComputeMeshPrepareMS", 1.5f);
    LLTimer timer;
    const U64 bytes_before = geometry_upload_bytes+geometry_copy_bytes+geometry_allocation_bytes;
    U32 steps = 0;
    const size_t scans = llmin(build_jobs.size(), size_t(64));
    for (size_t i=0; i<scans && steps<32; ++i)
    {
        if (timer.getElapsedTimeF32()*1000.f >= llclamp(F32(budget_ms), 0.1f, 10.f) ||
            geometry_upload_bytes+geometry_copy_bytes+geometry_allocation_bytes-bytes_before >= 4ull*1024*1024) break;
        auto job = build_jobs.front(); build_jobs.pop_front();
        auto result = advanceJob(*job);
        if (result == BuildResult::MORE || result == BuildResult::DONE) ++steps;
        if (result == BuildResult::WAIT || result == BuildResult::MORE) build_jobs.push_back(std::move(job));
        else if (result == BuildResult::DROP || result == BuildResult::RESOURCE_LIMIT)
        {
            ++cancelled_jobs;
            auto owner = job->owner.lock();
            auto* object = job->object.get();
            if (owner && !object->isDead() && object->mComputeLOD == owner)
            {
                bool has_resident = false;
                for (const auto& face : owner->faces) has_resident |= face && face->valid;
                if (result == BuildResult::RESOURCE_LIMIT || has_resident)
                {
                    // Failure to refine must not freeze CPU LOD at the coarse
                    // placeholder. Back off admission while normal drawing catches up.
                    object->mComputeLODRetryFrame = gFrameCount + 300;
                    LLComputeMesh::invalidateLOD(*object);
                    if (has_resident) object->forceLOD(job->target);
                    object->notifyMeshLoaded();
                }
            }
        }
    }
    const F32 elapsed = timer.getElapsedTimeF32()*1000.f;
    preparation_ms += elapsed;
    preparation_max_ms = llmax(preparation_max_ms, elapsed);
    prepared_steps += steps;
}
}

bool LLComputeMesh::preserveLODOnMeshLoad(LLVOVolume& object)
{
    if (!object.mComputeLOD || !eligible(object)) return false;
    for (const auto& face : object.mComputeLOD->faces) if (!face || !face->valid) return false;
    return true;
}

std::shared_ptr<LLComputeMesh::Resident> LLComputeMesh::prepareFace(LLFace& face)
{
    auto* object = face.getDrawable()->getVOVolume();
    if (!object || !eligible(*object) || !face.getVertexBuffer()) return {};
    const S32 index = face.getTEOffset(), count = object->mDrawable->getNumFaces();
    if (index < 0 || index >= count) return {};
    if (object->mComputeLOD && object->mComputeLOD->faces.size() != size_t(count)) invalidateLOD(*object);
    if (object->mComputeLOD && object->mComputeLOD->faces[index]) return object->mComputeLOD->faces[index];
    if (build_jobs.size() >= CAPACITY) return {};
    if (!object->mComputeLOD)
    {
        object->mComputeLOD = std::make_shared<Object>();
        object->mComputeLOD->object = object;
        object->mComputeLOD->faces.resize(count);
    }
    auto record = std::make_shared<Resident>();
    record->owner = object->mComputeLOD;
    object->mComputeLOD->faces[index] = record;
    enqueueBuild(object->mComputeLOD, object->getLOD());
    return record;
}

bool LLComputeMesh::drawLOD(LLDrawInfo& info)
{
    const auto& record = info.mComputeLOD;
    if (!record || !record->valid || !enabled()) return false;
    if (record->avatar && (record->avatar->object->isDead() ||
        (!record->avatar->object->isControlAvatar() && !record->avatar->object->isFullyLoaded()) ||
        info.mAvatar != record->avatar->object || info.getSkinHash() != record->skin_hash)) return false;
    if (!dispatch()) return false;
    record->buffer->setBuffer();
    record->buffer->drawIndirect(LLRender::TRIANGLES, record->slot, 1);
    ++draws;
    if (record->avatar) ++rigged_draws;
    return true;
}

void LLComputeMesh::beginLOD()
{
    consumeDemand();
    processBuildJobs();
    // Snapshot the world camera before shadow/reflection cameras are installed.
    lod_camera = LLViewerCamera::getInstance()->getOrigin();
    static LLCachedControl<bool> ignore_fov(gSavedSettings, "IgnoreFOVZoomForLODs");
    F32 factor = LLVOVolume::sLODFactor;
    if (!ignore_fov) factor *= DEFAULT_FIELD_OF_VIEW / LLViewerCamera::getInstance()->getDefaultFOV();
    lod_policy[0] = LLVOVolume::sDistanceFactor;
    lod_policy[1] = LLVOVolume::sLODFactor * 2.f;
    lod_policy[2] = factor;
    lod_policy[3] = LLPipeline::sDynamicLOD ? 1.f : 0.f;
    for (U32 i=0; i<avatar_extent; ++i)
    {
        auto* owner = avatar_owners[i];
        if (!owner) continue;
        AvatarInput input = avatarInput(*owner->object);
        if (std::memcmp(&input, &avatar_inputs[i], sizeof(input)))
        {
            avatar_inputs[i] = input;
            avatars_dirty = true;
            dispatched_frame = ~0u;
        }
    }
    camera_frame = gFrameCount;
    if (report_timer.getElapsedTimeF32() >= 5.f)
    {
        LL_INFOS("ComputeLOD") << "allocated_faces=" << extent - available.size()
            << " geometry_bytes=" << resident_bytes << " dispatches=" << dispatches
            << " indirect_draws=" << draws << " cpu_lod_bypasses=" << bypasses
            << " metadata_upload_bytes=" << uploaded
            << " rigged_indirect_draws=" << rigged_draws << " rigged_cpu_lod_bypasses=" << rigged_bypasses
            << " avatar_dispatches=" << avatar_dispatches
            << " budget_mib=" << byteBudget()/1048576
            << " preparation_pending=" << build_jobs.size() << " preparation_steps=" << prepared_steps
            << " preparation_ms=" << preparation_ms << " preparation_max_frame_ms=" << preparation_max_ms
            << " geometry_allocation_bytes=" << geometry_allocation_bytes
            << " geometry_copy_bytes=" << geometry_copy_bytes << " geometry_upload_bytes=" << geometry_upload_bytes << " preparation_cancelled=" << cancelled_jobs << LL_ENDL;
        prepared_steps = cancelled_jobs = 0;
        geometry_upload_bytes = geometry_copy_bytes = geometry_allocation_bytes = 0;
        preparation_ms = preparation_max_ms = 0.f;
        rigged_draws = rigged_bypasses = avatar_dispatches = 0;
        draws = dispatches = bypasses = 0; uploaded = 0; report_timer.reset();
    }

}

void LLComputeMesh::shiftLOD(const LLVector3& offset)
{
    for (auto& job : build_jobs)
    {
        job->faces.clear(); job->initialized = false; job->face_index = job->lod = 0;
    }
    for (U32 i = 0; i < extent; ++i)
    {
        if (!owners[i]) continue;
        for (U32 a=0; a<3; ++a) entries[i].centerRadius[a] += offset.mV[a];
        dirty(i);
    }
}

void LLComputeMesh::reloadLOD()
{
    // Shader reload does not invalidate immutable geometry. Preserve residency
    // and recreate only the program and command/metadata buffers on next draw.
    if (demand_fence) glDeleteSync(demand_fence);
    demand_fence = nullptr;
    if (demand_readback) glDeleteBuffers(1, &demand_readback);
    demand_readback = 0;
    if (buffers[0]) glDeleteBuffers(5, buffers);
    if (program) glDeleteProgram(program);
    program = buffers[0] = buffers[1] = buffers[2] = buffers[3] = buffers[4] = 0;
    failed = false;
    dispatched_frame = ~0u;
}

void LLComputeMesh::destroyLOD()
{
    build_jobs.clear();
    preparation_frame = ~0u;
    for (auto* owner : owners) if (owner) owner->valid = false;
    avatar_owners.fill(nullptr);
    avatar_lookup.clear();
    available_avatars.clear();
    avatar_inputs.fill(AvatarInput{});
    avatar_extent = 0;
    avatars_dirty = false;
    owners.fill(nullptr);
    entries.fill(Entry{});
    available.clear();
    extent = 0;
    ++generation;
    if (demand_fence) glDeleteSync(demand_fence);
    demand_fence = nullptr;
    if (demand_readback) glDeleteBuffers(1, &demand_readback);
    demand_readback = 0;
    if (buffers[0]) glDeleteBuffers(5, buffers);
    if (program) glDeleteProgram(program);
    program = buffers[0] = buffers[1] = buffers[2] = buffers[3] = buffers[4] = 0;
    failed = false;
    dirty_begin = CAPACITY; dirty_end = 0;
    dispatched_frame = ~0u;
}
