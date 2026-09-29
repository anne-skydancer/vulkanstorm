// SPDX-License-Identifier: LGPL-2.1-or-later
#include "llviewerprecompiledheaders.h"
#include "llmeshgeometry.h"
#include "llvertexbuffer.h"
#include "llvolume.h"
#include "llspatialpartition.h"
#include "llcomputemesh.h"
#include "llmatrix4a.h"
#include "lldir.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace
{
constexpr U64 CACHE_BYTES = 128ull * 1024 * 1024;
constexpr size_t MAX_JOBS = 2048;
struct Source
{
    LLPointer<LLVolume> volume; // prohibits address reuse while a cache key exists
    GLuint buffer = 0;
    U32 vertices = 0;
    U64 bytes = 0, used = 0;
    ~Source() { if (buffer) glDeleteBuffers(1, &buffer); }
};
using Key = std::tuple<const LLVolume*, U64, S32, bool>;
std::map<Key, std::shared_ptr<Source>> sources;
U64 cached_bytes = 0, serial = 0;
struct Job
{
    LLPointer<LLVertexBuffer> destination;
    std::shared_ptr<Source> source;
    LLPointer<LLVertexBuffer> copy_source;
    U32 operation = 0;
    bool index_destination = false;
    LLMeshGeometry::Texcoords uv;
    std::array<F32, 16> position{}, normal{};
    std::array<U32, 4> input, output;
};
std::vector<Job> jobs;
struct Commands { GLuint buffer = 0; U64 used = 0; };
std::map<std::vector<U32>, Commands> commands;
GLuint command_input = 0;
GLuint program = 0;
GLint position_location = -1, normal_location = -1, input_location = -1, output_location = -1;
bool failed = false;
GLint operation_location = -1, texture_location = -1, rotation_location = -1;
GLint scale_location = -1, uv_transform_location = -1, offset_location = -1;
GLint bump_s_location = -1, bump_t_location = -1, binormal_location = -1, flags_location = -1;

// Compute changes real GL state without changing the renderer's state caches.
struct Bindings
{
    GLint shader = 0, generic = 0, indexed[2] = {};
    GLint64 start[2] = {}, size[2] = {};
    Bindings()
    {
        glGetIntegerv(GL_CURRENT_PROGRAM, &shader);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &generic);
        for (U32 i = 0; i < 2; ++i)
        {
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, i, &indexed[i]);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START, i, &start[i]);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE, i, &size[i]);
        }
    }
    ~Bindings()
    {
        glUseProgram(shader);
        for (U32 i = 0; i < 2; ++i)
            if (indexed[i] && size[i]) glBindBufferRange(GL_SHADER_STORAGE_BUFFER, i, indexed[i], start[i], size[i]);
            else glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, indexed[i]);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, generic);
    }
};

std::shared_ptr<Source> acquire(const LLVolume& volume, S32 face)
{
    if (face < 0 || face >= volume.getNumVolumeFaces()) return {};
    const auto& geometry = volume.getVolumeFace(face);
    const Key key(&volume, volume.getGeometryRevision(), face, geometry.mWeightsScrubbed);
    if (auto found = sources.find(key); found != sources.end())
    {
        found->second->used = ++serial;
        return found->second;
    }
    const U64 bytes = U64(geometry.mNumVertices) * 72 + U64((geometry.mNumIndices+1)&~1) * 2;
    if (!bytes || bytes > CACHE_BYTES) return {};
    // Queued jobs retain their snapshots. Flush before eviction so the bounded
    // cache cannot turn into an unbounded collection of in-flight CPU owners.
    if (cached_bytes + bytes > CACHE_BYTES) LLMeshGeometry::flushRequired();
    while (cached_bytes + bytes > CACHE_BYTES && !sources.empty())
    {
        auto oldest = std::min_element(sources.begin(), sources.end(),
            [](const auto& a, const auto& b) { return a.second->used < b.second->used; });
        cached_bytes -= oldest->second->bytes;
        sources.erase(oldest);
    }
    auto source = std::make_shared<Source>();
    source->volume = const_cast<LLVolume*>(&volume);
    source->vertices = geometry.mNumVertices;
    source->bytes = bytes;
    source->used = ++serial;
    Bindings saved;
    glGenBuffers(1, &source->buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, source->buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_STATIC_DRAW);
    const U64 plane = U64(geometry.mNumVertices) * sizeof(LLVector4a);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, plane, geometry.mPositions);
    if (geometry.mNormals) glBufferSubData(GL_SHADER_STORAGE_BUFFER, plane, plane, geometry.mNormals);
    if (geometry.mTangents) glBufferSubData(GL_SHADER_STORAGE_BUFFER, 2 * plane, plane, geometry.mTangents);
    if (geometry.mTexCoords) glBufferSubData(GL_SHADER_STORAGE_BUFFER, 3 * plane, plane / 2, geometry.mTexCoords);
    if (geometry.mWeights) glBufferSubData(GL_SHADER_STORAGE_BUFFER, 3 * plane + plane / 2, plane, geometry.mWeights);
    if (geometry.mNumIndices) glBufferSubData(GL_SHADER_STORAGE_BUFFER, 4 * plane + plane / 2,
        geometry.mNumIndices * sizeof(U16), geometry.mIndices);
    if (!source->buffer || glGetError() != GL_NO_ERROR) return {};
    sources.emplace(key, source);
    cached_bytes += bytes;
    return source;
}
}

