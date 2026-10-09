// Viewer UI CPU assets, immutable publication and ordered facade draws. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsuiresources.h"
#include "llimage.h"
#include "llfontgl.h"
#include "llvector4a.h"
#include "v2math.h"
#include "v4coloru.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace
{
void check(bool condition,const char* text) { if (!condition) throw std::runtime_error(text); }
VSUIRenderer::Blend nativeBlend()
{
    if (!LLRender2D::isNativeUI()) return VSUIRenderer::Blend::StraightAlpha;
    switch (LLRender2D::nativeBlend())
    {
    case LLRender::BT_ADD: return VSUIRenderer::Blend::Additive;
    case LLRender::BT_ADD_WITH_ALPHA: return VSUIRenderer::Blend::AdditiveAlpha;
    default: return VSUIRenderer::Blend::StraightAlpha;
    }
}
}
struct VSUIResources::Impl
{
    struct Asset
    {
        unsigned width,height;
        std::vector<std::uint8_t> bytes;
        VSUIRenderer::Image image;
    };
    struct Entry { std::shared_ptr<Asset> asset; LLUIImagePtr facade; };
    VSUIRenderer& renderer;
    std::map<std::string,Entry> assets;
    std::vector<VSUIRenderer::Packet> packets;
    VSUIRenderer::Sampling sampling=VSUIRenderer::Sampling::Linear;
    float logical_height=0,logical_width=0;
    float dpi=1;
    struct FontPage { LLPointer<LLImageRaw> raw; S32 generation=-1; std::string key; };
    std::map<LLImageRaw*,FontPage> font_pages;
    std::array<float,4> clip{};
    bool active=false;
    explicit Impl(VSUIRenderer& value) : renderer(value) {}
    void draw(const Asset& a,S32 x,S32 y,S32 width,S32 height,const LLColor4& color,
              bool solid,const LLRectf& outer,const LLRectf& center,bool inner)
    {
        check(active,"Native UI facade draw outside packet collection");
        const auto origin=LLRender2D::isNativeUI()?LLRender2D::nativeOrigin():std::array<F32,2>{0,0};
        if (width<=0 || height<=0) return;
        for (float v: {outer.mLeft,outer.mRight,outer.mTop,outer.mBottom,
                       center.mLeft,center.mRight,center.mTop,center.mBottom})
            check(std::isfinite(v),"Non-finite native UI image region");
        check(outer.getWidth()>0 && outer.getHeight()>0 &&
              outer.mLeft>=0 && outer.mRight<=1 && outer.mBottom>=0 && outer.mTop<=1 &&
              center.mLeft>=0 && center.mRight<=1 && center.mBottom>=0 && center.mTop<=1,
              "Invalid native UI image region");
        auto quad=[&](float l,float b,float r,float t,float ul,float vb,float ur,float vt)
        {
            if (l>=r || b>=t) return;
            VSUIRenderer::Packet p; p.blend=nativeBlend(); p.image=a.image;
            p.bounds={x+origin[0]+l,logical_height-y-origin[1]-t,x+origin[0]+r,logical_height-y-origin[1]-b};
            p.uv={ul,1-vt,ur,1-vb}; p.clip=clip;
            std::copy_n(color.mV,4,p.color.begin());
            // drawSolid uses the image alpha mask, with RGB replaced by tint.
            p.alpha_mask=solid;
            p.sampling=sampling;
            packets.push_back(std::move(p));
        };
        if (center.mLeft==0 && center.mRight==1 && center.mBottom==0 && center.mTop==1)
        { quad(0,0,float(width),float(height),outer.mLeft,outer.mBottom,outer.mRight,outer.mTop); return; }
        const float uw=outer.getWidth(),uh=outer.getHeight();
        LLRectf uv(outer.mLeft+center.mLeft*uw,outer.mBottom+center.mTop*uh,
                   outer.mLeft+center.mRight*uw,outer.mBottom+center.mBottom*uh);
        // Preserve LLUIImage's existing inner/outer nine-slice geometry,
        // including collapsed or reversed center UVs in existing skin assets.
        LLRectf c(uv.mLeft*a.width,uv.mTop*a.height,uv.mRight*a.width,uv.mBottom*a.height);
        const float natural_w=std::round(a.width*uw),natural_h=std::round(a.height*uh);
        check(natural_w>0 && natural_h>0,"Empty native image natural extent");
        if (inner)
        {
            c.mRight+=width-natural_w; c.mTop+=height-natural_h;
            const float sw=std::max(0.f,c.mLeft-c.mRight),sh=std::max(0.f,c.mBottom-c.mTop);
            const float rw=center.getWidth()==1 ? 0 : sw/(natural_w*(1-center.getWidth()));
            const float rh=center.getHeight()==1 ? 0 : sh/(natural_h*(1-center.getHeight()));
            const float scale=1-std::max(rw,rh);
            c.mLeft*=scale; c.mBottom*=scale;
            c.mRight=width+(c.mRight-width)*scale; c.mTop=height+(c.mTop-height)*scale;
        }
        else
        {
            check(c.getWidth()>0 && c.getHeight()>0,"Empty native outer-scale image center");
            const float scale=std::min({float(width)/c.getWidth(),float(height)/c.getHeight(),1.f});
            c.setCenterAndSize(uv.getCenterX()*width,uv.getCenterY()*height,c.getWidth()*scale,c.getHeight()*scale);
        }
        const float xs[]{0,c.mLeft,c.mRight,float(width)},ys[]{0,c.mBottom,c.mTop,float(height)};
        const float us[]{outer.mLeft,uv.mLeft,uv.mRight,outer.mRight};
        const float vs[]{outer.mBottom,uv.mBottom,uv.mTop,outer.mTop};
        for (unsigned row=0;row<3;++row) for (unsigned col=0;col<3;++col)
            quad(xs[col],ys[row],xs[col+1],ys[row+1],us[col],vs[row],us[col+1],vs[row+1]);
    }
};
VSUIResources::VSUIResources(VSUIRenderer& r) : mImpl(std::make_shared<Impl>(r)) {}
VSUIResources::~VSUIResources() = default;
LLUIImagePtr VSUIResources::publish(const std::string& key,const LLImageRaw& raw)
{
    const auto width=raw.getWidth(); const auto height=raw.getHeight(); const auto components=raw.getComponents();
    check(!key.empty() && width>0 && height>0 && width<=16384 && height<=16384 &&
          components>=1 && components<=4 && raw.getData(),"Invalid native UI CPU image");
    auto asset=std::make_shared<Impl::Asset>(); asset->width=width; asset->height=height;
    asset->bytes.resize(std::size_t(width)*height*4);
    for (unsigned y=0;y<asset->height;++y) for (unsigned x=0;x<asset->width;++x)
    {
        const auto* s=raw.getData()+((std::size_t(height)-1-y)*width+x)*components;
        auto* d=asset->bytes.data()+(std::size_t(y)*width+x)*4;
        d[0]=s[0]; d[1]=components<3?s[0]:s[1]; d[2]=components<3?s[0]:s[2];
        d[3]=components==2?s[1]:components==4?s[3]:255;
    }
    asset->image=mImpl->renderer.upload(width,height,asset->bytes);
    std::weak_ptr<Impl> owner=mImpl;
    LLUIImagePtr facade=new LLUIImage(key,width,height,
        [owner,asset](S32 x,S32 y,S32 w,S32 h,const LLColor4& color,bool solid,
                      const LLRectf& outer,const LLRectf& center,bool inner)
        {
            auto canvas=owner.lock(); check(bool(canvas),"Native UI image owner has retired");
            canvas->draw(*asset,x,y,w,h,color,solid,outer,center,inner);
        });
    mImpl->assets.insert_or_assign(key,Impl::Entry{asset,facade});
    return facade;
}
LLUIImagePtr VSUIResources::region(const std::string& key,const std::string& name,const LLRectf& uv,
                                  VSUIRenderer::Sampling sampling)
{
    auto found=mImpl->assets.find(key); check(found!=mImpl->assets.end(),"Unknown native UI image region");
    const auto asset=found->second.asset;
    std::weak_ptr<Impl> owner=mImpl;
    LLUIImagePtr facade=new LLUIImage(name,asset->width,asset->height,
        [owner,asset,sampling](S32 x,S32 y,S32 w,S32 h,const LLColor4& color,bool solid,
                              const LLRectf& outer,const LLRectf& center,bool inner)
        {
            auto canvas=owner.lock(); check(bool(canvas),"Native UI image owner has retired");
            const auto previous=canvas->sampling; canvas->sampling=sampling;
            try { canvas->draw(*asset,x,y,w,h,color,solid,outer,center,inner); }
            catch (...) { canvas->sampling=previous; throw; }
            canvas->sampling=previous;
        });
    facade->setClipRegion(uv);
    return facade;
}
void VSUIResources::patch(const std::string& key,unsigned x,unsigned y,unsigned width,unsigned height,
                         const std::vector<std::uint8_t>& rgba)
{
    auto found=mImpl->assets.find(key); check(found!=mImpl->assets.end(),"Unknown native UI image patch");
    auto& a=*found->second.asset;
    check(width && height && x<=a.width && y<=a.height && width<=a.width-x && height<=a.height-y &&
          rgba.size()==std::size_t(width)*height*4,"Invalid native UI image patch");
    auto bytes=a.bytes;
    for (unsigned row=0;row<height;++row)
        std::copy_n(rgba.data()+std::size_t(row)*width*4,width*4,bytes.data()+((std::size_t(y)+row)*a.width+x)*4);
    auto image=mImpl->renderer.replace(a.image,x,y,width,height,rgba);
    a.bytes=std::move(bytes); a.image=std::move(image);
    found->second.facade->onImageLoaded();
}
void VSUIResources::begin(unsigned width,unsigned height,float dpi)
{
    check(!mImpl->active && width && height && std::isfinite(dpi) && dpi>0,"Invalid native UI collection begin");
    mImpl->packets.clear(); mImpl->logical_height=height/dpi;mImpl->logical_width=width/dpi;
    mImpl->dpi=dpi;
    mImpl->clip={0,0,width/dpi,height/dpi}; mImpl->active=true;
}
void VSUIResources::setClip(std::array<float,4> clip)
{
    check(mImpl->active,"Native UI clip outside collection"); mImpl->clip=clip;
}
std::vector<VSUIRenderer::Packet> VSUIResources::finish()
{
    check(mImpl->active,"Native UI collection is not active"); mImpl->active=false;
    return std::move(mImpl->packets);
}
void VSUIResources::erase(const std::string& key) { mImpl->assets.erase(key); }
std::uint64_t VSUIResources::generation(const std::string& key) const
{
    auto found=mImpl->assets.find(key); check(found!=mImpl->assets.end(),"Unknown native UI generation");
    return VSUIRenderer::generation(found->second.asset->image);
}
void VSUIResources::fontBatch(LLImageRaw* raw,S32 generation,const LLVector4a* positions,
                             const LLVector2* uv,const LLColor4U* colors,S32 count)
{
    auto& c=*mImpl;
    check(c.active && raw && raw->getData() && raw->getWidth()>0 && raw->getHeight()>0 &&
          positions && uv && colors && count>0 && count%6==0,"Invalid native font batch");
    auto& page=c.font_pages[raw];
    if (!page.raw)
    {
        page.raw=raw;page.key="viewer-font-"+std::to_string(c.font_pages.size());
    }
    if (page.generation!=generation)
    {
        auto existing=c.assets.find(page.key);
        if (existing==c.assets.end() || existing->second.asset->width!=unsigned(raw->getWidth()) ||
            existing->second.asset->height!=unsigned(raw->getHeight())) publish(page.key,*raw);
        else
        {
            // The upstream atlas exposes a generation but no dirty rectangle.
            // Compare its CPU bytes with the published snapshot and upload only
            // the bounding changed region into a new immutable GPU generation.
            const auto& asset=*existing->second.asset;
            const auto components=raw->getComponents();
            check(components==2 || components==4,"Invalid native font atlas components");
            unsigned left=asset.width,top=asset.height,right=0,bottom=0;
            auto pixel=[&](unsigned x,unsigned y)
            {
                const auto* s=raw->getData()+((std::size_t(asset.height)-1-y)*asset.width+x)*components;
                return std::array<std::uint8_t,4>{s[0],components==2?s[0]:s[1],
                    components==2?s[0]:s[2],components==2?s[1]:s[3]};
            };
            for (unsigned y=0;y<asset.height;++y) for (unsigned x=0;x<asset.width;++x)
            {
                const auto p=pixel(x,y);
                if (!std::equal(p.begin(),p.end(),asset.bytes.begin()+(std::size_t(y)*asset.width+x)*4))
                { left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1); }
            }
            if (right>left && bottom>top)
            {
                std::vector<std::uint8_t> bytes(std::size_t(right-left)*(bottom-top)*4);
                for (unsigned y=top;y<bottom;++y) for (unsigned x=left;x<right;++x)
                {
                    const auto p=pixel(x,y);
                    std::copy(p.begin(),p.end(),bytes.begin()+(std::size_t(y-top)*(right-left)+x-left)*4);
                }
                patch(page.key,left,top,right-left,bottom-top,bytes);
            }
        }
        page.generation=generation;
    }
    auto image=c.assets.at(page.key).asset->image;
    for (S32 first=0;first<count;first+=6)
    {
        VSUIRenderer::Packet p; p.blend=nativeBlend();p.image=image;p.clip=c.clip;p.triangles.emplace();
        for (S32 i=0;i<6;++i)
        {
            const auto* pos=positions[first+i].getF32ptr();const auto& tex=uv[first+i];const auto& color=colors[first+i];
            (*p.triangles)[i]={pos[0]/c.dpi,c.logical_height-pos[1]/c.dpi,tex.mV[0],1-tex.mV[1],
                              color.mV[0]/255.f,color.mV[1]/255.f,color.mV[2]/255.f,color.mV[3]/255.f};
        }
        c.packets.push_back(std::move(p));
    }
}
void VSUIResources::releaseFontPages()
{
    for (const auto& page:mImpl->font_pages) mImpl->assets.erase(page.second.key);
    mImpl->font_pages.clear();
}
void VSUIResources::rectangle(const LLRectf& r,const LLColor4& color)
{
    auto& c=*mImpl;check(c.active,"Native rectangle outside collection");
    if (r.getWidth()<=0 || r.getHeight()<=0) return;
    const std::string key="native-solid-white";
    if (!c.assets.count(key))
    {
        LLPointer<LLImageRaw> raw=new LLImageRaw(1,1,4);raw->clear(255,255,255,255);publish(key,*raw);
    }
    VSUIRenderer::Packet p; p.blend=nativeBlend();p.image=c.assets.at(key).asset->image;
    p.bounds={r.mLeft/c.dpi,c.logical_height-r.mTop/c.dpi,r.mRight/c.dpi,c.logical_height-r.mBottom/c.dpi};
    p.clip=c.clip;std::copy_n(color.mV,4,p.color.begin());c.packets.push_back(std::move(p));
}
void VSUIResources::screenClip(const LLRect* rect)
{
    auto& c=*mImpl;check(c.active,"Native clipping outside collection");
    if (!rect) { c.clip={0,0,c.logical_width,c.logical_height};return; }
    if (rect->isEmpty()) { c.clip={0,0,0,0};return; }
    // Match the existing UI scissor convention: round the physical origin down,
    // round its extent up and include one extra physical pixel on the far edges.
    // Adding a logical pixel instead over-expands clips at high DPI.
    const float left=std::floor(rect->mLeft*c.dpi),bottom=std::floor(rect->mBottom*c.dpi);
    const float right=left+std::ceil(rect->getWidth()*c.dpi)+1.f;
    const float top=bottom+std::ceil(rect->getHeight()*c.dpi)+1.f;
    c.clip={left/c.dpi,c.logical_height-top/c.dpi,right/c.dpi,c.logical_height-bottom/c.dpi};
}

void VSUIResources::triangle(const std::array<std::array<float, 6>, 3>& vertices)
{
    auto& c = *mImpl;
    check(c.active, "Native triangle outside collection");
    const std::string key = "native-solid-white";
    if (!c.assets.count(key))
    {
        LLPointer<LLImageRaw> raw = new LLImageRaw(1,1,4);
        raw->clear(255,255,255,255);
        publish(key, *raw);
    }
    VSUIRenderer::Packet p;
    p.image = c.assets.at(key).asset->image;
    p.blend = nativeBlend(); p.clip = c.clip;
    p.triangles.emplace();
    for (unsigned i=0; i<6; ++i)
    {
        const auto& v = vertices[i < 3 ? i : 2]; // A single triangle plus a degenerate triangle.
        (*p.triangles)[i] = {v[0]/c.dpi, c.logical_height-v[1]/c.dpi, 0.f, 0.f, v[2],v[3],v[4],v[5]};
    }
    c.packets.push_back(std::move(p));
}
