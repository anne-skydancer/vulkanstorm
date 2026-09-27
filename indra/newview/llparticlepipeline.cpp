// SPDX-License-Identifier: LGPL-2.1-or-later
#include "llviewerprecompiledheaders.h"
#include "llparticlepipeline.h"
#include "llgl.h"
#include "llrender.h"
#include "lldir.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace
{
using namespace LLParticlePipeline;
enum Kernel { SIMULATE, ORDER, SORT, RANGES, SUFFIX, LINKS, BIRTH, ALPHA_INTERVALS, GEOMETRY, PICK, BOUNDS, CULL, DEMAND, KERNEL_COUNT };
enum Buffer { PARTICLES, SOURCES, WIND, SPATIAL, DEPTH, SPATIAL_RANGES,
              MATERIAL_RANGES, SCRATCH_A, SCRATCH_B, BIRTHS, ALLOCATION, SOURCE_TAILS, ALPHA_BOUNDARIES, ALPHA_RANGES, DRAW_COMMANDS, VERTICES, PICK_RESULT, REGIONS, BOUNDS_DATA, TEXTURE_DEMAND, BUFFER_COUNT };
constexpr const char* FILES[KERNEL_COUNT] = {
    "particleSimulationC.glsl", "particleOrderC.glsl", "particleSortC.glsl",
    "particleRangesC.glsl", "particleSuffixC.glsl", "particleRibbonLinksC.glsl",
    "particleBirthC.glsl", "particleAlphaIntervalsC.glsl",
    "particleResidentGeometryC.glsl", "particlePickC.glsl", "particleBoundsC.glsl", "particleCullC.glsl", "particleDemandC.glsl"
};
constexpr size_t MAX_WIND_VELOCITIES = 256 * 1024;
struct Program
{
    GLuint id = 0;
    GLint particleCount = -1, sourceCount = -1, deltaTime = -1, originShift = -1;
    GLint paddedCount = -1, cameraPosition = -1, cameraForward = -1, cellSize = -1;
    GLint sortWidth = -1, sortStride = -1, rangeMode = -1, scanStride = -1, linkMode = -1;
    GLint birthCount = -1, maxLiveCount = -1, birthMode = -1;
    GLint boundaryCount = -1, domain = -1;
    GLint rayStart = -1, rayDirection = -1, pickMode = -1;
    GLint regionCount = -1, killRegion = -1, regionLower = -1, regionUpper = -1;
    GLint viewProjection = -1, viewDomain = -1, cullEnabled = -1, pixelMeterRatio = -1;
} programs[KERNEL_COUNT];
GLuint buffers[BUFFER_COUNT] = {};
GLuint drawVAO = 0;
U32 capacity = 0, sourceCount = 0, intervalCount = 0, regionCount = 0;
std::vector<std::array<U32, 2>> sourceControls;
bool failed = false, sourcesPublished = false, viewReady = false, hasRibbons = false;

// Preserve base and range bindings as well as generic binding. Raw programs are
// temporary, leaving LLGLSLShader's cached program unchanged when we return.
struct Bindings
{
    GLint program = 0, generic = 0, indexed[4] = {};
    GLint64 start[4] = {}, size[4] = {};
    Bindings()
    {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &generic);
        for (U32 i = 0; i < 4; ++i)
        {
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, i, &indexed[i]);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START, i, &start[i]);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE, i, &size[i]);
        }
    }
    ~Bindings()
    {
        glUseProgram(program);
        for (U32 i = 0; i < 4; ++i)
        {
            if (indexed[i] && size[i])
                glBindBufferRange(GL_SHADER_STORAGE_BUFFER, i, indexed[i], start[i], size[i]);
            else
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, indexed[i]);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, generic);
    }
};

bool checkErrors(const char* operation)
{
    GLenum error = glGetError();
    if (error == GL_NO_ERROR) return true;
    failed = true;
    viewReady = false;
    intervalCount = 0;
    LL_WARNS("ParticlePipeline") << operation << " failed with GL error " << error << LL_ENDL;
    return false;
}
bool compile(Program& p, const char* name)
{
    std::ifstream file(gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS,
        "shaders", std::string("class1/objects/") + name));
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (text.empty())
    {
        LL_WARNS("ParticlePipeline") << "Missing required shader " << name << LL_ENDL;
        return false;
    }
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char* source = text.c_str();
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    char log[4096] = {};
    if (!ok)
    {
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LL_WARNS("ParticlePipeline") << name << ": " << log << LL_ENDL;
        glDeleteShader(shader);
        return false;
    }
    p.id = glCreateProgram();
    glAttachShader(p.id, shader);
    glLinkProgram(p.id);
    glDeleteShader(shader);
    glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        glGetProgramInfoLog(p.id, sizeof(log), nullptr, log);
        LL_WARNS("ParticlePipeline") << name << ": " << log << LL_ENDL;
        glDeleteProgram(p.id);
        p.id = 0;
        return false;
    }
