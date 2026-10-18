// LLImageGL's alpha histogram, shared by native worker-prepared textures.
// LGPL-2.1, like llimagegl.cpp.
#pragma once
#include <cstdint>
#include <cstddef>
#include <algorithm>

inline bool vs_image_alpha_mask(const void* data,unsigned width,unsigned height,unsigned stride,unsigned offset)
{
    if(!data || !width || !height || offset>=stride)return false;
    const auto* bytes=static_cast<const std::uint8_t*>(data)+offset;
    std::uint32_t length=width*height,total=0,sample[16]{};
    if(width>=2 && height>=2)
    {
        // Identical raw and 2x2 box histograms to LLImageGL. Partial edge
        // blocks also permit legal non-power-of-two native source images.
        for(unsigned y=0;y<height;y+=2)for(unsigned x=0;x<width;x+=2)
        {
            const unsigned columns=std::min(2u,width-x),rows=std::min(2u,height-y),count=columns*rows;
            std::uint32_t sum=0;
            for(unsigned dx=0;dx<columns;++dx)for(unsigned dy=0;dy<rows;++dy)
            {
                const auto value=bytes[(std::size_t(y+dy)*width+x+dx)*stride];
                sum+=value;++sample[value/16];
            }
            total+=sum;total+=sum;
            sample[sum/(16*count)]+=count;
        }
        length*=2;
    }
    else
        for(std::uint32_t index=0;index<length;++index)
        {const auto value=bytes[std::size_t(index)*stride];total+=value;++sample[value/16];}
    std::uint32_t middle=0,lower=0,upper=0;
    for(unsigned i=2;i<13;++i)middle+=sample[i];
    for(unsigned i=0;i<8;++i)lower+=sample[i];
    for(unsigned i=8;i<16;++i)upper+=sample[i];
    return !(middle>length/48 || (lower==length && total!=0) || (upper==length && total!=255*length));
}
