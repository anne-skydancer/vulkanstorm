// Worker-prepared native image mip chains and optional BC3 payloads. LGPL-2.1.
#pragma once
#include "vsimagemips.h"
#include "vsworldcontracts.h"
#include "third_party/stb/stb_dxt.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
namespace VSTextureUpload
{
struct Level
{
    unsigned width=0,height=0,rowBytes=0;
    std::vector<std::uint8_t> bytes;
};
struct Prepared
{
    unsigned width=0,height=0;
    bool compressed=false;
    bool compressionRequested=false;
    VSWorldContracts::ImageRole role=VSWorldContracts::ImageRole::CoreUI;
    std::size_t gpuBytes=0;
    std::vector<std::uint8_t> pixels;
    std::vector<Level> levels;
};
using Image=std::shared_ptr<const Prepared>;
inline bool valid(const Prepared& value)
{
    if(!value.width || !value.height || value.width>16384 || value.height>16384 ||
       value.pixels.size()!=std::size_t(value.width)*value.height*4 || value.levels.empty() ||
       (value.compressed && value.role!=VSWorldContracts::ImageRole::RemoteAsset)) return false;
    const bool mipmaps=value.role==VSWorldContracts::ImageRole::RemoteAsset || value.role==VSWorldContracts::ImageRole::AvatarComposite;
    unsigned width=value.width,height=value.height;std::size_t bytes=0;
    for(std::size_t index=0;index<value.levels.size();++index)
    {
        const auto& level=value.levels[index];
        const unsigned stride=value.compressed?((width+3)/4)*16:width*4;
        const auto size=std::size_t(stride)*(value.compressed?(height+3)/4:height);
        if(level.width!=width || level.height!=height || level.rowBytes!=stride || level.bytes.size()!=size) return false;
        bytes+=size;
        if(!mipmaps || (width==1 && height==1)) return index+1==value.levels.size() && bytes==value.gpuBytes;
        width=std::max(width/2,1u);height=std::max(height/2,1u);
    }
    return false;
}
inline std::size_t bc3Bytes(unsigned width,unsigned height)
{ return std::size_t((width+3)/4)*((height+3)/4)*16; }
inline std::vector<std::uint8_t> bc3(unsigned width,unsigned height,const std::vector<std::uint8_t>& rgba)
{
    if(!width || !height || width>16384 || height>16384 || rgba.size()!=std::size_t(width)*height*4)
        throw std::invalid_argument("Invalid BC3 image extent or bytes");
    std::vector<std::uint8_t> output(bc3Bytes(width,height));
    for(unsigned y=0;y<height;y+=4) for(unsigned x=0;x<width;x+=4)
    {
        alignas(16) std::array<std::uint8_t,64> block;
        for(unsigned by=0;by<4;++by) for(unsigned bx=0;bx<4;++bx)
        {
            const auto input=(std::size_t(std::min(y+by,height-1))*width+std::min(x+bx,width-1))*4;
            std::copy_n(rgba.data()+input,4,block.data()+(by*4+bx)*4);
        }
        const auto offset=(std::size_t(y/4)*((width+3)/4)+x/4)*16;
        stb_compress_dxt_block(output.data()+offset,block.data(),1,STB_DXT_HIGHQUAL);
    }
    return output;
}
inline Image prepare(unsigned width,unsigned height,std::vector<std::uint8_t> pixels,
                     VSWorldContracts::ImageRole role,bool compress)
{
    auto result=std::make_shared<Prepared>();
    result->width=width;result->height=height;result->role=role;
    // UI, font, media and locally composited avatar resources retain their
    // exact-pixel representation. Compression requires an explicit world request.
    result->compressionRequested=compress;
    result->compressed=compress && role==VSWorldContracts::ImageRole::RemoteAsset;
    result->pixels=std::move(pixels);
    const bool mipmaps=role==VSWorldContracts::ImageRole::RemoteAsset || role==VSWorldContracts::ImageRole::AvatarComposite;
    auto levels=VSImageMips::build(width,height,result->pixels,mipmaps);
    std::size_t encodedBytes=0;
    for(const auto& level:levels) encodedBytes+=bc3Bytes(level.width,level.height);
    if(encodedBytes>=VSImageMips::bytes(width,height,mipmaps)) result->compressed=false;
    for(auto& level:levels)
    {
        Level output;output.width=level.width;output.height=level.height;
        output.rowBytes=result->compressed?((level.width+3)/4)*16:level.width*4;
        output.bytes=result->compressed?bc3(level.width,level.height,level.pixels):std::move(level.pixels);
        result->gpuBytes+=output.bytes.size();result->levels.push_back(std::move(output));
    }
    return result;
}
}