#define LOCATION(field) p.field = glGetUniformLocation(p.id, #field)
    LOCATION(particleCount); LOCATION(sourceCount); LOCATION(deltaTime); LOCATION(originShift);
    LOCATION(paddedCount); LOCATION(cameraPosition); LOCATION(cameraForward); LOCATION(cellSize);
    LOCATION(boundaryCount); LOCATION(domain);
    LOCATION(rayStart); LOCATION(rayDirection); LOCATION(pickMode);
    LOCATION(regionCount); LOCATION(killRegion); LOCATION(regionLower); LOCATION(regionUpper);
    LOCATION(viewProjection); LOCATION(viewDomain); LOCATION(cullEnabled); LOCATION(pixelMeterRatio);
    LOCATION(birthCount); LOCATION(maxLiveCount); LOCATION(birthMode);
    LOCATION(sortWidth); LOCATION(sortStride); LOCATION(rangeMode); LOCATION(scanStride); LOCATION(linkMode);
#undef LOCATION
    return true;
}
void bind(U32 index, Buffer buffer)
{
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, index, buffers[buffer]);
}
void allocate(Buffer buffer, size_t bytes, const void* data)
{
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[buffer]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, data, GL_DYNAMIC_DRAW);
}
void dispatch()
{
    glDispatchCompute((capacity + 63) / 64, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
Program& use(Kernel kernel)
{
    Program& p = programs[kernel];
    glUseProgram(p.id);
    if (p.particleCount >= 0) glUniform1ui(p.particleCount, capacity);
    if (p.paddedCount >= 0) glUniform1ui(p.paddedCount, capacity);
    return p;
}
void sort(Buffer buffer)
{
    bind(0, buffer);
    Program& p = use(SORT);
    for (U32 width = 2; width <= capacity; width *= 2)
    {
        glUniform1ui(p.sortWidth, width);
        for (U32 stride = width / 2; stride; stride /= 2)
        {
            glUniform1ui(p.sortStride, stride);
            dispatch();
        }
    }
}
bool finite(const std::array<float, 3>& v)
{
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}
void releaseBuffers()
{
    if (drawVAO) glDeleteVertexArrays(1, &drawVAO);
    drawVAO = 0;
    glDeleteBuffers(BUFFER_COUNT, buffers);
    std::fill(std::begin(buffers), std::end(buffers), 0);
    capacity = sourceCount = intervalCount = regionCount = 0;
    sourceControls.clear();
    sourcesPublished = viewReady = hasRibbons = false;
}
}

bool LLParticlePipeline::isSupported()
{
    return gGLManager.mGLVersion >= 4.49f &&
        (gGLManager.mGLSLVersionMajor > 4 ||
         (gGLManager.mGLSLVersionMajor == 4 && gGLManager.mGLSLVersionMinor >= 50)) &&
        gGLManager.mHasFragmentShaderInterlock && gGLManager.mHasBindlessTexture &&
        gGLManager.mHasShaderDrawParameters && gGLManager.mHasIndirectParameters;
}
bool LLParticlePipeline::initGL()
{
    if (failed || !isSupported()) return false;
    if (programs[SIMULATE].id) return true;
    for (U32 i = 0; i < KERNEL_COUNT; ++i)
    {
        if (!compile(programs[i], FILES[i]))
        {
            unloadShaders();
            failed = true;
            return false;
        }
    }
    if (!checkErrors("shader initialization"))
    {
        unloadShaders();
        failed = true;
        return false;
    }
    LL_INFOS("ParticlePipeline") << "Resident particle pipeline shaders initialized" << LL_ENDL;
    return true;
}
void LLParticlePipeline::unloadShaders()
{
    for (auto& program : programs)
    {
        if (program.id) glDeleteProgram(program.id);
        program = Program{};
    }
    // Never clear an allocation/dispatch failure while resident state may be bad.
    // Shader-only failure can be retried after a full destroyGL.
}
void LLParticlePipeline::destroyGL()
{
    unloadShaders();
    releaseBuffers();
    failed = false;
}
bool LLParticlePipeline::initializePool(const std::vector<Particle>& initialSlots)
{
    if (capacity || initialSlots.size() > MAX_CAPACITY || !initGL()) return false;
    gGL.flush();
    Bindings saved;
    U32 size = 1;
    while (size < initialSlots.size()) size *= 2;
    std::vector<Particle> slots(size);
    std::copy(initialSlots.begin(), initialSlots.end(), slots.begin());
    glGenBuffers(BUFFER_COUNT, buffers);
    glGenVertexArrays(1, &drawVAO);
    allocate(PARTICLES, slots.size() * sizeof(Particle), slots.data());
    // Legal nonzero storage even when there are no sources/wind fields.
    Source emptySource{};
    WindVelocity emptyWind{};
    allocate(SOURCES, sizeof(emptySource), &emptySource);
    allocate(WIND, sizeof(emptyWind), &emptyWind);
    for (Buffer buffer : {SPATIAL, DEPTH})
        allocate(buffer, size * sizeof(Entry), nullptr);
    for (Buffer buffer : {SPATIAL_RANGES, MATERIAL_RANGES, SCRATCH_A, SCRATCH_B})
        allocate(buffer, size * sizeof(std::uint32_t) * 4, nullptr);
    allocate(BIRTHS, sizeof(Birth), nullptr);
    allocate(ALLOCATION, (4 + 2 * ((size + 63) / 64) + size) * sizeof(U32), nullptr);
    std::vector<std::array<U32, 4>> tails(MAX_CAPACITY, {0xffffffffu, 0u, 0u, 0u});
    allocate(SOURCE_TAILS, tails.size() * sizeof(tails[0]), tails.data());
    allocate(VERTICES, size * 4 * sizeof(Vertex), nullptr);
    allocate(PICK_RESULT, 2 * sizeof(U32), nullptr);
    allocate(REGIONS, sizeof(LLParticlePipeline::Region), nullptr);
    allocate(BOUNDS_DATA, size*8*sizeof(float), nullptr);
    std::vector<U32> demand(MAX_CAPACITY,0);
    allocate(TEXTURE_DEMAND,demand.size()*sizeof(U32),demand.data());
    allocate(ALPHA_BOUNDARIES, sizeof(float), nullptr);
    allocate(ALPHA_RANGES, 4 * sizeof(U32), nullptr);
    // Every draw is one six-vertex quad. Its dynamically uniform DrawID selects
    // the sorted entry through the interval table. No GPU-to-host offsets/counts.
    std::vector<std::array<U32, 4>> commands(size*2, {6u, 1u, 0u, 0u});
    allocate(DRAW_COMMANDS, commands.size() * sizeof(commands[0]), commands.data());
    if (!checkErrors("pool allocation"))
    {
        releaseBuffers();
        return false;
    }
    capacity = size;
    hasRibbons = std::any_of(slots.begin(), slots.end(), [](const Particle& p) {
        return p.identity[1] && (p.identity[2] & 0x400u);
    });
    return true;
}
bool LLParticlePipeline::publishSources(const std::vector<Source>& sources,
                                       const std::vector<WindVelocity>& wind)
{
    if (!capacity || failed || sources.size() > MAX_CAPACITY ||
        wind.size() > MAX_WIND_VELOCITIES) return false;
    // Invalid wind data must not silently turn a requested simulation feature off.
    for (const Source& s : sources)
    {
        if (s.wind[1] && (s.wind[0] > wind.size() || wind.size() - s.wind[0] < 256 ||
            !std::isfinite(s.regionOriginWidth[3]) || s.regionOriginWidth[3] <= 0.f))
            return false;
    }
    gGL.flush();
    Bindings saved;
    Source emptySource{};
    WindVelocity emptyWind{};
    allocate(SOURCES, sources.empty() ? sizeof(Source) : sources.size() * sizeof(Source),
             sources.empty() ? &emptySource : sources.data());
    allocate(WIND, wind.empty() ? sizeof(WindVelocity) : wind.size() * sizeof(WindVelocity),
             wind.empty() ? &emptyWind : wind.data());
    if (!checkErrors("source publication")) return false;
    sourceCount = static_cast<U32>(sources.size());
    sourceControls.resize(sources.size());
    for (size_t i = 0; i < sources.size(); ++i)
        sourceControls[i] = {sources[i].control[0], sources[i].control[1]};
    sourcesPublished = true;
    return true;
}
bool LLParticlePipeline::emit(const std::vector<Particle>& births, U32 maxLiveCount)
{
    if (!capacity || !sourcesPublished || failed || births.size() > MAX_CAPACITY ||
        maxLiveCount > capacity || !initGL()) return false;
    if (births.empty()) return true;
    constexpr U32 invalid = 0xffffffffu;
    std::vector<Birth> commands(births.size());
    std::vector<U32> previous(sourceCount, invalid);
    bool ribbons = false;
    for (U32 i = 0; i < births.size(); ++i)
    {
        const Particle& p = births[i];
        const U32 source = p.sourceParent[0];
        if (source >= sourceCount || sourceControls[source][0] != p.sourceParent[1] ||
            sourceControls[source][1] || !std::isfinite(p.velocityLife[3]) ||
            p.velocityLife[3] <= 0.f || !std::isfinite(p.positionAge[3]) ||
            p.positionAge[3] < 0.f || p.positionAge[3] > p.velocityLife[3]) return false;
        commands[i].particle = p;
        commands[i].chain[0] = previous[source];
        commands[i].chain[1] = invalid;
        if (previous[source] != invalid) commands[previous[source]].chain[1] = i;
        previous[source] = i;
        ribbons |= (p.identity[2] & 0x400u) != 0;
    }
    gGL.flush();
    Bindings saved;
    viewReady = false;
    intervalCount = 0;
    allocate(BIRTHS, commands.size() * sizeof(Birth), commands.data());
    bind(0, PARTICLES); bind(1, BIRTHS); bind(2, ALLOCATION); bind(3, SOURCE_TAILS);
    Program& p = use(BIRTH);
    glUniform1ui(p.birthCount, static_cast<U32>(births.size()));
    glUniform1ui(p.maxLiveCount, maxLiveCount);
    for (U32 mode = 0; mode < 6; ++mode)
    {
        glUniform1ui(p.birthMode, mode);
        if (mode == 1)
        {
            glDispatchCompute(1, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
        else dispatch();
    }
    hasRibbons |= ribbons;
    return checkErrors("birth allocation and ribbon linking");
}
bool LLParticlePipeline::advance(float dt, const std::array<float, 3>& originShift)
{
    if (!capacity || !sourcesPublished || failed || !std::isfinite(dt) ||
        dt < 0.f || !finite(originShift) || !initGL()) return false;
    gGL.flush();
    Bindings saved;
    viewReady = false;
    intervalCount = 0;
    bind(0, PARTICLES); bind(1, SOURCES); bind(2, WIND); bind(3, REGIONS);
    Program& p = use(SIMULATE);
    glUniform1ui(p.killRegion, 0);
    glUniform1ui(p.regionCount, regionCount);
    glUniform1ui(p.sourceCount, sourceCount);
    glUniform1f(p.deltaTime, dt);
    glUniform3fv(p.originShift, 1, originShift.data());
    dispatch();
    if (hasRibbons)
    {
        Buffer previous = SCRATCH_A, next = SCRATCH_B;
        Program& links = use(LINKS);
        bind(0, PARTICLES); bind(1, previous); bind(2, next);
        glUniform1ui(links.linkMode, 0);
        dispatch();
        std::swap(previous, next);
        for (U32 distance = 1; distance < capacity; distance *= 2)
        {
            bind(1, previous); bind(2, next);
            glUniform1ui(links.linkMode, 1);
            dispatch();
            std::swap(previous, next);
        }
        bind(1, previous); bind(2, next);
        glUniform1ui(links.linkMode, 2);
        dispatch();
        // Commit repaired handles before scratch reuse or future slot recycling.
        bind(1, next); bind(2, previous);
        glUniform1ui(links.linkMode, 3);
        dispatch();
    }
    return checkErrors("simulation and ribbon repair");
}
bool LLParticlePipeline::prepareView(const std::array<float, 3>& cameraPosition,
                                    const std::array<float, 3>& cameraForward, float cellSize,
                                    const float* viewProjection, U32 viewDomain)
{
    if (!capacity || failed || !finite(cameraPosition) || !finite(cameraForward) ||
        !std::isfinite(cellSize) || cellSize <= 0.f || !initGL()) return false;
    if (cameraForward[0] == 0.f && cameraForward[1] == 0.f && cameraForward[2] == 0.f)
        return false;
    gGL.flush();
    Bindings saved;
    viewReady = false;
    intervalCount = 0;
    bind(0, PARTICLES); bind(1, SOURCES); bind(2, VERTICES);
    Program& geometry = use(GEOMETRY);
    glUniform1ui(geometry.sourceCount, sourceCount);
    glUniform3fv(geometry.cameraPosition, 1, cameraPosition.data());
    glUniform3fv(geometry.cameraForward, 1, cameraForward.data());
    dispatch();
    bind(0, PARTICLES); bind(1, SPATIAL); bind(2, DEPTH);
    Program& order = use(ORDER);
    glUniform3fv(order.cameraPosition, 1, cameraPosition.data());
    glUniform3fv(order.cameraForward, 1, cameraForward.data());
    glUniform1f(order.cellSize, cellSize);
    dispatch();
    sort(SPATIAL);
    bind(0, SPATIAL); bind(1, SPATIAL_RANGES); bind(2, SCRATCH_A);
    Program& ranges = use(RANGES);
    glUniform1ui(ranges.rangeMode, 0);
    dispatch();
    bind(0, SPATIAL); bind(1, SPATIAL_RANGES); bind(2, VERTICES); bind(3, BOUNDS_DATA);
    Program& bounds = use(BOUNDS);
    glUniform1ui(bounds.cullEnabled, viewProjection ? 1 : 0);
    glUniform1ui(bounds.viewDomain, viewDomain);
    if (viewProjection) glUniformMatrix4fv(bounds.viewProjection, 1, GL_FALSE, viewProjection);
    dispatch();
    bind(0, DEPTH); bind(1, BOUNDS_DATA);
    use(CULL); dispatch();
    sort(DEPTH);
    use(RANGES);
    bind(0, DEPTH); bind(1, SCRATCH_A); bind(2, SCRATCH_B);
    glUniform1ui(ranges.rangeMode, 1);
    dispatch();
    Buffer previous = SCRATCH_A, next = SCRATCH_B;
    Program& suffix = use(SUFFIX);
    for (U32 stride = 1; stride < capacity; stride *= 2)
    {
        bind(0, previous); bind(1, next);
        glUniform1ui(suffix.scanStride, stride);
        dispatch();
        std::swap(previous, next);
    }
    use(RANGES);
    bind(0, DEPTH); bind(1, MATERIAL_RANGES); bind(2, previous);
    glUniform1ui(ranges.rangeMode, 2);
    dispatch();
    viewReady = checkErrors("view ordering and ranges");
    return viewReady;
}
LLParticlePipeline::ViewBuffers LLParticlePipeline::viewBuffers()
{
    if (!viewReady || failed) return {};
    return {buffers[PARTICLES], buffers[SPATIAL], buffers[DEPTH],
            buffers[SPATIAL_RANGES], buffers[MATERIAL_RANGES], capacity};
}


bool LLParticlePipeline::prepareAlphaIntervals(const std::vector<float>& groupDepths, U32 domain)
{
    intervalCount = 0;
    if (!viewReady || failed || domain > 1 || groupDepths.size() > 65536 || !initGL()) return false;
    for (size_t i = 0; i < groupDepths.size(); ++i)
        if (!std::isfinite(groupDepths[i]) || (i && groupDepths[i] > groupDepths[i-1])) return false;
    gGL.flush();
    Bindings saved;
    float empty = 0.f;
    allocate(ALPHA_BOUNDARIES, groupDepths.empty() ? sizeof(float) : groupDepths.size()*sizeof(float),
             groupDepths.empty() ? &empty : groupDepths.data());
    const U32 count = static_cast<U32>(groupDepths.size()) + 1;
    allocate(ALPHA_RANGES, count * 4 * sizeof(U32), nullptr);
    bind(0, DEPTH); bind(1, ALPHA_BOUNDARIES); bind(2, ALPHA_RANGES);
    Program& p = use(ALPHA_INTERVALS);
    glUniform1ui(p.boundaryCount, count-1);
    glUniform1ui(p.domain, domain);
    glDispatchCompute((count+63)/64, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
    if (!checkErrors("alpha intervals")) return false;
    intervalCount = count;
    return true;
}
LLParticlePipeline::AlphaBuffers LLParticlePipeline::alphaBuffers()
{
    if (!viewReady || failed || !intervalCount) return {};
    return {buffers[VERTICES], buffers[DEPTH], buffers[ALPHA_RANGES], buffers[DRAW_COMMANDS], intervalCount};
}

bool LLParticlePipeline::drawAlphaInterval(U32 interval, U32 materials, std::int32_t intervalUniform, bool interleaveGlow)
{
    if (!viewReady || failed || interval >= intervalCount || !materials || intervalUniform < 0)
        return false;
    gGL.flush();
    Bindings saved;
    GLint vao = 0, indirect = 0, parameters = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING, &indirect);
    glGetIntegerv(GL_PARAMETER_BUFFER_BINDING_ARB, &parameters);
    glBindVertexArray(drawVAO);
    bind(0, VERTICES); bind(1, DEPTH); bind(2, ALPHA_RANGES);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, materials);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buffers[DRAW_COMMANDS]);
    glBindBuffer(GL_PARAMETER_BUFFER_ARB, buffers[ALPHA_RANGES]);
    glUniform1i(intervalUniform, interval);
    glMultiDrawArraysIndirectCountARB(GL_TRIANGLES, nullptr,
        static_cast<GLintptr>(interval) * 4 * sizeof(U32) + (interleaveGlow?3:1)*sizeof(U32),
        capacity*(interleaveGlow?2:1), 4 * sizeof(U32));
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
    glBindVertexArray(vao);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect);
    glBindBuffer(GL_PARAMETER_BUFFER_ARB, parameters);
    return checkErrors("ordered alpha draw");
}

