// Viewer UI CPU assets, immutable publication and ordered facade draws. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsuiresources.h"
#include "vstextureupload.h"
#include "vsuiimageprovider.h"
#include "llimage.h"
#include "lldir.h"
#include <fstream>
#include <tuple>
#include "llfontgl.h"
#include "llvector4a.h"
#include "v2math.h"
#include "v4coloru.h"
#include "llsd.h"
#include "llsdutil.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace
{
void check(bool condition,const char* text) { if (!condition) throw std::runtime_error(text); }
VSWorldContracts::ImageRole imageRole(const std::string& key)
{ return key.compare(0, 17, "avatar-composite:") == 0 ? VSWorldContracts::ImageRole::AvatarComposite
                      : (key.compare(0, 7, "remote:") == 0 || key.compare(0, 17, "gltf-scene-image:") == 0)
                          ? VSWorldContracts::ImageRole::RemoteAsset
                          : key.compare(0, 6, "media:") == 0 ? VSWorldContracts::ImageRole::Media
                          : VSWorldContracts::ImageRole::CoreUI; }
float nativeVertexColor(float value)
{
    // LLRender::color4f stores a normalized U8 vertex channel, truncating
    // after clamp. Preserve that representation for image/primitive parity.
    return static_cast<U8>(llclamp(value, 0.f, 1.f) * 255.f) / 255.f;
}
VSUIRenderer::Blend nativeBlend()
{
    if (!LLRender2D::isNativeUI()) return VSUIRenderer::Blend::StraightAlpha;
    switch (LLRender2D::nativeBlend())
    {
    case LLRender::BT_REPLACE: return VSUIRenderer::Blend::Replace;
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
        VSUIRenderer::Image image;
    };
    struct Entry { std::shared_ptr<Asset> asset; LLUIImagePtr facade; };
    Entry prepareEntry(const std::shared_ptr<Impl>& canvas,const std::string& key,const VSUIResources::PreparedImage& prepared)
    {
        const auto width=prepared->width,height=prepared->height;
        auto asset=std::make_shared<Impl::Asset>();asset->width=width;asset->height=height;
        asset->image=renderer.uploadPrepared(prepared);
        if(!asset->image) return {};
        std::weak_ptr<Impl> owner=canvas;
        LLUIImagePtr facade=new LLUIImage(key,width,height,
            [owner,asset](S32 x,S32 y,S32 w,S32 h,const LLColor4& color,bool solid,
                          const LLRectf& outer,const LLRectf& center,bool inner)
            {
                auto canvas=owner.lock(); check(bool(canvas),"Native UI image owner has retired");
                canvas->draw(*asset,x,y,w,h,color,solid,outer,center,inner);
            });
        facade->setNativeRotatedDraw([owner,asset](S32 x,S32 y,S32 w,S32 h,F32 degrees,
                                                   const LLColor4& color,const LLRectf& uv)
        {
            auto canvas=owner.lock(); check(bool(canvas),"Native UI image owner has retired");
            canvas->drawRotated(*asset,x,y,w,h,degrees,color,uv);
        });
        return {asset,facade};
    }

    struct BumpEntry {
        VSUIRenderer::Image image;
        std::uint64_t sourceGeneration=0,touched=0;
        float scale=0;
        unsigned heightChannel=0;
    };
    std::array<LLUUID,32> standardBumps;
    unsigned standardBumpCount=3;
    std::uint64_t bumpClock=0;
    std::map<std::pair<LLUUID,unsigned>,BumpEntry> bumps;
    VSUIRenderer& renderer;
    VSTextureMemory::Policy textureMemory;
    U64 lastRemoteRefusals=0;bool remotePressure=false;
    double remotePressureReleaseBytes=0;
    VSResourceBudget imagePreparationBytes{640 * 1024 * 1024};
    std::map<std::string,Entry> assets;
    std::vector<VSUIRenderer::Packet> packets;
    VSUIRenderer::Sampling sampling=VSUIRenderer::Sampling::Linear;
    float logical_height=0,logical_width=0;
    float dpi=1;
    struct FontPage
    {
        LLPointer<LLImageRaw>   raw;
        S32                     generation = -1, pending_generation = -1;
        std::string             key;
        VSUIRenderer::FontAtlas atlas;
    };
    static std::vector<std::uint8_t> fontPixels(const LLImageRaw& raw)
    {
        const auto components = raw.getComponents();
        check(components == 2 || components == 4, "Invalid native font atlas components");
        const unsigned            width = raw.getWidth(), height = raw.getHeight();
        std::vector<std::uint8_t> pixels(std::size_t(width) * height * 4);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x)
            {
                const auto* source = raw.getData() + (std::size_t(height - 1 - y) * width + x) * components;
                auto*       target = pixels.data() + (std::size_t(y) * width + x) * 4;
                target[0]          = source[0];
                target[1]          = components == 2 ? source[0] : source[1];
                target[2]          = components == 2 ? source[0] : source[2];
                target[3]          = components == 2 ? source[1] : source[3];
            }
        return pixels;
    }
    std::map<LLImageRaw*,FontPage> font_pages;
    std::array<float,4> clip{};
    unsigned                       deferred_draw_batches = 0;
    bool active=false;
    explicit Impl(VSUIRenderer& value) : renderer(value)
    {
        std::ifstream file(gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS,"std_bump.ini"));
        std::string magic,version,label,id;unsigned number=0;
        check(bool(file>>magic>>version>>number) && magic=="LLStandardBumpmap" && version=="version" && number==1,
              "Native standard bump configuration is missing or invalid");
        while(file>>label)
        {
            check(bool(file>>id),"Incomplete native standard bump source record");
            LLUUID source;
            check(standardBumpCount<standardBumps.size() && LLUUID::parseUUID(id,&source) && source.notNull(),
                  "Invalid native standard bump source");
            standardBumps[standardBumpCount++]=source;
        }
        check(file.eof(),"Invalid native standard bump configuration record");
    }
    void drawRotated(const Asset& a,S32 x,S32 y,S32 width,S32 height,F32 degrees,
                     const LLColor4& color,const LLRectf& uv)
    {
        check(std::isfinite(degrees), "Non-finite native UI image rotation");
        const auto first = packets.size();
        draw(a,x,y,width,height,color,false,uv,LLRectf(0.f,1.f,1.f,0.f),true,degrees == 0.f);
        const auto origin = LLRender2D::isNativeUI() ? LLRender2D::nativeOrigin() : std::array<F32,2>{0,0};
        // Preserve the GL helper's centre and counter-clockwise bottom-left
        // rotation after canonicalizing to the renderer's top-left space.
        const float cx = x + origin[0] + float(width/2);
        const float cy = logical_height - y - origin[1] - float(height/2);
        const float radians = degrees * DEG_TO_RAD;
        const float c = std::cos(radians), sn = std::sin(radians);
        for (auto i = first; i < packets.size(); ++i)
            packets[i].transform = {c,-sn,sn,c,cx-c*cx-sn*cy,cy+sn*cx-c*cy};
    }
    void draw(const Asset& a,S32 x,S32 y,S32 width,S32 height,const LLColor4& color,
              bool solid,const LLRectf& outer,const LLRectf& center,bool inner,bool snap_extent=true)
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
            std::transform(color.mV, color.mV + 4, p.color.begin(), nativeVertexColor);
            // drawSolid uses the image alpha mask, with RGB replaced by tint.
            p.alpha_mask=solid;
            p.sampling=sampling;
            packets.push_back(std::move(p));
        };
        if (center.mLeft==0 && center.mRight==1 && center.mBottom==0 && center.mTop==1)
        {
            // The ordinary GL image helper rounds the physical width/height,
            // retaining its translated origin. Rotated quads retain their
            // original extents before rotation, matching the GL rotation path.
            const float w = snap_extent ? std::floor(width * dpi + .5f) / dpi : float(width);
            const float h = snap_extent ? std::floor(height * dpi + .5f) / dpi : float(height);
            quad(0,0,w,h,outer.mLeft,outer.mBottom,outer.mRight,outer.mTop);
            return;
        }
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
        // The GL skin helper rounds the inner nine-slice boundaries in physical
        // pixels after UI translation and scaling. Preserve that contract at
        // fractional UI scales, rather than interpolating borders across a
        // half-pixel seam. Outer bounds intentionally remain unrounded.
        auto snap = [this](float value, float offset)
        { return std::floor((offset + value) * dpi + .5f) / dpi - offset; };
        c.mLeft = snap(c.mLeft, x + origin[0]);
        c.mRight = snap(c.mRight, x + origin[0]);
        c.mBottom = snap(c.mBottom, y + origin[1]);
        c.mTop = snap(c.mTop, y + origin[1]);
        const float xs[]{0,c.mLeft,c.mRight,float(width)},ys[]{0,c.mBottom,c.mTop,float(height)};
        const float us[]{outer.mLeft,uv.mLeft,uv.mRight,outer.mRight};
        const float vs[]{outer.mBottom,uv.mBottom,uv.mTop,outer.mTop};
        for (unsigned row=0;row<3;++row) for (unsigned col=0;col<3;++col)
            quad(xs[col],ys[row],xs[col+1],ys[row+1],us[col],vs[row],us[col+1],vs[row+1]);
    }
};
VSUIResources::VSUIResources(VSUIRenderer& r) : mImpl(std::make_shared<Impl>(r)) {}
VSUIResources::~VSUIResources() = default;
VSUIRenderer::Image VSUIResources::defaultWorldImage()
{
    // Transpose LLViewerTextureManager::init / bindDefaultImage: known scene
    // geometry remains visible while its asynchronous texture is unavailable.
    const std::string key="native-default-world-texture";
    if (auto existing=image(key)) return existing;
    LLPointer<LLImageRaw> raw=new LLImageRaw(128,128,3);
    raw->clear(0x7f,0x7f,0x7f,0xff);
    publish(key,*raw);
    return image(key);
}
VSUIResources::PreparedImage VSUIResources::prepareImage(const LLImageRaw& raw,VSWorldContracts::ImageRole role,bool compress)
{
    const auto width=raw.getWidth();const auto height=raw.getHeight();const auto components=raw.getComponents();
    check(width>0 && height>0 && width<=16384 && height<=16384 &&
          components>=1 && components<=4 && raw.getData(),"Invalid native UI CPU image");
    std::vector<std::uint8_t> bytes(std::size_t(width)*height*4);
    for(unsigned y=0;y<unsigned(height);++y) for(unsigned x=0;x<unsigned(width);++x)
    {
        const auto* s=raw.getData()+((std::size_t(height)-1-y)*width+x)*components;
        auto* d=bytes.data()+(std::size_t(y)*width+x)*4;
        d[0]=s[0]; d[1]=components<3?s[0]:s[1]; d[2]=components<3?s[0]:s[2];
        d[3]=components==2?s[1]:components==4?s[3]:255;
    }
    return VSTextureUpload::prepare(width,height,std::move(bytes),role,compress);
}
LLUUID VSUIResources::standardBumpSource(unsigned code) const
{return code>=3 && code<mImpl->standardBumpCount?mImpl->standardBumps[code]:LLUUID::null;}
void VSUIResources::clearWorldDerivedImages() {mImpl->bumps.clear();}
VSUIRenderer::Image VSUIResources::legacyBump(const LLUUID& id,unsigned code,float scale)
{
    if(id.isNull() || !code || code>=32) return {};
    const auto source=image("remote:"+vs_native_image_id(id).asString());
    if(!source || (code>=3 && VSUIRenderer::imageDescription(source).compression!=VSWorldContracts::ImageCompression::Uncompressed)) return {};
    const auto generation=VSUIRenderer::generation(source);
    const unsigned components=unsigned(vs_ui_image_components(id));
    const unsigned channel=components==1?0:components==3?2:3;
    const auto key=std::make_pair(id,code);
    auto found=mImpl->bumps.find(key);
    if(found!=mImpl->bumps.end())
    {
        found->second.touched=++mImpl->bumpClock;
        if(found->second.sourceGeneration==generation && found->second.scale==scale && found->second.heightChannel==channel)
            return found->second.image;
    }
    if(found==mImpl->bumps.end() && mImpl->bumps.size()>=1024)
    {
        auto victim=mImpl->bumps.end();
        for(auto item=mImpl->bumps.begin();item!=mImpl->bumps.end();++item)
            if(item->second.image.use_count()==1 && (victim==mImpl->bumps.end() || item->second.touched<victim->second.touched)) victim=item;
        if(victim==mImpl->bumps.end()) return {};
        mImpl->bumps.erase(victim);
    }
    auto generated=mImpl->renderer.generateBump(source,code,scale,channel);
    while(!generated)
    {
        auto victim=mImpl->bumps.end();
        for(auto item=mImpl->bumps.begin();item!=mImpl->bumps.end();++item)
            if(item->second.image.use_count()==1 &&
               (victim==mImpl->bumps.end() || item->second.touched<victim->second.touched)) victim=item;
        if(victim!=mImpl->bumps.end()) mImpl->bumps.erase(victim);
        else if(!vs_evict_world_image(id)) return {};
        generated=mImpl->renderer.generateBump(source,code,scale,channel);
    }
    if(!generated) return {}; // Keep the complete prior face publication on refusal.
    mImpl->bumps.insert_or_assign(key,Impl::BumpEntry{generated,generation,++mImpl->bumpClock,scale,channel});
    return generated;
}
bool VSUIResources::supportsCompression() const { return mImpl->renderer.supportsCompression(); }
std::shared_ptr<const VSResourceBudget::Reservation> VSUIResources::reserveImagePreparation(std::size_t bytes)
{ return mImpl->imagePreparationBytes.reserve(bytes); }
LLUIImagePtr VSUIResources::publish(const std::string& key,const LLImageRaw& raw)
{
    const auto role=imageRole(key);
    return publishPrepared(key,prepareImage(raw,role,false));
}
LLUIImagePtr VSUIResources::publishPrepared(const std::string& key,const PreparedImage& prepared)
{
    check(!key.empty() && prepared && VSTextureUpload::valid(*prepared),"Invalid prepared UI image publication");
    check(imageRole(key)==prepared->role,"Prepared native image has the wrong resource role");
    auto entry=mImpl->prepareEntry(mImpl,key,prepared);
    if(!entry.asset) return nullptr;
    const auto facade=entry.facade;
    mImpl->assets.insert_or_assign(key,std::move(entry));
    return facade;
}
std::vector<LLUIImagePtr> VSUIResources::publishBatch(const std::vector<std::pair<std::string,PreparedImage>>& inputs)
{
    std::map<std::string,Impl::Entry> staged;
    std::vector<LLUIImagePtr> facades;facades.reserve(inputs.size());
    for(const auto& [key,prepared]:inputs)
    {
        check(!key.empty() && prepared && VSTextureUpload::valid(*prepared) && imageRole(key)==prepared->role,
              "Invalid native image batch publication");
        auto entry=mImpl->prepareEntry(mImpl,key,prepared);
        if(!entry.asset) return {};
        facades.push_back(entry.facade);
        check(staged.emplace(key,std::move(entry)).second,"Duplicate native batch image identity");
    }
    // All allocations and uploads precede publication. Existing nodes are
    // replaced without allocation; new map nodes transfer from the staged map.
    for(auto found=staged.begin();found!=staged.end();)
    {
        if(auto old=mImpl->assets.find(found->first);old!=mImpl->assets.end())
        {old->second=std::move(found->second);found=staged.erase(found);}
        else ++found;
    }
    mImpl->assets.merge(staged);
    return facades;
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
    facade->setNativeRotatedDraw([owner,asset,sampling](S32 x,S32 y,S32 w,S32 h,F32 degrees,
                                                       const LLColor4& color,const LLRectf& clip)
    {
        auto canvas=owner.lock(); check(bool(canvas),"Native UI image owner has retired");
        const auto previous=canvas->sampling; canvas->sampling=sampling;
        try { canvas->drawRotated(*asset,x,y,w,h,degrees,color,clip); }
        catch (...) { canvas->sampling=previous; throw; }
        canvas->sampling=previous;
    });
    facade->setClipRegion(uv);
    return facade;
}
bool VSUIResources::patch(const std::string& key,unsigned x,unsigned y,unsigned width,unsigned height,
                         const std::vector<std::uint8_t>& rgba)
{
    auto found=mImpl->assets.find(key); check(found!=mImpl->assets.end(),"Unknown native UI image patch");
    auto& a=*found->second.asset;
    check(width && height && x<=a.width && y<=a.height && width<=a.width-x && height<=a.height-y &&
          rgba.size()==std::size_t(width)*height*4,"Invalid native UI image patch");
    auto image=mImpl->renderer.replace(a.image,x,y,width,height,rgba);
    if (!image) return false;
    a.image=std::move(image);
    found->second.facade->onImageLoaded();
    return true;
}
void VSUIResources::begin(unsigned width,unsigned height,float dpi)
{
    check(!mImpl->active && width && height && std::isfinite(dpi) && dpi>0,"Invalid native UI collection begin");
    mImpl->packets.clear();
    mImpl->deferred_draw_batches = 0;
    mImpl->logical_height        = height / dpi;
    mImpl->logical_width         = width / dpi;
    mImpl->dpi=dpi;
    mImpl->clip={0,0,width/dpi,height/dpi}; mImpl->active=true;
}
void VSUIResources::setClip(std::array<float,4> clip)
{
    check(mImpl->active,"Native UI clip outside collection"); mImpl->clip=clip;
}
std::vector<VSUIRenderer::Packet> VSUIResources::finish()
{
    check(mImpl->active, "Native UI collection is not active");
    // LLFontBitmapCache appends glyphs without moving earlier cells. Packet
    // collection can therefore coalesce additions to each page into one update
    // before any of this frame's glyphs are drawn. Cache reset uses a new raw
    // page; retained packets keep the old atlas alive through GPU completion.
    for (auto& entry : mImpl->font_pages)
    {
        auto& page = entry.second;
        if (page.atlas && page.generation != page.pending_generation)
        {
            mImpl->renderer.updateFontAtlas(page.atlas, Impl::fontPixels(*page.raw));
            page.generation = page.pending_generation;
        }
    }
    mImpl->active = false;
    return std::move(mImpl->packets);
}
void VSUIResources::erase(const std::string& key) { mImpl->assets.erase(key); }
#if defined(VS_VULKAN_DIAGNOSTICS) && VS_VULKAN_DIAGNOSTICS
std::vector<std::uint8_t> VSUIResources::readbackImage(const VSUIRenderer::Image& image)
{return mImpl->renderer.readbackImage(image);}
std::vector<std::uint8_t> VSUIResources::readbackImage(const std::string& key)
{
    return mImpl->renderer.readbackImage(image(key));
}
#endif
LLUIImagePtr VSUIResources::facade(const std::string& key) const
{
    const auto found = mImpl->assets.find(key);
    return found == mImpl->assets.end() ? LLUIImagePtr{} : found->second.facade;
}
VSUIRenderer::Image VSUIResources::image(const std::string& key) const
{
    const auto found=mImpl->assets.find(key);
    return found==mImpl->assets.end() ? VSUIRenderer::Image{} : found->second.asset->image;
}
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
    if (!page.atlas ||
        VSUIRenderer::imageExtent(page.atlas) != std::array<unsigned, 2>{ unsigned(raw->getWidth()), unsigned(raw->getHeight()) })
    {
        // Allocation failure is a renderer error, never a successful textless
        // frame. Ordinary glyph additions do not allocate another GPU atlas.
        auto atlas      = c.renderer.createFontAtlas(raw->getWidth(), raw->getHeight(), Impl::fontPixels(*raw));
        page.atlas      = std::move(atlas);
        page.generation = generation;
        c.assets[page.key].asset =
            std::make_shared<Impl::Asset>(Impl::Asset{ unsigned(raw->getWidth()), unsigned(raw->getHeight()), page.atlas });
    }
    page.pending_generation = generation;
    auto image              = page.atlas;
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
        LLPointer<LLImageRaw> raw = new LLImageRaw(1, 1, 4);
        raw->clear(255, 255, 255, 255);
        if (!publish(key, *raw))
        {
            ++c.deferred_draw_batches;
            return;
        }
    }
    VSUIRenderer::Packet p; p.blend=nativeBlend();p.image=c.assets.at(key).asset->image;
    p.bounds={r.mLeft/c.dpi,c.logical_height-r.mTop/c.dpi,r.mRight/c.dpi,c.logical_height-r.mBottom/c.dpi};
    p.clip=c.clip;std::transform(color.mV, color.mV + 4, p.color.begin(), nativeVertexColor);c.packets.push_back(std::move(p));
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
        (*p.triangles)[i] = {v[0]/c.dpi, c.logical_height-v[1]/c.dpi, 0.f, 0.f, nativeVertexColor(v[2]),nativeVertexColor(v[3]),nativeVertexColor(v[4]),nativeVertexColor(v[5])};
    }
    c.packets.push_back(std::move(p));
}

