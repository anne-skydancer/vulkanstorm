// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsworldpaint.h"
#include "vsimmediateprimitives.h"
#include "llrender.h"
#include <cstring>
#include <limits>
namespace
{
std::optional<unsigned> blendFactor(unsigned factor)
{
    switch(factor)
    {
    case 1:return 0;case 0:return 1;case 0x0306:return 2;case 0x0300:return 3;
    case 0x0307:return 4;case 0x0301:return 5;case 0x0304:return 6;
    case 0x0302:return 7;case 0x0305:return 8;case 0x0303:return 9;default:return {};
    }
}
std::optional<VSWorldRenderer::DepthCompare> comparison(unsigned value)
{
    switch(value)
    {
    case 0x0203:return VSWorldRenderer::DepthCompare::LessEqual;
    case 0x0207:return VSWorldRenderer::DepthCompare::Always;
    case 0x0204:return VSWorldRenderer::DepthCompare::Greater;
    case 0x0206:return VSWorldRenderer::DepthCompare::GreaterEqual;
    default:return {};
    }
}
static_assert(LLRender::TRIANGLES==VSImmediateGeometry::Triangles && LLRender::TRIANGLE_STRIP==VSImmediateGeometry::TriangleStrip &&
    LLRender::TRIANGLE_FAN==VSImmediateGeometry::TriangleFan && LLRender::POINTS==VSImmediateGeometry::Points &&
    LLRender::LINES==VSImmediateGeometry::Lines && LLRender::LINE_STRIP==VSImmediateGeometry::LineStrip && LLRender::LINE_LOOP==VSImmediateGeometry::LineLoop);
}
struct VSWorldPaint::Impl
{
    VSWorldRenderer& renderer;Reserve reserve;std::function<bool()>current;
    std::unique_ptr<VSImmediateGeometry> stream;
    struct Cache{VSWorldRenderer::Mesh mesh;std::shared_ptr<const VSResourceBudget::Reservation>residency;};
    std::vector<Cache>cache;std::vector<VSWorldRenderer::Packet>packets;
    std::vector<std::shared_ptr<const VSResourceBudget::Reservation>>packetResidency;
    std::size_t next=0;bool accepted=false;
    Impl(VSWorldRenderer&r,Reserve b,std::function<bool()>c):renderer(r),reserve(std::move(b)),current(std::move(c)){}
    bool submit(VSImmediateGeometry::Batch&& batch)
    {
        if(!current())return false;
        VSWorldRenderer::Packet packet;packet.alpha=batch.state.blend;packet.cull=batch.state.cull;packet.cullFront=batch.state.cullFront;
        packet.depthTest=batch.state.depthTest;packet.depthWrite=batch.state.depthWrite;
        const auto compare=comparison(batch.state.depthCompare);if(!compare)return false;packet.depthCompare=*compare;
        if(batch.state.blend)
        {
            const auto source=blendFactor(batch.state.sourceBlend),destination=blendFactor(batch.state.destinationBlend);
            if(!source || !destination)return false;
            packet.blend=VSWorldRenderer::Blend{*source,*destination,*source,*destination,15};
        }
        if(batch.state.projection)
        {
            struct ProjectionOwner{std::array<float,16>matrix;std::shared_ptr<const VSResourceBudget::Reservation>residency;};
            auto charge=reserve(sizeof(ProjectionOwner)+64);if(!charge)return false;
            try
            {
                auto owner=std::make_shared<ProjectionOwner>();owner->residency=std::move(charge);
                std::copy_n(glm::value_ptr(*batch.state.projection),16,owner->matrix.data());
                packet.projectionOverride=std::shared_ptr<const std::array<float,16>>(owner,&owner->matrix);
            }
            catch(const std::bad_alloc&){return false;}
        }
        packet.image=std::static_pointer_cast<const VSUIRenderer::Texture>(batch.state.image);
        packet.fullbright=true;packet.overlay=true;packet.hud=batch.state.hud;
        std::size_t vertices=0,indices=0;
        const auto count=[&](const auto&,const auto&,const auto&,unsigned kind)
        {
            const auto v=kind==3?3u:4u,i=kind==3?3u:6u;
            if(vertices>U32_MAX-v || indices>U32_MAX-i)return false;
            vertices+=v;indices+=i;return true;
        };
        if(!vs_immediate_primitives(batch,count))return false;
        if(!vertices)return true;
        const auto scratch=reserve(vertices*sizeof(VSWorldRenderer::Vertex)+indices*sizeof(unsigned));if(!scratch)return false;
        std::vector<VSWorldRenderer::Vertex> output;std::vector<unsigned>elements;
        try{output.reserve(vertices);elements.reserve(indices);}catch(const std::bad_alloc&){return false;}
        const auto vertex=[](const VSImmediateGeometry::Vertex& value)
        {
            VSWorldRenderer::Vertex v{value.position.x,value.position.y,value.position.z,value.normal.x,value.normal.y,value.normal.z,value.uv.x,value.uv.y};
            v.r=value.color.r;v.g=value.color.g;v.b=value.color.b;v.a=value.color.a;return v;
        };
        const auto emit=[&](const auto& a,const auto& b,const auto& c,unsigned kind)
        {
            const unsigned first=unsigned(output.size());
            if(kind==3)
            {output.push_back(vertex(a));output.push_back(vertex(b));output.push_back(vertex(c));elements.insert(elements.end(),{first,first+1,first+2});}
            else
            {
                constexpr float corners[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
                for(unsigned i=0;i<4;++i)
                {
                    auto v=vertex(kind==2 && (i==1 || i==2)?b:a);
                    if(kind==1){v.u=corners[i][0];v.v=corners[i][1];}
                    else {v.x=a.position.x;v.y=a.position.y;v.z=a.position.z;v.nx=b.position.x;v.ny=b.position.y;v.nz=b.position.z;v.u=(corners[i][0]+1)*.5f;v.v=corners[i][1];}
                    output.push_back(v);
                }
                elements.insert(elements.end(),{first,first+1,first+2,first,first+2,first+3});
                packet.cull=false;
                if(kind==1)packet.screenPointPixels=batch.state.pointPixels;else packet.screenLinePixels=batch.state.linePixels;
            }
            return true;
        };
        if(!vs_immediate_primitives(batch,emit) || !current())return false;
        if(batch.state.debugShader)
        {
            packet.constantColor=true;
            for(unsigned c=0;c<4;++c)packet.color[c]=std::max(0.f,batch.state.uniformColor[c]);
            for(auto& vertex:output)vertex.r=vertex.g=vertex.b=vertex.a=1;
        }
        else
        {
            const auto& first=output.front();const std::array<float,4> color{first.r,first.g,first.b,first.a};
            if(std::all_of(output.begin(),output.end(),[&](const auto& v){return color==std::array<float,4>{v.r,v.g,v.b,v.a};}))
            {
                // Uniform color changes are draw state, as in the reference;
                // they must not reupload an otherwise unchanged gizmo mesh.
                packet.color=color;for(auto& vertex:output)vertex.r=vertex.g=vertex.b=vertex.a=1;
            }
        }
        if(next==cache.size())
        {
            auto charge=reserve(sizeof(Cache));if(!charge)return false;
            try{cache.push_back({{},std::move(charge)});}catch(const std::bad_alloc&){return false;}
        }
        auto& mesh=cache[next++].mesh;
        const bool same=mesh && VSWorldRenderer::geometryVertices(mesh).size()==output.size() &&
            VSWorldRenderer::geometryIndices(mesh)==elements &&
            !std::memcmp(VSWorldRenderer::geometryVertices(mesh).data(),output.data(),output.size()*sizeof(output[0]));
        if(!same && !renderer.publishMesh(mesh,output,elements,VSWorldRenderer::Topology::Triangles,
            VSWorldContracts::Position|VSWorldContracts::Normal|VSWorldContracts::UV0|VSWorldContracts::Color))return false;
        const auto packetCharge=reserve(sizeof(packet));if(!packetCharge || !current())return false;
        packet.mesh=mesh;
        try{packets.push_back(std::move(packet));packetResidency.push_back(packetCharge);}catch(const std::bad_alloc&){return false;}
        return true;
    }
};
VSWorldPaint::VSWorldPaint(VSWorldRenderer&r,Reserve reserve,std::function<bool()>current):mImpl(std::make_unique<Impl>(r,std::move(reserve),std::move(current))){}
VSWorldPaint::~VSWorldPaint()=default;
VSImmediateGeometry& VSWorldPaint::begin()
{
    auto& state=*mImpl;state.packets.clear();state.packetResidency.clear();state.next=0;state.accepted=false;
    state.stream=std::make_unique<VSImmediateGeometry>(state.reserve,[state=&state](auto&& batch){return state->submit(std::move(batch));});
    return *state.stream;
}
VSImmediateGeometry& VSWorldPaint::begin(const glm::mat4& modelview,const glm::mat4& projection)
{
    auto& state=*mImpl;state.packets.clear();state.packetResidency.clear();state.next=0;state.accepted=false;
    state.stream=std::make_unique<VSImmediateGeometry>(state.reserve,[state=&state](auto&& batch)
        {return state->submit(std::move(batch));},modelview,projection);
    return *state.stream;
}
bool VSWorldPaint::finish(std::vector<VSWorldRenderer::Packet>& output)
{
    auto& state=*mImpl;if(!state.stream)return false;state.stream->flush();
    if(!state.current() || !state.stream->good())return false;
    output.insert(output.end(),state.packets.begin(),state.packets.end());state.cache.resize(state.next);state.accepted=true;return true;
}
void VSWorldPaint::retire(){mImpl->stream.reset();mImpl->packets.clear();mImpl->packetResidency.clear();mImpl->cache.clear();}