// Picking is explicitly on demand; only the winning handle is downloaded.
bool LLParticlePipeline::pick(const std::array<float,3>& start, const std::array<float,3>& end,
                              std::array<U32,2>& source, float& fraction)
{
    if (!viewReady || failed || !finite(start) || !finite(end)) return false;
    gGL.flush(); Bindings saved;
    const U32 empty[] = {0x7f800000u, 0xffffffffu};
    allocate(PICK_RESULT, sizeof(empty), empty);
    bind(0, PARTICLES); bind(1, VERTICES); bind(2, PICK_RESULT);
    Program& p = use(PICK);
    std::array<float,3> direction{end[0]-start[0],end[1]-start[1],end[2]-start[2]};
    glUniform3fv(p.rayStart, 1, start.data());
    glUniform3fv(p.rayDirection, 1, direction.data());
    for (U32 mode = 0; mode < 2; ++mode) { glUniform1ui(p.pickMode, mode); dispatch(); }
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    U32 result[2]; glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[PICK_RESULT]);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(result), result);
    if (result[1] >= capacity) return false;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[PARTICLES]);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, result[1]*sizeof(Particle)+offsetof(Particle,sourceParent),
        sizeof(source), source.data());
    std::memcpy(&fraction, &result[0], sizeof(float));
    return checkErrors("particle picking");
}