bool LLMeshGeometry::initGL()
{
    if (program) return true;
    if (failed || gGLManager.mGLVersion < 4.3f) return false;
    failed = true;
    std::ifstream file(gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS,
        "shaders", "class1/objects/meshGeometryC.glsl"));
    const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
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
        LL_WARNS("MeshGeometry") << log << LL_ENDL;
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
        LL_WARNS("MeshGeometry") << log << LL_ENDL;
        glDeleteProgram(linked);
        return false;
    }
    position_location = glGetUniformLocation(linked, "position_transform");
    normal_location = glGetUniformLocation(linked, "normal_transform");
    input_location = glGetUniformLocation(linked, "source_range");
    output_location = glGetUniformLocation(linked, "destination_range");
    if (position_location < 0 || normal_location < 0 || input_location < 0 || output_location < 0)
    {
        glDeleteProgram(linked);
        return false;
    }
    operation_location = glGetUniformLocation(linked, "operation");
    texture_location = glGetUniformLocation(linked, "texture_transform");
    rotation_location = glGetUniformLocation(linked, "bump_rotation");
    scale_location = glGetUniformLocation(linked, "uv_scale");
    uv_transform_location = glGetUniformLocation(linked, "uv_transform");
    offset_location = glGetUniformLocation(linked, "uv_offset");
    bump_s_location = glGetUniformLocation(linked, "bump_s");
    bump_t_location = glGetUniformLocation(linked, "bump_t");
    binormal_location = glGetUniformLocation(linked, "binormal_direction");
    flags_location = glGetUniformLocation(linked, "uv_flags");
    program = linked;
    failed = false;
    return true;
}

bool LLMeshGeometry::generate(LLVertexBuffer& destination, const LLVolume& volume, S32 face,
    U32 first, U32 count, U32 padded_count, U32 texture_index,
    const LLMatrix4a& position, const LLMatrix4a& normal, bool normals, bool tangents)
{
    if (!count) return true;
    if (face < 0 || face >= volume.getNumVolumeFaces() || padded_count < count ||
        U64(first) + padded_count > destination.getNumVerts() || !destination.getGLBuffer() ||
        !initGL()) return false;
    const auto& geometry = volume.getVolumeFace(face);
    if (count > U32(geometry.mNumVertices) || !geometry.mPositions ||
        (normals && !geometry.mNormals) || (tangents && !geometry.mTangents)) return false;
    if (jobs.size() >= MAX_JOBS) flushRequired();
    auto source = acquire(volume, face);
    if (!source) return false;
    Job job;
    job.destination = &destination;
    job.source = std::move(source);
    std::copy(position.getF32ptr(), position.getF32ptr() + 16, job.position.begin());
    std::copy(normal.getF32ptr(), normal.getF32ptr() + 16, job.normal.begin());
    job.input = {job.source->vertices, count, padded_count, texture_index};
    job.output = {destination.getOffset(LLVertexBuffer::TYPE_VERTEX) / 16 + first,
                  destination.getOffset(LLVertexBuffer::TYPE_NORMAL) / 16 + first,
                  destination.getOffset(LLVertexBuffer::TYPE_TANGENT) / 16 + first,
                  U32(normals) | (U32(tangents) << 1)};
    jobs.push_back(std::move(job));
    destination.setBeforeBind(flushRequired);
    return true;
}

