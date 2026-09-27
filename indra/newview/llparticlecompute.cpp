// SPDX-License-Identifier: LGPL-2.1-or-later
#include "llviewerprecompiledheaders.h"
#include "llparticlecompute.h"
#include "llface.h"
#include "llvopartgroup.h"
#include "llviewerpartsource.h"
#include "llviewercamera.h"
#include "llagentcamera.h"
#include "llviewerregion.h"
#include "lldir.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>

namespace
{
constexpr size_t MAX_PARTICLES = 16384; // existing U16 quad indices
struct ParticleInput
{
    F32 center_scale[4];
    F32 axis_velocity_scale[4];
    F32 parent_center_scale[4];
    F32 parent_axis_flags[4];
    U32 appearance[4];
};
static_assert(sizeof(ParticleInput) == 80, "std430 particle stride");
static_assert(offsetof(ParticleInput, appearance) == 64, "std430 particle fields");
// One dispatch writes a scratch arena, then GPU copies publish each group's
// original vertex buffer. Strong references prevent pool reuse while queued.
struct GroupInput
{
    U32 offsets[4];
    U32 first_uv[4];
    F32 camera[4];
    F32 normal[4];
};
static_assert(sizeof(GroupInput) == 64, "std430 particle group stride");
struct Destination
{
    LLPointer<LLVertexBuffer> buffer;
    U32 offset;
};
constexpr U32 MAX_OUTPUT_BYTES = 16 * 1024 * 1024;
std::vector<ParticleInput> inputs;
std::vector<GroupInput> groups;
std::vector<Destination> destinations;
U32 output_bytes = 0;
GLuint program = 0, input_buffer = 0, group_buffer = 0, output_buffer = 0;
GLint count_location = -1;
void flushRequired()
{
    if (!LLParticleCompute::flush())
        LL_ERRS("ParticleCompute") << "Required GPU particle batch failed; cannot draw incomplete geometry" << LL_ENDL;
}
bool failed = false;

// Raw compute calls must not invalidate the renderer's shader or indexed-buffer
// caches. Restore both base/range bindings and the generic binding.
struct Bindings
{
    GLint shader = 0, generic = 0, indexed[3] = {};
    GLint copy_read = 0, copy_write = 0;
    GLint64 start[3] = {}, size[3] = {};
    Bindings()
    {
        glGetIntegerv(GL_CURRENT_PROGRAM, &shader);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &generic);
        glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &copy_read);
        glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &copy_write);
        for (U32 i = 0; i < 3; ++i)
        {
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, i, &indexed[i]);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START, i, &start[i]);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE, i, &size[i]);
        }
    }
    ~Bindings()
    {
        glUseProgram(shader);
        for (U32 i = 0; i < 3; ++i)
        {
            if (indexed[i] && size[i])
                glBindBufferRange(GL_SHADER_STORAGE_BUFFER, i, indexed[i], start[i], size[i]);
            else
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, indexed[i]);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, generic);
        glBindBuffer(GL_COPY_READ_BUFFER, copy_read);
        glBindBuffer(GL_COPY_WRITE_BUFFER, copy_write);
    }
};

bool initialize()
{
    if (program) return true;
    failed = true;
    std::ifstream file(gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS,
        "shaders", "class1/objects/particleGeometryC.glsl"));
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (source.empty())
    {
        LL_WARNS("ParticleCompute") << "Required particle compute shader is missing" << LL_ENDL;
        return false;
    }
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
        LL_WARNS("ParticleCompute") << log << LL_ENDL;
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
        LL_WARNS("ParticleCompute") << log << LL_ENDL;
        glDeleteProgram(linked);
        return false;
    }
    glGenBuffers(1, &input_buffer);
    glGenBuffers(1, &group_buffer);
    glGenBuffers(1, &output_buffer);
    program = linked;
    count_location = glGetUniformLocation(program, "particleCount");
    if (!input_buffer || !group_buffer || !output_buffer || count_location < 0)
    {
        LLParticleCompute::destroyGL();
        failed = true;
        return false;
    }
    failed = false;
    LL_INFOS("ParticleCompute") << "GPU particle geometry initialized" << LL_ENDL;
    return true;
}
}

