// Shared LLDebugBeacon data + original CPU geometry; no legacy scene owner.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsbeaconpresentation.h"
#include "vsdebugbeacongeometry.h"
#include "vsworldscene.h"
#include "vsnativesession.h"
#include "llviewerobjectlist.h"
#include "llfloatertelehub.h"
#include <array>
#include <optional>
struct VSBeaconPresentation::Impl
{
    VSWorldRenderer& renderer;
    VSWorldScene& world;
    struct Cache{LLVector3 position;VSWorldRenderer::Mesh farPass,nearPass;};
    std::vector<Cache> cache;
    VSNativeSession::AccountHandle account;
    Impl(VSWorldRenderer&r,VSWorldScene&w):renderer(r),world(w){}
    struct Sink
    {
        std::vector<VSWorldRenderer::Vertex> vertices;
        std::vector<unsigned> indices;
        std::optional<std::array<float,3>> first;
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
        result={position,std::move(nextFar),std::move(nextNear)};
        return true;
    }
};
VSBeaconPresentation::VSBeaconPresentation(VSWorldRenderer&r,VSWorldScene&w):mImpl(std::make_unique<Impl>(r,w)){}
VSBeaconPresentation::~VSBeaconPresentation()=default;
void VSBeaconPresentation::reset(){mImpl->cache.clear();mImpl->account={};gObjectList.resetObjectBeacons();}
void VSBeaconPresentation::append(const VSNativeSession& session,std::vector<VSWorldRenderer::Packet>& packets)
{
    if(!session.current(mImpl->account)){mImpl->cache.clear();mImpl->account=session.account();}
    // The normal reference queue is rebuilt for this exact frame/account.
    gObjectList.resetObjectBeacons();
    if(LLFloaterTelehub::renderBeacons())LLFloaterTelehub::addBeacons();
    const auto& sources=gObjectList.debugBeacons();
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
        packet.color={color.mV[0],color.mV[1],color.mV[2],color.mV[3]};packet.fullbright=true;
        packet.alpha=true;packet.overlay=true;packet.depthTest=pass!=0;packet.depthWriteAlpha=pass!=0;
        packet.screenLinePixels=float(sources[i].mLineWidth);
        packets.push_back(std::move(packet));
    }
    gObjectList.resetObjectBeacons();
}
