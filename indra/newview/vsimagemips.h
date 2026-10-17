// Immutable bounded RGBA mip chains shared by native image publication. LGPL-2.1.
#pragma once
#include <vector>
#include <utility>
#include <cstdint>
#include <algorithm>
namespace VSImageMips
{
struct Level {unsigned width=0,height=0;std::vector<std::uint8_t> pixels;};
inline std::vector<Level> build(unsigned width,unsigned height,const std::vector<std::uint8_t>& pixels,bool mipmaps)
{
    if(!width || !height || pixels.size()!=std::size_t(width)*height*4) return {};
    std::vector<Level> levels{{width,height,pixels}};
    while(mipmaps && (width>1 || height>1))
    {
        const unsigned w=std::max(1u,width/2),h=std::max(1u,height/2);
        Level next{w,h,std::vector<std::uint8_t>(std::size_t(w)*h*4)};
        const auto& previous=levels.back().pixels;
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) for(unsigned c=0;c<4;++c)
        {
            // Area weighting covers odd dimensions rather than dropping the
            // last row/column, and matches unorm texture filtering semantics.
            const unsigned x0=x*width/w,x1=(x+1)*width/w,y0=y*height/h,y1=(y+1)*height/h;
            unsigned sum=0,count=0;
            for(unsigned py=y0;py<y1;++py) for(unsigned px=x0;px<x1;++px)
            {sum+=previous[(std::size_t(py)*width+px)*4+c];++count;}
            next.pixels[(std::size_t(y)*w+x)*4+c]=std::uint8_t((sum+count/2)/count);
        }
        levels.push_back(std::move(next));width=w;height=h;
    }
    return levels;
}
inline std::size_t bytes(unsigned width,unsigned height,bool mipmaps)
{
    std::size_t total=std::size_t(width)*height*4;
    while(mipmaps && (width>1 || height>1)) {width=std::max(1u,width/2);height=std::max(1u,height/2);total+=std::size_t(width)*height*4;}
    return total;
}
}