void LLMeshGeometry::flushRequired()
{
    if (jobs.empty()) return;
    if (!initGL()) LL_ERRS("MeshGeometry") << "Required mesh geometry compute is unavailable" << LL_ENDL;
    gGL.flush();
    // All host writes must precede the producer, including uploads made after
    // queueing another face in the same destination buffer.
    for (auto& job : jobs) job.destination->unmapBuffer();
    Bindings saved;
    glUseProgram(program);
    for (const auto& job : jobs)
    {
        // Preserve queue order for overlapping replacement ranges and prior
        // copy consumers. No GPU buffer is read back to the CPU.
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, job.source ? job.source->buffer :
            job.copy_source ? job.copy_source->getGLIndices() : job.destination->getGLBuffer());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1,
            job.index_destination ? job.destination->getGLIndices() : job.destination->getGLBuffer());
        glUniform1ui(operation_location, job.operation);
        if (job.operation == 3)
        {
            glUniformMatrix4fv(texture_location, 1, GL_FALSE, job.uv.texture.getF32ptr());
            glUniformMatrix4fv(rotation_location, 1, GL_FALSE, job.uv.rotation.getF32ptr());
            glUniform4fv(scale_location, 1, job.uv.scale);
            glUniform4fv(uv_transform_location, 1, job.uv.transform);
            glUniform4fv(offset_location, 1, job.uv.offset);
            glUniform4fv(bump_s_location, 1, job.uv.bump_s);
            glUniform4fv(bump_t_location, 1, job.uv.bump_t);
            glUniform4fv(binormal_location, 1, job.uv.binormal);
            glUniform1ui(flags_location, job.uv.flags);
        }
        glUniformMatrix4fv(position_location, 1, GL_FALSE, job.position.data());
        glUniformMatrix4fv(normal_location, 1, GL_FALSE, job.normal.data());
        glUniform4uiv(input_location, 1, job.input.data());
        glUniform4uiv(output_location, 1, job.output.data());
        glDispatchCompute((job.input[2] + 63) / 64, 1, 1);
    }
    glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_ELEMENT_ARRAY_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    if (glGetError() != GL_NO_ERROR)
        LL_ERRS("MeshGeometry") << "Required mesh geometry dispatch failed" << LL_ENDL;
    for (auto& job : jobs) job.destination->setBeforeBind(nullptr);
    jobs.clear();
}

void LLMeshGeometry::destroyGL()
{
    // Shader reload retains ordinary vertex buffers. Finish their producers
    // before clearing callbacks; otherwise a queued static face can stay blank
    // indefinitely. Context-loss teardown discards the buffers instead.
    if (program && !gGLManager.mIsDisabled) flushRequired();
    for (auto& job : jobs) job.destination->setBeforeBind(nullptr);
    jobs.clear();
    sources.clear();
    cached_bytes = serial = 0;
    for (auto& entry : commands) glDeleteBuffers(1, &entry.second.buffer);
    commands.clear();
    if (command_input) glDeleteBuffers(1, &command_input);
    command_input = 0;
    if (program) glDeleteProgram(program);
    program = 0;
    failed = false;
}

namespace
{
void queue(Job job)
{
    if (jobs.size() >= MAX_JOBS) LLMeshGeometry::flushRequired();
    job.destination->setBeforeBind(LLMeshGeometry::flushRequired);
    jobs.push_back(std::move(job));
}
}

void LLMeshGeometry::fill(LLVertexBuffer& destination, U32 attribute, U32 first, U32 count, U32 value)
{
    if (!count) return;
    llassert(U64(first)+count<=destination.getNumVerts());
    Job job;
    job.destination=&destination;
    job.operation=1;
    job.input={0,count,count,value};
    job.output={destination.getOffset(static_cast<LLVertexBuffer::AttributeType>(attribute))/4+first,0,0,0};
    queue(std::move(job));
}