LLPointer<LLImageRaw> VSUIResources::rawImage(const std::string& key) const
{
    const auto found = mImpl->assets.find(key);
    if (found == mImpl->assets.end()) return nullptr;
    const auto& asset = *found->second.asset;
    if(VSUIRenderer::imagePixels(asset.image).size()!=std::size_t(asset.width)*asset.height*4) return nullptr;
    LLPointer<LLImageRaw> raw = new LLImageRaw(asset.width, asset.height, 4);
    const auto stride = asset.width * 4;
    for (unsigned y = 0; y < asset.height; ++y)
        std::copy_n(VSUIRenderer::imagePixels(asset.image).data() + y * stride, stride, raw->getData() + (asset.height - 1 - y) * stride);
    return raw;
}
void VSUIResources::updateTextureMemory(const VSTextureMemory::Settings& settings,VSTextureMemory::Input input)
{
    const auto stats=mImpl->renderer.statistics();
    input.gpuBytes=stats.residentGPUImageBytes;
    const double limit=double(stats.remoteImageBudgetBytes);
    const bool refused=stats.remoteImageAllocationRefusals!=mImpl->lastRemoteRefusals;
    if(refused)
    {
        mImpl->remotePressure=true;
        mImpl->remotePressureReleaseBytes=double(stats.remoteImagePayloadBytes)*.8;
    }
    mImpl->lastRemoteRefusals=stats.remoteImageAllocationRefusals;
    if(!refused && (!stats.remoteImagePayloadBytes || (limit>0 &&
        double(stats.remoteImagePayloadBytes)<std::min(limit*.7,mImpl->remotePressureReleaseBytes))))
        mImpl->remotePressure=false;
    // Preserve existing admission bounds as a distinct native pressure signal;
    // neither CPU snapshots nor leased uploads become invented VRAM usage.
    input.admissionOver=limit>0?(double(stats.remoteImagePayloadBytes)-limit*.8)/(limit*.8):-1;
    if(mImpl->remotePressure) input.admissionOver=std::max(input.admissionOver,.05);
    mImpl->textureMemory.update(settings,input);
}
const VSTextureMemory::Policy& VSUIResources::textureMemoryPolicy() const {return mImpl->textureMemory;}
unsigned VSUIResources::worldImageDimension(unsigned requested,unsigned maxDiscard,bool exact) const
{return exact?requested:mImpl->textureMemory.dimension(requested,maxDiscard);}

