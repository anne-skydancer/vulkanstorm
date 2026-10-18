// Shared LLDebugBeacon data + original CPU geometry; no legacy scene owner.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsbeaconpresentation.h"
#include "vsdebugbeacongeometry.h"
#include "vsworldscene.h"
#include "vsnativesession.h"
#include "llviewerobjectlist.h"
#include "llfloatertelehub.h"
#include "llenvironment.h"
#include "llsettingssky.h"
#include "llhudtext.h"
#include "pipeline.h"
#include "llviewercontrol.h"
#include <array>
#include <optional>
struct VSBeaconPresentation::Impl
{
    VSWorldRenderer& renderer;
    VSWorldScene& world;
    struct Cache{LLVector3 position;VSWorldRenderer::Mesh farPass,nearPass;LLPointer<LLHUDText> label;std::string labelText;};
    struct SunCache{LLVector3 position,direction;VSWorldRenderer::Mesh mesh;};
    std::array<SunCache,2> sky;
    void retireLabels(){for(auto& entry:cache)if(entry.label)entry.label->markDead();}
    std::vector<Cache> cache;
    VSNativeSession::AccountHandle account;
    Impl(VSWorldRenderer&r,VSWorldScene&w):renderer(r),world(w){}
    struct Sink
    {
        std::vector<VSWorldRenderer::Vertex> vertices;
        std::vector<unsigned> indices;
        std::optional<std::array<float,3>> first;
        void vertex3fv(const float* p){vertex3f(p[0],p[1],p[2]);}
        void vertex3f(float x,float y,float z)
        {
            const std::array<float,3> point{x,y,z};
            if(!first){first=point;return;}
            constexpr float endpoint[]={0,1,0,0,1,1},side[]={-1,-1,1,1,-1,1};
            for(unsigned i=0;i<6;++i)
            {
                indices.push_back(unsigned(vertices.size()));
                vertices.push_back({(*first)[0],(*first)[1],(*first)[2],point[0],point[1],point[2],endpoint[i],side[i]});
            }
            first.reset();
        }
    };
    static std::array<float,4> vertexColor(LLColor4 color)
    {
        std::array<float,4> result;
        // Actual LLRender::color4f clamps then truncates each component to U8.
        for(unsigned c=0;c<4;++c)result[c]=float(static_cast<U8>(llclamp(color.mV[c],0.f,1.f)*255.f))/255.f;
        return result;
    }
    void label(Cache& entry,const LLDebugBeacon& source)
    {
        if(source.mString.empty())
        {
            if(entry.label){entry.label->markDead();}
            entry.label=nullptr;entry.labelText.clear();return;
        }
        if(!entry.label)
        {
            entry.label=static_cast<LLHUDText*>(LLHUDObject::addHUDObject(LLHUDObject::LL_HUD_TEXT));
            entry.label->setZCompare(false);
        }
        if(entry.labelText!=source.mString){entry.label->setString(source.mString);entry.labelText=source.mString;}
        entry.label->setColor(source.mTextColor);entry.label->setPositionAgent(source.mPositionAgent);
    }
    void sunMoon(std::vector<VSWorldRenderer::Packet>& packets)
    {
        const auto settings=LLEnvironment::instance().getCurrentSky();LLVector3 position;
        if(!settings || !world.agentPosition(position))return;
        for(unsigned slot=0;slot<2;++slot)
        {
            if(!gSavedSettings.getBOOL(slot?"moonbeacon":"sunbeacon"))continue;
            const auto direction=slot?settings->getMoonDirection():settings->getSunDirection();auto& accepted=sky[slot];
            if(!accepted.mesh || accepted.position!=position || accepted.direction!=direction)
            {
                // Two crosses plus their connecting line, exactly seven lines.
                const auto residency=world.reserveSpatialBytes(42*(sizeof(VSWorldRenderer::Vertex)+sizeof(unsigned)));
                if(!residency)continue;
                Sink sink;sink.vertices.reserve(42);sink.indices.reserve(42);vs_sun_moon_beacon(sink,position,direction);
                VSWorldRenderer::Mesh next;
                if(!renderer.publishMesh(next,sink.vertices,sink.indices))continue;
                accepted={position,direction,std::move(next)};
            }
            LLColor4 color=slot?LLColor4(1.f,0.f,.8f,.5f):LLColor4(1.f,.5f,0.f,.5f);color.mV[3]*=.5f;
            VSWorldRenderer::Packet packet;packet.mesh=accepted.mesh;packet.color=vertexColor(color);
            packet.fullbright=true;packet.alpha=true;packet.overlay=true;packet.depthTest=false;packet.depthWriteAlpha=false;
            packet.screenLinePixels=F32(LLPipeline::DebugBeaconLineWidth);packets.push_back(std::move(packet));
        }
    }
    bool publish(Cache& result,const LLVector3& position)
    {
        if(result.farPass && result.nearPass && result.position==position)return true;
        // The reference emits three cross lines and twelve cube edges per pass.
        // Reserve temporary CPU arrays before allocating; publishMesh owns the
        // retained CPU/GPU generations under the existing geometry budget.
        const auto residency=world.reserveSpatialBytes(180*(sizeof(VSWorldRenderer::Vertex)+sizeof(unsigned)));
        if(!residency)return false;
        Sink farPass,nearPass;farPass.vertices.reserve(90);farPass.indices.reserve(90);nearPass.vertices.reserve(90);nearPass.indices.reserve(90);
        vs_debug_beacon_cross(farPass,position,2.f,2.f,50.f);vs_debug_beacon_cube(farPass,.10f,position);
        vs_debug_beacon_cross(nearPass,position,.5f,.5f,.5f);vs_debug_beacon_cube(nearPass,.10f,position);
        VSWorldRenderer::Mesh nextFar,nextNear;
        if(!renderer.publishMesh(nextFar,farPass.vertices,farPass.indices) || !renderer.publishMesh(nextNear,nearPass.vertices,nearPass.indices))return false;
        result.position=position;result.farPass=std::move(nextFar);result.nearPass=std::move(nextNear);
        return true;
    }
};
VSBeaconPresentation::VSBeaconPresentation(VSWorldRenderer&r,VSWorldScene&w):mImpl(std::make_unique<Impl>(r,w)){}
VSBeaconPresentation::~VSBeaconPresentation(){reset();}
void VSBeaconPresentation::reset(){mImpl->retireLabels();mImpl->sky={};mImpl->cache.clear();mImpl->account={};gObjectList.resetObjectBeacons();}
void VSBeaconPresentation::append(const VSNativeSession& session,std::vector<VSWorldRenderer::Packet>& packets)
{
    if(!session.current(mImpl->account)){gObjectList.resetObjectBeacons();mImpl->retireLabels();mImpl->sky={};mImpl->cache.clear();mImpl->account=session.account();}
    // Consume the actual current-frame queue, then clear after model publication.
    if(LLFloaterTelehub::renderBeacons())LLFloaterTelehub::addBeacons();
    const auto& sources=gObjectList.debugBeacons();
    for(std::size_t i=sources.size();i<mImpl->cache.size();++i)if(mImpl->cache[i].label)mImpl->cache[i].label->markDead();
    mImpl->cache.resize(sources.size());
    std::vector<bool> admitted(sources.size(),false);
    for(std::size_t i=0;i<sources.size();++i)admitted[i]=mImpl->publish(mImpl->cache[i],sources[i].mPositionAgent);
    // Preserve all farPass passes before all nearPass passes, matching renderObjectBeacons.
    for(unsigned pass=0;pass<2;++pass)for(std::size_t i=0;i<sources.size();++i)
    {
        if(!admitted[i])continue;
        auto color=sources[i].mColor;if(!pass)color.mV[3]*=.25f;
        color=linearColor4(color);
        VSWorldRenderer::Packet packet;packet.mesh=pass?mImpl->cache[i].nearPass:mImpl->cache[i].farPass;
        packet.color=Impl::vertexColor(color);packet.fullbright=true;
        packet.alpha=true;packet.overlay=true;packet.depthTest=pass!=0;packet.depthWriteAlpha=pass!=0;
        packet.screenLinePixels=float(sources[i].mLineWidth);
        packets.push_back(std::move(packet));
    }
    // Keep actual text owners until the next accepted frame/reset; the input
    // queue can then retire without destroying labels before UI collection.
    for(std::size_t i=0;i<sources.size();++i)mImpl->label(mImpl->cache[i],sources[i]);
    gObjectList.resetObjectBeacons();
    mImpl->sunMoon(packets);
}
