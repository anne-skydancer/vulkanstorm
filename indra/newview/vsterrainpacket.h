// Bounded simulator terrain bitstream decoder. LGPL-2.1.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
namespace VSTerrain
{
struct Patch
{
    unsigned x=0,y=0,size=0,quant=0,range=0;
    float offset=0;
    std::array<int,1024> coefficients{};
};
// LLBitPack stores each byte MSB first, but multi-byte fields in little-endian
// byte chunks (including the final partial byte). Check every read before use.
class Bits
{
    const std::vector<std::uint8_t>& data;
    std::size_t cursor=0;
public:
    explicit Bits(const std::vector<std::uint8_t>& d):data(d) {}
    bool read(unsigned count,std::uint32_t& value)
    {
        if (count>32 || count>data.size()*8-cursor) return false;
        value=0;
        for (unsigned shift=0;count;shift+=8)
        {
            const unsigned chunk=count>8?8:count;unsigned byte=0;
            for (unsigned i=0;i<chunk;++i,++cursor) byte=(byte<<1)|((data[cursor/8]>>(7-cursor%8))&1);
            value|=byte<<shift;count-=chunk;
        }
        return true;
    }
};
inline bool decode(const std::vector<std::uint8_t>& data,unsigned type,std::vector<Patch>& output,
                   unsigned width=256,unsigned height=256)
{
    const bool wind = type == '7' || type == '9';
    if (data.size() < 5 || data.size() > 65535 || (type != 'L' && type != 'M' && !wind) || width < 16 || height < 16 || width > 65536 ||
        height > 65536)
        return false;
    Bits bits(data);std::uint32_t stride,size,layer;
    if (!bits.read(16,stride) || !bits.read(8,size) || !bits.read(8,layer) ||
        layer!=type || (size!=16 && size!=32) || stride<size) return false;
    if (wind && size != 16)
        return false;
    std::vector<Patch> patches;
    for (unsigned n=0;n<=256;++n)
    {
        std::uint32_t quant,raw,range,id;
        if (!bits.read(8,quant)) return false;
        if (quant == 97)
        {
            if (wind && patches.size() != 2)
                return false;
            output = std::move(patches);
            return true;
        }
        if (n==256 || !bits.read(32,raw) || !bits.read(16,range) || !bits.read(type=='M'?32:10,id)) return false;
        Patch p;p.size=size;p.quant=quant;p.range=range;std::memcpy(&p.offset,&raw,4);
        p.x=type=='M'?id>>16:id>>5;p.y=type=='M'?id&65535:id&31;
        if ((!wind && (p.x >= width / size || p.y >= height / size)) || !std::isfinite(p.offset) || std::abs(p.offset) > 65536)
            return false;
        const unsigned width=(quant&15)+2;
        for (unsigned i=0;i<size*size;++i)
        {
            std::uint32_t present,value,sign;
            if (!bits.read(1,present)) return false;
            if (!present) continue;
            if (!bits.read(1,value)) return false;
            if (!value) break; // End of coefficients; remaining entries are zero.
            if (!bits.read(1,sign) || !bits.read(width,value)) return false;
            p.coefficients[i]=sign?-int(value):int(value);
        }
        patches.push_back(std::move(p));
        if (wind && patches.size() == 2)
        {
            output = std::move(patches);
            return true;
        }
    }
    return false;
}
}