bool LLParticleCompute::initGL()
{
    if (failed || gGLManager.mGLVersion < 4.3f) return false;
#if LL_WINDOWS && !LL_MESA
    if (!glDispatchCompute || !glMemoryBarrier || !glCopyBufferSubData) return false;
#endif
    return initialize();
}

bool LLParticleCompute::generate(LLVertexBuffer& buffer, const std::vector<LLFace*>& faces)
{
    if (faces.empty()) return true;
    if (failed || gGLManager.mGLVersion < 4.3f ||
        faces.size() > MAX_PARTICLES ||
        !buffer.getGLBuffer() ||
        buffer.getNumVerts() < faces.size() * 4 ||
        buffer.getTypeMask() != LLVOPartGroup::VERTEX_DATA_MASK ||
        buffer.getSize() > MAX_OUTPUT_BYTES)
        return false;
    LL_PROFILE_ZONE_NAMED("Particle GPU geometry queue");
    if (inputs.size() + faces.size() > MAX_PARTICLES ||
        output_bytes + buffer.getSize() > MAX_OUTPUT_BYTES)
        flushRequired();
    const size_t first = inputs.size();
    const auto reject = [first]() { inputs.resize(first); return false; };
    // Match LLVOPartGroup::getCameraPosition(), including during secondary views.
    const LLVector3 camera = gAgentCamera.getCameraPositionAgent();
    const LLVector3 normal = -LLViewerCamera::getInstance()->getXAxis();
    if (inputs.capacity() < first + faces.size()) inputs.reserve(MAX_PARTICLES);
    for (const LLFace* face : faces)
    {
        auto* object = static_cast<LLVOPartGroup*>(face->getViewerObject());
        if (!object) return reject();
        const auto* group = object->getViewerPartGroup();
        const S32 index = face->getTEOffset();
        if (!group || index < 0 || size_t(index) >= group->mParticles.size()) return reject();
        const auto* part = group->mParticles[index];
        if (!part) return reject();
        ParticleInput input{};
        std::copy(part->mPosAgent.mV, part->mPosAgent.mV + 3, input.center_scale);
        input.center_scale[3] = part->mScale.mV[0];
        U32 flags = object->getPartitionType() == LLViewerRegion::PARTITION_HUD_PARTICLE ? 4 : 0;
        const LLColor4U color = part->mColor;
        LLColor4U parent_color = color, parent_glow = part->mGlow;
        LLVector3 axis_velocity = part->mVelocity;
        if (part->mFlags & LLPartData::LL_PART_RIBBON_MASK)
        {
            flags |= 1;
            axis_velocity = part->mAxis;
            LLVector3 parent_position = part->mPosAgent, parent_axis = part->mAxis;
            F32 parent_scale = part->mScale.mV[0];
            if (part->mParent)
            {
                parent_position = part->mParent->mPosAgent;
                parent_axis = part->mParent->mAxis;
                parent_scale = part->mParent->mScale.mV[0];
                parent_color = part->mParent->mColor;
                parent_glow = part->mParent->mGlow;
            }
            else
            {
                parent_color = part->mStartColor;
                parent_glow = LLColor4U(0, 0, 0, (U8)ll_round(255.f * part->mStartGlow));
                if (part->mPartSourcep.notNull() && part->mPartSourcep->mSourceObjectp.notNull())
                {
                    parent_position = part->mPartSourcep->mPosAgent;
                    parent_axis = LLVector3(0, 0, 1) * part->mPartSourcep->mSourceObjectp->getRenderRotation();
                    parent_scale = part->mStartScale.mV[0];
                }
            }
            std::copy(parent_position.mV, parent_position.mV + 3, input.parent_center_scale);
            input.parent_center_scale[3] = parent_scale;
            std::copy(parent_axis.mV, parent_axis.mV + 3, input.parent_axis_flags);
        }
        else if (part->mFlags & LLPartData::LL_PART_FOLLOW_VELOCITY_MASK)
            flags |= 2;
        std::copy(axis_velocity.mV, axis_velocity.mV + 3, input.axis_velocity_scale);
        input.axis_velocity_scale[3] = part->mScale.mV[1];
        flags |= static_cast<U32>(groups.size()) << 3;
        std::memcpy(&input.parent_axis_flags[3], &flags, sizeof(flags));
        std::memcpy(&input.appearance[0], color.mV, sizeof(U32));
        std::memcpy(&input.appearance[1], part->mGlow.mV, sizeof(U32));
        std::memcpy(&input.appearance[2], parent_color.mV, sizeof(U32));
        std::memcpy(&input.appearance[3], parent_glow.mV, sizeof(U32));
        inputs.push_back(input);
    }
    GroupInput group{};
    const U32 base = output_bytes / sizeof(U32);
    group.offsets[0] = base + buffer.getOffset(LLVertexBuffer::TYPE_VERTEX)/4;
    group.offsets[1] = base + buffer.getOffset(LLVertexBuffer::TYPE_NORMAL)/4;
    group.offsets[2] = base + buffer.getOffset(LLVertexBuffer::TYPE_COLOR)/4;
    group.offsets[3] = base + buffer.getOffset(LLVertexBuffer::TYPE_EMISSIVE)/4;
    group.first_uv[0] = static_cast<U32>(first);
    group.first_uv[1] = base + buffer.getOffset(LLVertexBuffer::TYPE_TEXCOORD0)/4;
    std::copy(camera.mV, camera.mV + 3, group.camera);
    std::copy(normal.mV, normal.mV + 3, group.normal);
    groups.push_back(group);
    destinations.push_back({ &buffer, output_bytes });
    output_bytes += buffer.getSize();
    // Publish existing index/UV uploads before this buffer becomes GPU-owned.
    buffer.unmapBuffer();
    buffer.setBeforeBind(flushRequired);
    return true;
}