bool LLParticlePipeline::publishRegions(const std::vector<LLParticlePipeline::Region>& regions)
{
    if (!capacity || failed || regions.size()>1024) return false;
    gGL.flush(); Bindings saved;
    LLParticlePipeline::Region empty{};
    allocate(REGIONS, std::max<size_t>(sizeof(LLParticlePipeline::Region),regions.size()*sizeof(LLParticlePipeline::Region)),regions.empty()?&empty:regions.data());
    regionCount=U32(regions.size());
    return checkErrors("region publication");
}
bool LLParticlePipeline::retireRegion(const std::array<float,3>& lower,const std::array<float,3>& upper)
{
    if (!capacity || failed || !initGL()) return false;
    gGL.flush(); Bindings saved;
    bind(0,PARTICLES);
    Program& p=use(SIMULATE);
    glUniform1ui(p.killRegion,1);
    glUniform3fv(p.regionLower,1,lower.data()); glUniform3fv(p.regionUpper,1,upper.data());
    dispatch(); glUniform1ui(p.killRegion,0);
    viewReady=false; intervalCount=0;
    return checkErrors("region retirement");
}

U32 LLParticlePipeline::boundsBuffer() { return viewReady && !failed ? buffers[BOUNDS_DATA] : 0; }

U32 LLParticlePipeline::textureDemand(float ratio,const std::array<float,3>& camera)
{
    if (!viewReady || failed || !std::isfinite(ratio)) return 0;
    gGL.flush(); Bindings saved;
    bind(0,PARTICLES); bind(1,BOUNDS_DATA); bind(2,TEXTURE_DEMAND);
    Program& p=use(DEMAND);
    glUniform1f(p.pixelMeterRatio,ratio);
    glUniform3fv(p.cameraPosition,1,camera.data());
    dispatch();
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    return checkErrors("texture demand") ? buffers[TEXTURE_DEMAND] : 0;
}