bool LLMeshGeometry::weights(LLVertexBuffer& destination, const LLVolume& volume, S32 face, U32 first, U32 count)
{
    if (!count) return true;
    if (face < 0 || face >= volume.getNumVolumeFaces() ||
        U64(first)+count>destination.getNumVerts() || !volume.getVolumeFace(face).mWeights) return false;
    Job job;
    job.source=acquire(volume,face);
    if (!job.source || count>job.source->vertices) return false;
    job.destination=&destination;
    job.operation=2;
    job.input={job.source->vertices,count,count,0};
    job.output={destination.getOffset(LLVertexBuffer::TYPE_WEIGHT4)/4+first*4,0,0,0};
    queue(std::move(job));
    return true;
}

bool LLMeshGeometry::indices(LLVertexBuffer& destination, const LLVolume& volume, S32 face,
    U32 first, U32 count, U32 base_vertex)
{
    if (!count) return true;
    if (face < 0 || face >= volume.getNumVolumeFaces()) return false;
    const auto& geometry=volume.getVolumeFace(face);
    if (U64(first)+count>destination.getNumIndices() || count>U32(geometry.mNumIndices)) return false;
    Job job;
    job.source=acquire(volume,face);
    if (!job.source) return false;
    job.destination=&destination;
    job.operation=4;
    job.index_destination=true;
    job.input={job.source->vertices*36,count,count,base_vertex};
    job.output={first,0,0,0};
    queue(std::move(job));
    return true;
}

bool LLMeshGeometry::texcoords(LLVertexBuffer& destination, const LLVolume& volume, S32 face,
    U32 attribute, U32 first, U32 count, const Texcoords& parameters)
{
    if (!count) return true;
    if (U64(first)+count>destination.getNumVerts()) return false;
    Job job;
    job.source=acquire(volume,face);
    if (!job.source || count>job.source->vertices) return false;
    job.destination=&destination;
    job.operation=3;
    job.uv=parameters;
    std::copy(parameters.normal.getF32ptr(),parameters.normal.getF32ptr()+16,job.normal.begin());
    job.input={job.source->vertices,count,count,0};
    job.output={destination.getOffset(static_cast<LLVertexBuffer::AttributeType>(attribute))/4+first*2,0,0,0};
    queue(std::move(job));
    return true;
}

void LLMeshGeometry::copyResidentRange(LLVertexBuffer& destination, LLVertexBuffer& source,
    U32 source_vertex, U32 source_index, U32 vertices, U32 count, U32 target_vertex, U32 target_index)
{
    flushRequired();
    source.unmapBuffer();
    destination.unmapBuffer();
    llassert(destination.getTypeMask()==source.getTypeMask());
    llassert(U64(source_vertex)+vertices<=source.getNumVerts());
    llassert(U64(target_vertex)+vertices<=destination.getNumVerts());
    llassert(U64(source_index)+count<=source.getNumIndices());
    llassert(U64(target_index)+count<=destination.getNumIndices());
    GLint read=0,write=0;
    glGetIntegerv(GL_COPY_READ_BUFFER_BINDING,&read);
    glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING,&write);
    glBindBuffer(GL_COPY_READ_BUFFER,source.getGLBuffer());
    glBindBuffer(GL_COPY_WRITE_BUFFER,destination.getGLBuffer());
    for (U32 type=0;type<LLVertexBuffer::TYPE_TEXTURE_INDEX;++type)
    {
        const auto attribute=static_cast<LLVertexBuffer::AttributeType>(type);
        if (!source.hasDataType(attribute)) continue;
        const U32 stride=LLVertexBuffer::sTypeSize[type];
        glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,
            source.getOffset(attribute)+source_vertex*stride,
            destination.getOffset(attribute)+target_vertex*stride,vertices*stride);
    }
    glBindBuffer(GL_COPY_READ_BUFFER,read);
    glBindBuffer(GL_COPY_WRITE_BUFFER,write);
    if (count)
    {
        Job job;
        job.destination=&destination;
        job.copy_source=&source;
        job.operation=4;
        job.index_destination=true;
        job.input={source_index,count,count,target_vertex-source_vertex};
        job.output={target_index,0,0,0};
        queue(std::move(job));
    }
}


