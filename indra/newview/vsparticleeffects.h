// Actual shared particle CPU owners with native residency/submission. LGPL-2.1.
#pragma once
#include "llpointer.h"
#include "vsworldrenderer.h"
#include "v3math.h"
#include <functional>
#include <memory>
#include <optional>
class LLViewerPart;
class LLViewerPartSource;
class LLViewerPartSourceBeam;
class LLColor4;
class LLColor4U;
class LLUUID;
class LLVector3d;
class LLViewerPartGroup;
class VSNativeSession;
class VSWorldScene;
class VSUIResources;
struct VSParticleGroupState;
class VSParticleEffects
{
public:
    explicit VSParticleEffects(std::weak_ptr<VSNativeSession>);
    ~VSParticleEffects();
    bool admit(LLViewerPart*);
    void release(const LLViewerPart*);
    bool source(LLPointer<LLViewerPartSource>);
    std::function<bool()> sourcePolicy(const LLUUID& source,const LLUUID& owner);
    bool positionSource(LLPointer<LLViewerPartSource>,std::function<std::optional<LLVector3>()>,const LLVector3& initial,std::function<bool()> allowed = {});
    std::shared_ptr<VSParticleGroupState> group(const LLVector3& center,F32 side,bool hud);
    LLPointer<LLViewerPartSourceBeam> beam(const LLUUID& source,const LLUUID& target,const LLVector3d&,const LLColor4&);
    bool pick(const LLVector3& start,const LLVector3& end,LLUUID& owner,LLUUID& source) const;
    void tick();
    void append(VSWorldRenderer&,VSUIResources&,VSWorldScene&,std::vector<VSWorldRenderer::Packet>&,bool normalView=true);
    void reset();
private:
    void reconcile();
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
bool vs_native_particle_peer();
bool vs_native_particle_admit(LLViewerPart*);
void vs_native_particle_release(const LLViewerPart*);
bool vs_native_particle_source(LLPointer<LLViewerPartSource>);
std::shared_ptr<VSParticleGroupState> vs_native_particle_group(const LLVector3&,F32,bool);
bool vs_native_particle_group_current(const std::shared_ptr<VSParticleGroupState>&);
LLVector3 vs_native_particle_wind(const std::shared_ptr<VSParticleGroupState>&,const LLVector3&);

void vs_native_beam_effect(const LLUUID& source,const LLUUID& target,const LLColor4U&,F32 duration);

bool vs_native_particle_group_visible(const std::shared_ptr<VSParticleGroupState>&);
