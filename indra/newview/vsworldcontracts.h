// Backend-independent native producer/resource contracts. LGPL-2.1.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace VSWorldContracts
{
inline std::array<float,4> regionOffset(std::uint64_t origin,std::uint64_t region)
{
    // Region handles contain unsigned global metre coordinates. Subtract in
    // integer space so nearby regions retain precision at large global origins.
    return {float(std::int64_t(std::uint32_t(region>>32))-std::int64_t(std::uint32_t(origin>>32))),
            float(std::int64_t(std::uint32_t(region))-std::int64_t(std::uint32_t(origin))),0,0};
}
enum class ResourceKind { Geometry, Image, Material, Animation, SceneAsset, Media };
enum class OwnerKind { Account, Region, Object, Face, Avatar, ControlAvatar, Attachment, Inventory, Task, Interaction };
enum class ImageOrigin { TopLeft, BottomLeft };
enum class ColorEncoding { EncodedSRGB, Linear, Data };
enum class AlphaMode { Opaque, Mask, Blend };
enum class ImageCompression { None, BC3 };
enum class ImageRole
{
    CoreUI,
    RemoteAsset,
    AvatarComposite,
    Media
};
enum class AddressMode { Clamp, Repeat, Mirror };
enum class Filter { Nearest, Linear };
struct Sampler
{
    Filter minification=Filter::Linear,magnification=Filter::Linear;
    AddressMode u=AddressMode::Repeat,v=AddressMode::Repeat;
    bool mipmaps=false,trilinear=true;
    unsigned anisotropy=1;
    bool operator==(const Sampler& rhs) const {return minification==rhs.minification && magnification==rhs.magnification && u==rhs.u && v==rhs.v && mipmaps==rhs.mipmaps && trilinear==rhs.trilinear && anisotropy==rhs.anisotropy;}
};
struct Image
{
    unsigned width=0,height=0,components=4;
    ImageOrigin origin=ImageOrigin::TopLeft;
    ColorEncoding encoding=ColorEncoding::EncodedSRGB;
    AlphaMode alpha=AlphaMode::Blend;
    ImageRole role=ImageRole::CoreUI;
    std::uint64_t generation=0;
    unsigned mipLevels=1;
    ImageCompression compression=ImageCompression::None;
    std::size_t gpuBytes=0;
};
// Existing producers publish indexed region-local position/normal/UV0.
// Optional channels are explicit schema requirements for later producers;
// a missing requested channel is not silently synthesized by this contract.
enum Attribute : unsigned { Position=1,Normal=2,UV0=4,Tangent=8,Color=16,JointIndices=32,JointWeights=64,UV1=128 };
struct Geometry
{
    unsigned attributes=Position|Normal|UV0;
    std::size_t vertices=0,indices=0,stride=32;
    std::uint64_t generation=0;
};
struct Material
{
    std::array<float,4> diffuse{1,1,1,1};
    AlphaMode alpha=AlphaMode::Opaque;
    bool fullbright=false,doubleSided=false;
    bool diffuseImageKnown=false;
    Sampler sampler;
    ColorEncoding diffuseEncoding=ColorEncoding::EncodedSRGB;
    std::uint64_t generation=0;
};
struct Capabilities
{
    unsigned maximumImageDimension=0;
    bool rgba8=false,bgra8=false,depth32=false;
    bool nativeVulkan=true;
};
}