bool LLParticleCompute::flush()
{
    if (inputs.empty()) return true;
    if (!initGL()) return false;
    LL_PROFILE_ZONE_NAMED("Particle GPU geometry batch");
    gGL.flush();
    Bindings saved;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, input_buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, inputs.size()*sizeof(ParticleInput), inputs.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, group_buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, groups.size()*sizeof(GroupInput), groups.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, output_buffer);
    // Orphan the arena: subsequent batches cannot overwrite in-flight copies.
    glBufferData(GL_SHADER_STORAGE_BUFFER, output_bytes, nullptr, GL_STREAM_DRAW);
    if (glGetError() != GL_NO_ERROR)
    {
        failed = true;
        LL_WARNS("ParticleCompute") << "Particle GPU batch allocation failed" << LL_ENDL;
        return false;
    }
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, input_buffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, output_buffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, group_buffer);
    glUseProgram(program);
    glUniform1ui(count_location, static_cast<U32>(inputs.size()));
    glDispatchCompute((static_cast<U32>(inputs.size()) + 63)/64, 1, 1);
    // Make compute writes visible to buffer-copy reads. The copies themselves
    // are ordered with subsequent vertex fetches by ordinary GL command ordering.
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glBindBuffer(GL_COPY_READ_BUFFER, output_buffer);
    for (const auto& destination : destinations)
    {
        glBindBuffer(GL_COPY_WRITE_BUFFER, destination.buffer->getGLBuffer());
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
            destination.offset, 0, destination.buffer->getSize());
    }
    if (glGetError() != GL_NO_ERROR)
    {
        failed = true;
        LL_WARNS("ParticleCompute") << "Particle GPU batch dispatch/copy failed" << LL_ENDL;
        return false;
    }
    for (auto& destination : destinations) destination.buffer->setBeforeBind(nullptr);
    inputs.clear();
    groups.clear();
    destinations.clear();
    output_bytes = 0;
    return true;
}

void LLParticleCompute::destroyGL()
{
    for (auto& destination : destinations) destination.buffer->setBeforeBind(nullptr);
    destinations.clear();
    inputs.clear();
    groups.clear();
    output_bytes = 0;
    if (input_buffer) glDeleteBuffers(1, &input_buffer);
    if (group_buffer) glDeleteBuffers(1, &group_buffer);
    if (output_buffer) glDeleteBuffers(1, &output_buffer);
    if (program) glDeleteProgram(program);
    input_buffer = group_buffer = output_buffer = program = 0;
    failed = false;
}