bool LLMeshGeometry::compatibleBatch(const LLDrawInfo& a, const LLDrawInfo& b)
{
    return a.mMeshGeometry && b.mMeshGeometry && a.mVertexBuffer && a.mCount && b.mCount &&
        a.mVertexBuffer == b.mVertexBuffer &&
        (!a.mComputeLOD || !a.mComputeLOD->valid) && (!b.mComputeLOD || !b.mComputeLOD->valid) &&
        !a.mComputeBatch && !b.mComputeBatch && compatibleState(a, b);
}

bool LLMeshGeometry::compatibleState(const LLDrawInfo& a, const LLDrawInfo& b)
{
    // Program selection and ordering belong to the caller. These values must
    // match for every draw sharing the caller's GL state and skin palette.
    return
        a.mModelMatrix == b.mModelMatrix && a.mTextureMatrix == b.mTextureMatrix &&
        a.mTexture == b.mTexture && a.mTextureList == b.mTextureList &&
        a.mNormalMap == b.mNormalMap && a.mSpecularMap == b.mSpecularMap &&
        a.mNormalMapMatrix == b.mNormalMapMatrix && a.mSpecularMapMatrix == b.mSpecularMapMatrix &&
        a.mGLTFMaterial == b.mGLTFMaterial && a.mMaterial == b.mMaterial && a.mMaterialID == b.mMaterialID &&
        a.mAvatar == b.mAvatar && a.mSkinInfo == b.mSkinInfo && a.mShaderMask == b.mShaderMask &&
        a.mSpecColor == b.mSpecColor && a.mEnvIntensity == b.mEnvIntensity &&
        a.mAlphaMaskCutoff == b.mAlphaMaskCutoff && a.mDiffuseAlphaMode == b.mDiffuseAlphaMode &&
        a.mBlendFuncSrc == b.mBlendFuncSrc && a.mBlendFuncDst == b.mBlendFuncDst &&
        a.mBump == b.mBump && a.mShiny == b.mShiny && a.mFullbright == b.mFullbright && a.mHasGlow == b.mHasGlow;
}

bool LLMeshGeometry::drawBatch(const std::vector<LLDrawInfo*>& batch)
{
    if (batch.size() < 2 || batch.size() > 256 || !batch.front() || !initGL()) return false;
    const auto& first = *batch.front();
    std::vector<U32> ranges;
    ranges.reserve(batch.size()*2);
    for (const auto* draw : batch)
    {
        if (!draw || !compatibleBatch(first,*draw)) return false;
        ranges.push_back(draw->mCount);
        ranges.push_back(draw->mOffset);
    }
    auto found = commands.find(ranges);
    if (found == commands.end())
    {
        // At most 256 cached packets of 256 commands (1.25 MiB of commands).
        if (commands.size() >= 256)
        {
            auto oldest = std::min_element(commands.begin(),commands.end(),
                [](const auto& a,const auto& b){return a.second.used < b.second.used;});
            glDeleteBuffers(1,&oldest->second.buffer);
            commands.erase(oldest);
        }
        gGL.flush();
        Bindings saved;
        if (!command_input) glGenBuffers(1,&command_input);
        Commands packet;
        glGenBuffers(1,&packet.buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,command_input);
        glBufferData(GL_SHADER_STORAGE_BUFFER,ranges.size()*sizeof(U32),ranges.data(),GL_STREAM_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,packet.buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER,batch.size()*5*sizeof(U32),nullptr,GL_STATIC_DRAW);
        if (!command_input || !packet.buffer || glGetError()!=GL_NO_ERROR)
        {
            if (packet.buffer) glDeleteBuffers(1,&packet.buffer);
            return false;
        }
        glUseProgram(program);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,command_input);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,packet.buffer);
        glUniform1ui(operation_location,5);
        glUniform4ui(input_location,0,U32(batch.size()),U32(batch.size()),0);
        glDispatchCompute((U32(batch.size())+63)/64,1,1);
        glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
        if (glGetError()!=GL_NO_ERROR)
        {
            glDeleteBuffers(1,&packet.buffer);
            return false;
        }
        found=commands.emplace(std::move(ranges),packet).first;
    }
    found->second.used=++serial;
    GLint indirect=0;
    glGetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING,&indirect);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER,found->second.buffer);
    auto* buffer=batch.front()->mVertexBuffer.get();
    buffer->setBuffer();
    buffer->drawIndirect(LLRender::TRIANGLES,0,U32(batch.size()));
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER,indirect);
    return true;
}
