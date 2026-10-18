// Original tracker lifecycle/geometry, real CPU atmosphere and native submission.
#include "llviewerprecompiledheaders.h"
#include "vstrackerpresentation.h"
#include "vstrackermeshsink.h"
#include "vstrackerdraw.h"
#include "vstrackergeometry.h"
#include "vsnativesession.h"
#include "vsworldscene.h"
#include "lllegacyatmospherics.h"
#include "llenvironment.h"
#include "lltracker.h"
#include "llviewercamera.h"
#include "llappviewer.h"
#include "pipeline.h"
#include <limits>
struct VSTrackerPresentation::Impl
{
    VSWorldRenderer& renderer;
    VSWorldScene& world;
    LLAtmospherics atmosphere;
    LLColor4 fog;
    VSNativeSession::AccountHandle account;
    std::vector<VSWorldRenderer::Mesh> accepted;
    Impl(VSWorldRenderer& r,VSWorldScene& w):renderer(r),world(w){atmosphere.init();fog=atmosphere.getFogColor();}
};
VSTrackerPresentation::VSTrackerPresentation(VSWorldRenderer&r,VSWorldScene&w):mImpl(std::make_unique<Impl>(r,w)){}
VSTrackerPresentation::~VSTrackerPresentation()=default;
void VSTrackerPresentation::reset(){mImpl->accepted.clear();mImpl->account={};}
void VSTrackerPresentation::append(const VSNativeSession& session,std::vector<VSWorldRenderer::Packet>& packets)
{
    if(!session.current(mImpl->account)){mImpl->accepted.clear();mImpl->account=session.account();}
    // Keep the actual reference fog owner and render-feature policy. No fake
    // sky/legacy scene object is constructed to obtain the color.
    if(gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_FOG))
        mImpl->fog=mImpl->atmosphere.calculateSkyFogColor(LLEnvironment::instance().getCurrentSky(),
            LLVector3(LLEnvironment::instance().getClampedLightNorm()));
    VSTrackerDraw::Context context;
    context.fogColor=mImpl->fog;
    std::size_t ordinal=0;
    context.beam=[&](const LLVector3&position,const std::string& direction,const LLColor4& color,F32 distance)
    {
        if(!session.current(mImpl->account))return;
        const auto index=ordinal++;
        if(mImpl->accepted.size()<=index)mImpl->accepted.resize(index+1);
        const float height=direction=="DOWN"?5020.f-position.mV[2]:position.mV[2];
        const double rows=std::max(2.f,height<0.f?0.f:std::ceil((256.f*height)/5020.f));
        // Input accounting follows the original number of rows plus fan
        // triangulation. There is no arbitrary height/geometry truncation.
        if(!std::isfinite(rows) || rows>(std::numeric_limits<unsigned>::max()-96.)/12.)return;
        const auto capacity=std::size_t(rows)*12+96;
        const auto residency=mImpl->world.reserveSpatialBytes(capacity*(sizeof(VSWorldRenderer::Vertex)+sizeof(unsigned)));
        if(!residency)return;
        VSTrackerMeshSink sink(capacity);
        vs_tracker_beam(sink,position,direction,color,distance,[]{return gRenderStartTime.getElapsedTimeF32();},
            LLViewerCamera::getInstance()->getLeftAxis(),LLTracker::getTrackingStatus()==LLTracker::TRACKING_AVATAR,LLTracker::sCheesyBeacon);
        if(!mImpl->renderer.publishMesh(mImpl->accepted[index],sink.vertices,sink.indices,VSWorldRenderer::Topology::Triangles,
            VSWorldContracts::Position|VSWorldContracts::Normal|VSWorldContracts::UV0|VSWorldContracts::Color))return;
        if(!session.current(mImpl->account))return;
        VSWorldRenderer::Packet packet;packet.mesh=mImpl->accepted[index];packet.fullbright=true;packet.doubleSided=true;
        packet.alpha=true;packet.overlay=true;packet.depthWriteAlpha=false;
        packets.push_back(std::move(packet));
    };
    VSTrackerDraw::Scope scope(context);
    LLTracker::render3D();
    if(session.current(mImpl->account))mImpl->accepted.resize(ordinal);
}