LLSD VSUIResources::evidence() const
{
    const auto stats=mImpl->renderer.statistics();const auto caps=mImpl->renderer.capabilities();
    LLSD result;result["image_generations"]=S32(mImpl->assets.size());
    result["texture_cache"] = vs_ui_texture_cache_evidence();
    const auto& memory=mImpl->textureMemory;
    result["texture_memory"]["budget_mb"]=memory.budgetMiB;result["texture_memory"]["target_mb"]=memory.targetMiB;
    result["texture_memory"]["estimated_used_mb"]=memory.usedMiB;result["texture_memory"]["free_mb"]=memory.freeMiB;
    result["texture_memory"]["discard_bias"]=memory.bias;
    result["remote_image_payload_bytes"]=ll_sd_from_U64(stats.remoteImagePayloadBytes);
    result["remote_image_payload_budget_bytes"]=ll_sd_from_U64(stats.remoteImageBudgetBytes);
    result["deferred_draw_batches"]                                                            = S32(mImpl->deferred_draw_batches);
    result["resident_image_payload_bytes"]=ll_sd_from_U64(stats.residentImageBytes);
    result["resident_gpu_image_payload_bytes"]=ll_sd_from_U64(stats.residentGPUImageBytes);
    result["resident_image_snapshot_bytes"]=ll_sd_from_U64(stats.residentImageSnapshotBytes);
    result["image_payload_budget_bytes"]=ll_sd_from_U64(stats.imageBudgetBytes);
    result["image_allocation_refusals"]=ll_sd_from_U64(stats.imageAllocationRefusals);
    result["image_preparation_bytes"]=ll_sd_from_U64(mImpl->imagePreparationBytes.used());
    result["image_preparation_budget_bytes"]=ll_sd_from_U64(mImpl->imagePreparationBytes.limit());
    result["cached_bump_generations"]=S32(mImpl->bumps.size());
    result["standard_bump_sources"]=S32(mImpl->standardBumpCount-3);
    result["maximum_image_dimension"]=S32(caps.maximumImageDimension);
    result["rgba8"]=caps.rgba8;result["bgra8"]=caps.bgra8;result["depth32"]=caps.depth32;
    return result;
}
