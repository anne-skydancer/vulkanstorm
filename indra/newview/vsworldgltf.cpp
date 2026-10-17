// Native glTF scene ownership and CPU animation. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "llviewercontrol.h"
#include "vsworldgltf.h"
#include "vsuiresources.h"
#include "gltf/asset.h"
#include "llassetstorage.h"
#include "llfilesystem.h"
#include "llappviewer.h"
#include "llsd.h"
#include "llsdutil.h"
#include "llui.h"
#include "llimage.h"
#include "lldir.h"
#include "lluri.h"
#include "llbase64.h"
#include "llframetimer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <new>
#include <boost/container/pmr/memory_resource.hpp>
using namespace LL::GLTF;
namespace
{
constexpr std::size_t maximumBytes = 64 * 1024 * 1024;
// JSON text length does not bound its DOM allocation. Charge each actual
// parser allocation before materializing it, and retain charges to deallocation.
class BudgetJSONResource final : public boost::container::pmr::memory_resource
{
    VSResourceBudget budget;
    struct Allocation
    {
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
        std::size_t                                          alignment;
    };
    std::map<void*, Allocation> allocations;
    std::size_t                 bytes = 0;
    void*                       do_allocate(std::size_t size, std::size_t alignment) override
    {
        if (size > maximumBytes - bytes)
            throw std::bad_alloc();
        auto residency = budget.reserve(size + sizeof(Allocation) + 64);
        if (!residency)
            throw std::bad_alloc();
        alignment     = std::max(alignment, alignof(std::max_align_t));
        void* pointer = ::operator new(size, std::align_val_t(alignment));
        try
        {
            allocations.emplace(pointer, Allocation{ std::move(residency), alignment });
        }
        catch (...)
        {
            ::operator delete(pointer, std::align_val_t(alignment));
            throw;
        }
        bytes += size;
        return pointer;
    }
    void do_deallocate(void* pointer, std::size_t size, std::size_t) override
    {
        const auto found = allocations.find(pointer);
        if (found == allocations.end())
            std::terminate();
        ::operator delete(pointer, std::align_val_t(found->second.alignment));
        bytes -= size;
        allocations.erase(found);
    }
    bool do_is_equal(const boost::container::pmr::memory_resource& other) const noexcept override { return this == &other; }

public:
    explicit BudgetJSONResource(VSResourceBudget value) : budget(std::move(value)) {}
    ~BudgetJSONResource() override
    {
        for (const auto& [pointer, allocation] : allocations)
            ::operator delete(pointer, std::align_val_t(allocation.alignment));
    }
};

// The typed decoder allocates C++ arrays/maps as it copies the DOM. Admit a
// conservative bound before that copy; raw buffer/channel expansion is admitted
// separately by validate(). Unknown JSON/extras remain charged by their resource.
std::size_t typedSceneBytes(const boost::json::value& root)
{
    const std::size_t objectBytes = 2 * std::max({ sizeof(Node), sizeof(Material), sizeof(Primitive), sizeof(Animation), sizeof(Skin),
                                                   sizeof(Accessor), sizeof(Scene), sizeof(Buffer), sizeof(Image) });
    std::size_t       bytes       = sizeof(Asset);
    auto              charge      = [&](std::size_t count)
    {
        if (count > maximumBytes - bytes)
            throw std::length_error("glTF typed metadata exceeds admission limit");
        bytes += count;
    };
    std::function<void(const boost::json::value&, unsigned)> visit = [&](const boost::json::value& value, unsigned depth)
    {
        if (depth > 256)
            throw std::length_error("glTF metadata nesting exceeds admission limit");
        if (value.is_object())
        {
            charge(objectBytes);
            for (const auto& item : value.as_object())
            {
                charge(2 * item.key().size() + 128); // copied strings/map nodes
                visit(item.value(), depth + 1);
            }
        }
        else if (value.is_array())
        {
            for (const auto& item : value.as_array())
            {
                charge(64); // numeric/string/vector slots, including capacity
                visit(item, depth + 1);
            }
        }
        else if (value.is_string())
            charge(2 * value.as_string().size() + 32);
    };
    visit(root, 0);
    return bytes;
}
template<class T>
bool index(S32 value, const std::vector<T>& values)
{ return value >= 0 && std::size_t(value) < values.size(); }
bool validate(const Asset& asset, std::size_t& bytes)
{
    if (asset.mVersion != "2.0" || asset.mNodes.size() > 4096 || asset.mMeshes.size() > 4096 || asset.mBuffers.size() > 64 ||
        asset.mAccessors.size() > 65536 || asset.mScenes.empty())
        return false;
    bytes = 0;
    for (const auto& buffer : asset.mBuffers)
    {
        if (buffer.mByteLength <= 0 || std::size_t(buffer.mByteLength) > maximumBytes - bytes)
            return false;
        bytes += buffer.mByteLength;
    }
    for (const auto& view : asset.mBufferViews)
        if (!index(view.mBuffer, asset.mBuffers) || view.mByteOffset < 0 || view.mByteLength <= 0 || view.mByteStride < 0 ||
            U64(view.mByteOffset) + U64(view.mByteLength) > U64(asset.mBuffers[view.mBuffer].mByteLength))
            return false;
    for (const auto& image : asset.mImages)
        if (image.mBufferView != INVALID_INDEX && !index(image.mBufferView, asset.mBufferViews))
            return false;
    for (const auto& texture : asset.mTextures)
        if (texture.mSource != INVALID_INDEX && !index(texture.mSource, asset.mImages))
            return false;
    for (const auto& material : asset.mMaterials)
    {
        if (!std::isfinite(material.mAlphaCutoff) || material.mAlphaCutoff < 0 || material.mAlphaCutoff > 1)
            return false;
        const auto& factor = material.mPbrMetallicRoughness.mBaseColorFactor;
        for (unsigned channel = 0; channel < 4; ++channel)
            if (!std::isfinite(glm::value_ptr(factor)[channel]) || glm::value_ptr(factor)[channel] < 0 ||
                glm::value_ptr(factor)[channel] > 1)
                return false;
        const auto texture = material.mPbrMetallicRoughness.mBaseColorTexture.mIndex;
        if (texture != INVALID_INDEX && !index(texture, asset.mTextures))
            return false;
    }
    for (const auto& accessor : asset.mAccessors)
    {
        if (!index(accessor.mBufferView, asset.mBufferViews) || accessor.mByteOffset < 0 || accessor.mCount <= 0 ||
            accessor.mCount > 1048576)
            return false;
        unsigned component = 0;
        switch (accessor.mComponentType)
        {
            case Accessor::ComponentType::BYTE:
            case Accessor::ComponentType::UNSIGNED_BYTE:
                component = 1;
                break;
            case Accessor::ComponentType::SHORT:
            case Accessor::ComponentType::UNSIGNED_SHORT:
                component = 2;
                break;
            case Accessor::ComponentType::UNSIGNED_INT:
            case Accessor::ComponentType::FLOAT:
                component = 4;
                break;
            default:
                return false;
        }
        unsigned count = 0;
        switch (accessor.mType)
        {
            case Accessor::Type::SCALAR:
                count = 1;
                break;
            case Accessor::Type::VEC2:
                count = 2;
                break;
            case Accessor::Type::VEC3:
                count = 3;
                break;
            case Accessor::Type::VEC4:
                count = 4;
                break;
            case Accessor::Type::MAT2:
                count = 4;
                break;
            case Accessor::Type::MAT3:
                count = 9;
                break;
            case Accessor::Type::MAT4:
                count = 16;
                break;
            default:
                return false;
        }
        const auto& view  = asset.mBufferViews[accessor.mBufferView];
        const U64   width = component * count, stride = view.mByteStride ? U64(view.mByteStride) : width;
        if (stride < width || (U64(accessor.mByteOffset) + (U64(accessor.mCount) - 1) * stride + width) > U64(view.mByteLength))
            return false;
        // Bound all retained decoded channels and the tangent/normal expansion
        // before the shared decoder allocates them.
        const U64 expansion = U64(accessor.mCount) * 128;
        if (expansion > maximumBytes - bytes)
            return false;
        bytes += std::size_t(expansion);
    }
    for (const auto& mesh : asset.mMeshes)
    {
        if (mesh.mPrimitives.size() > 4096)
            return false;
        for (const auto& primitive : mesh.mPrimitives)
        {
            if ((primitive.mIndices != INVALID_INDEX && !index(primitive.mIndices, asset.mAccessors)) ||
                (primitive.mMaterial != INVALID_INDEX && !index(primitive.mMaterial, asset.mMaterials)))
                return false;
            if (!primitive.mAttributes.count("POSITION"))
                return false;
            for (const auto& [name, accessor] : primitive.mAttributes)
                if (!index(accessor, asset.mAccessors))
                    return false;
            const auto& position = asset.mAccessors[primitive.mAttributes.at("POSITION")];
            const U64   retained =
                U64(position.mCount) * 128 +
                (primitive.mIndices == INVALID_INDEX ? U64(position.mCount) : U64(asset.mAccessors[primitive.mIndices].mCount)) * 4;
            if (retained > maximumBytes - bytes)
                return false;
            bytes += std::size_t(retained);
            if (position.mType != Accessor::Type::VEC3 || position.mComponentType != Accessor::ComponentType::FLOAT)
                return false;
            for (const auto& [name, id] : primitive.mAttributes)
            {
                const auto& attribute = asset.mAccessors[id];
                if (attribute.mCount != position.mCount)
                    return false;
                if ((name == "NORMAL" && attribute.mType != Accessor::Type::VEC3) ||
                    ((name == "TANGENT" || name == "WEIGHTS_0" || name == "JOINTS_0") && attribute.mType != Accessor::Type::VEC4) ||
                    ((name == "TEXCOORD_0" || name == "TEXCOORD_1") && attribute.mType != Accessor::Type::VEC2))
                    return false;
                if ((name == "NORMAL" || name == "TANGENT" || name == "TEXCOORD_0" || name == "TEXCOORD_1") &&
                    attribute.mComponentType != Accessor::ComponentType::FLOAT)
                    return false;
                if ((name == "JOINTS_0" || name == "WEIGHTS_0" || name == "COLOR_0") &&
                    attribute.mComponentType != Accessor::ComponentType::UNSIGNED_BYTE &&
                    attribute.mComponentType != Accessor::ComponentType::UNSIGNED_SHORT &&
                    !(name != "JOINTS_0" && attribute.mComponentType == Accessor::ComponentType::FLOAT))
                    return false;
                if (name == "COLOR_0" && attribute.mType != Accessor::Type::VEC3 && attribute.mType != Accessor::Type::VEC4)
                    return false;
            }
            if (primitive.mIndices != INVALID_INDEX)
            {
                const auto& indices = asset.mAccessors[primitive.mIndices];
                if (indices.mType != Accessor::Type::SCALAR || (indices.mComponentType != Accessor::ComponentType::UNSIGNED_BYTE &&
                                                                indices.mComponentType != Accessor::ComponentType::UNSIGNED_SHORT &&
                                                                indices.mComponentType != Accessor::ComponentType::UNSIGNED_INT))
                    return false;
            }
        }
    }
    std::vector<unsigned> parents(asset.mNodes.size()), state(asset.mNodes.size());
    for (const auto& node : asset.mNodes)
    {
        for (const F32 value : { node.mTranslation.x, node.mTranslation.y, node.mTranslation.z, node.mRotation.x, node.mRotation.y,
                                 node.mRotation.z, node.mRotation.w, node.mScale.x, node.mScale.y, node.mScale.z })
            if (!std::isfinite(value))
                return false;
        for (unsigned component = 0; component < 16; ++component)
            if (!std::isfinite(glm::value_ptr(node.mMatrix)[component]))
                return false;
        if (glm::dot(node.mRotation, node.mRotation) < 1.e-12f)
            return false;
        if ((node.mMesh != INVALID_INDEX && !index(node.mMesh, asset.mMeshes)) ||
            (node.mSkin != INVALID_INDEX && !index(node.mSkin, asset.mSkins)))
            return false;
        for (S32 child : node.mChildren)
            if (!index(child, asset.mNodes) || ++parents[child] > 1)
                return false;
    }
    std::function<bool(S32, unsigned)> visit = [&](S32 node, unsigned depth)
    {
        if (depth > 256 || state[node] == 1)
            return false;
        if (state[node] == 2)
            return true;
        state[node] = 1;
        for (S32 child : asset.mNodes[node].mChildren)
            if (!visit(child, depth + 1))
                return false;
        state[node] = 2;
        return true;
    };
    for (S32 node = 0; std::size_t(node) < asset.mNodes.size(); ++node)
        if (!visit(node, 0))
            return false;
    for (const auto& scene : asset.mScenes)
        for (S32 root : scene.mNodes)
            if (!index(root, asset.mNodes) || parents[root])
                return false;
    if (asset.mScene != INVALID_INDEX && !index(asset.mScene, asset.mScenes))
        return false;
    for (const auto& skin : asset.mSkins)
    {
        if (skin.mJoints.empty() || skin.mJoints.size() > 256 ||
            (skin.mInverseBindMatrices != INVALID_INDEX && !index(skin.mInverseBindMatrices, asset.mAccessors)))
            return false;
        for (S32 joint : skin.mJoints)
            if (!index(joint, asset.mNodes))
                return false;
        if (skin.mInverseBindMatrices != INVALID_INDEX)
        {
            const auto& matrices = asset.mAccessors[skin.mInverseBindMatrices];
            if (matrices.mType != Accessor::Type::MAT4 || matrices.mComponentType != Accessor::ComponentType::FLOAT ||
                std::size_t(matrices.mCount) != skin.mJoints.size())
                return false;
        }
    }
    for (const auto& animation : asset.mAnimations)
    {
        for (const auto& sampler : animation.mSamplers)
            if (!index(sampler.mInput, asset.mAccessors) || !index(sampler.mOutput, asset.mAccessors))
                return false;
        auto channelsValid = [&](const auto& channels)
        {
            for (const auto& channel : channels)
            {
                if (!index(channel.mSampler, animation.mSamplers) || !index(channel.mTarget.mNode, asset.mNodes))
                    return false;
                const auto& sampler = animation.mSamplers[channel.mSampler];
                const auto& input   = asset.mAccessors[sampler.mInput];
                const auto& output  = asset.mAccessors[sampler.mOutput];
                if (input.mType != Accessor::Type::SCALAR || input.mComponentType != Accessor::ComponentType::FLOAT ||
                    output.mComponentType != Accessor::ComponentType::FLOAT ||
                    output.mType != (channel.mTarget.mPath == "rotation" ? Accessor::Type::VEC4 : Accessor::Type::VEC3))
                    return false;
            }
            return true;
        };
        if (!channelsValid(animation.mRotationChannels) || !channelsValid(animation.mTranslationChannels) ||
            !channelsValid(animation.mScaleChannels))
            return false;
    }
    return true;
}
bool validateData(Asset& asset)
{
    // Validate immutable encoded channels before primitive octrees, skin
    // palettes or animation controllers can consume them.
    for (auto& buffer : asset.mBuffers)
        if (!buffer.prep(asset))
            return false;
    for (const auto& accessor : asset.mAccessors)
    {
        if (accessor.mComponentType != Accessor::ComponentType::FLOAT)
            continue;
        unsigned components = 0;
        switch (accessor.mType)
        {
            case Accessor::Type::SCALAR:
                components = 1;
                break;
            case Accessor::Type::VEC2:
                components = 2;
                break;
            case Accessor::Type::VEC3:
                components = 3;
                break;
            case Accessor::Type::VEC4:
                components = 4;
                break;
            case Accessor::Type::MAT2:
                components = 4;
                break;
            case Accessor::Type::MAT3:
                components = 9;
                break;
            case Accessor::Type::MAT4:
                components = 16;
                break;
        }
        const auto&       view   = asset.mBufferViews[accessor.mBufferView];
        const auto&       bytes  = asset.mBuffers[view.mBuffer].mData;
        const std::size_t stride = view.mByteStride ? std::size_t(view.mByteStride) : components * sizeof(F32);
        const std::size_t base   = std::size_t(view.mByteOffset) + accessor.mByteOffset;
        if (base % 4 || stride % 4)
            return false;
        for (S32 item = 0; item < accessor.mCount; ++item)
            for (unsigned component = 0; component < components; ++component)
            {
                F32 value;
                std::memcpy(&value, bytes.data() + base + std::size_t(item) * stride + component * 4, 4);
                if (!std::isfinite(value))
                    return false;
            }
    }
    for (const auto& mesh : asset.mMeshes)
        for (const auto& primitive : mesh.mPrimitives)
        {
            if (primitive.mIndices == INVALID_INDEX)
                continue;
            const auto&    accessor = asset.mAccessors[primitive.mIndices];
            const auto&    view     = asset.mBufferViews[accessor.mBufferView];
            const auto&    bytes    = asset.mBuffers[view.mBuffer].mData;
            const unsigned size     = accessor.mComponentType == Accessor::ComponentType::UNSIGNED_BYTE    ? 1
                                      : accessor.mComponentType == Accessor::ComponentType::UNSIGNED_SHORT ? 2
                                                                                                           : 4;
            const auto     stride   = view.mByteStride ? std::size_t(view.mByteStride) : size;
            const auto     base     = std::size_t(view.mByteOffset) + accessor.mByteOffset;
            const U32      count    = U32(asset.mAccessors[primitive.mAttributes.at("POSITION")].mCount);
            for (S32 index = 0; index < accessor.mCount; ++index)
            {
                U32 vertex = 0;
                std::memcpy(&vertex, bytes.data() + base + std::size_t(index) * stride, size);
                if (vertex >= count)
                    return false;
            }
        }
    return true;
}
} // namespace
struct VSWorldGLTF::Impl : std::enable_shared_from_this<Impl>
{
    LLUUID                                               id;
    VSWorldRenderer&                                     renderer;
    VSUIResources&                                       resources;
    VSResourceBudget                                     budget;
    std::function<bool()>                                current;
    std::shared_ptr<const VSResourceBudget::Reservation> residency;
    std::shared_ptr<const VSResourceBudget::Reservation> typedResidency;
    std::shared_ptr<Asset>                               asset;
    std::string                                          error;
    std::set<LLUUID>                                     pendingBuffers;
    double                                               deadline         = 0;
    bool                                                 ready            = false;
    bool                                                 publicationKnown = false;
    LLUUID                                               imageOwner;
    std::map<unsigned, std::string>                      images;
    ~Impl()
    {
        for (const auto& [index, key] : images)
            resources.erase(key);
    }
    struct Publication
    {
        VSWorldRenderer::Mesh mesh;
        std::vector<float>    signature;
    };
    std::map<std::pair<unsigned, unsigned>, Publication> publications;
    std::vector<VSWorldRenderer::Packet>                 publishedPackets;
    struct Request
    {
        std::weak_ptr<Impl> owner;
    };
    Impl(const LLUUID& uuid, VSWorldRenderer& r, VSUIResources& u, VSResourceBudget b, std::function<bool()> guard) :
        id(uuid),
        renderer(r),
        resources(u),
        budget(std::move(b)),
        current(std::move(guard))
    { imageOwner.generate(); }
    void local(const std::string& filename)
    {
        auto working = budget.reserve(maximumBytes);
        if (!working)
        {
            error = "glTF file admission exhausted";
            return;
        }
        llifstream file(filename.c_str(), std::ios::binary | std::ios::ate);
        const auto length = file.tellg();
        if (!file || length <= 0 || U64(length) > maximumBytes)
        {
            error = "Invalid or oversized glTF file";
            return;
        }
        std::string contents(std::size_t(length), '\0');
        file.seekg(0);
        file.read(contents.data(), length);
        if (!file)
        {
            error = "glTF file read failed";
            return;
        }
        std::string_view json = contents;
        std::string_view binary;
        if (gDirUtilp->getExtension(filename) == "glb")
        {
            auto word = [&](std::size_t offset)
            {
                U32 value;
                std::memcpy(&value, contents.data() + offset, 4);
                return value;
            };
            if (contents.size() < 20 || word(0) != 0x46546c67 || word(4) != 2 || word(8) != contents.size() || word(16) != 0x4e4f534a)
            {
                error = "Invalid GLB header";
                return;
            }
            const U32 size = word(12);
            if (size > contents.size() - 20 || size % 4)
            {
                error = "Invalid GLB JSON chunk";
                return;
            }
            json                     = std::string_view(contents.data() + 20, size);
            const std::size_t offset = 20 + size;
            if (offset < contents.size())
            {
                if (contents.size() - offset < 8 || word(offset + 4) != 0x004e4942 || word(offset) > contents.size() - offset - 8)
                {
                    error = "Invalid GLB binary chunk";
                    return;
                }
                binary = std::string_view(contents.data() + offset + 8, word(offset));
            }
        }
        if (json.size() > 4 * 1024 * 1024)
        {
            error = "glTF JSON exceeds admission limit";
            return;
        }
        auto jsonStorage = boost::json::make_shared_resource<BudgetJSONResource>(budget);
        auto parsedJSON  = boost::json::parse(json, jsonStorage);
        typedResidency   = budget.reserve(typedSceneBytes(parsedJSON));
        if (!typedResidency)
        {
            error = "glTF typed decode admission exhausted";
            return;
        }
        asset                = std::make_shared<Asset>(parsedJSON);
        asset->mLoadIntoVRAM = false;
        asset->mFilename     = filename;
        std::size_t bytes    = 0;
        if (!validate(*asset, bytes))
        {
            error = "Invalid or oversized local glTF scene";
            asset.reset();
            return;
        }
        if (json.size() > maximumBytes - bytes)
        {
            error = "Local glTF scene exceeds retained admission limit";
            asset.reset();
            return;
        }
        residency = budget.reserve(bytes + json.size());
        if (!residency)
        {
            error = "Local glTF scene budget exhausted";
            asset.reset();
            return;
        }
        if (!asset->mBuffers.empty() && asset->mBuffers[0].mUri.empty())
        {
            const auto size = std::size_t(asset->mBuffers[0].mByteLength);
            if (size > binary.size())
            {
                error = "Truncated GLB buffer";
                return;
            }
            asset->mBuffers[0].mData.assign(binary.begin(), binary.begin() + size);
        }
        if (!validateData(*asset) || !asset->prep())
        {
            error = "Shared local glTF decoder rejected asset";
            return;
        }
        asset->updateTransforms();
        ready = true;
    }
    VSUIRenderer::Image image(unsigned index)
    {
        const auto& source = asset->mImages[index];
        LLUUID      uuid;
        if (LLUUID::parseUUID(source.mUri, &uuid) && uuid.notNull())
        {
            LLUI::getUIImageByID(uuid);
            return resources.image("remote:" + uuid.asString());
        }
        if (const auto found = images.find(index); found != images.end())
            return resources.image(found->second);
        auto working = budget.reserve(64 * 1024 * 1024);
        if (!working)
            return {};
        std::vector<U8> bytes;
        std::string     mime = source.mMimeType;
        if (source.mBufferView != INVALID_INDEX)
        {
            const auto& view = asset->mBufferViews[source.mBufferView];
            const auto& data = asset->mBuffers[view.mBuffer].mData;
            if (view.mByteLength > 16 * 1024 * 1024)
            {
                error = "glTF encoded image exceeds admission limit";
                return {};
            }
            bytes.assign(data.begin() + view.mByteOffset, data.begin() + view.mByteOffset + view.mByteLength);
        }
        else if (source.mUri.starts_with("data:"))
        {
            const auto comma     = source.mUri.find(',');
            const auto delimiter = source.mUri.find(";base64,");
            if (comma == std::string::npos || delimiter == std::string::npos || source.mUri.size() > 16 * 1024 * 1024)
            {
                error = "Invalid glTF image data URI";
                return {};
            }
            mime               = source.mUri.substr(5, delimiter - 5);
            const auto decoded = LLBase64::decodeAsString(source.mUri.substr(comma + 1));
            bytes.assign(decoded.begin(), decoded.end());
        }
        else if (!asset->mFilename.empty() && !source.mUri.empty())
        {
            const auto path = gDirUtilp->getDirName(asset->mFilename) + gDirUtilp->getDirDelimiter() + LLURI::unescape(source.mUri);
            llifstream file(path.c_str(), std::ios::binary | std::ios::ate);
            const auto size = file.tellg();
            if (!file || size <= 0 || size > 16 * 1024 * 1024)
            {
                error = "Invalid local glTF image file";
                return {};
            }
            bytes.resize(std::size_t(size));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(bytes.data()), size);
            if (!file)
            {
                error = "Local glTF image read failed";
                return {};
            }
            if (mime.empty())
                mime = gDirUtilp->getExtension(path) == "png" ? "image/png" : "image/jpeg";
        }
        if (bytes.empty())
        {
            error = "Missing glTF encoded image";
            return {};
        }
        LLPointer<LLImageFormatted> encoded = LLImageFormatted::loadFromMemory(bytes.data(), U32(bytes.size()), mime);
        if (!encoded || encoded->getWidth() <= 0 || encoded->getHeight() <= 0 || encoded->getWidth() > 2048 || encoded->getHeight() > 2048)
        {
            error = "Invalid or oversized glTF image dimensions";
            return {};
        }
        LLPointer<LLImageRaw> raw = new LLImageRaw;
        if (!encoded->decode(raw, 0))
        {
            error = "glTF image decode failed";
            return {};
        }
        const auto key = "gltf-scene-image:" + imageOwner.asString() + ":" + std::to_string(index);
        if (!resources.publish(key, *raw))
            return {};
        images[index] = key;
        return resources.image(key);
    }
    static void delivered(const LLUUID& id, LLAssetType::EType type, void* data, S32 status, LLExtStat)
    {
        std::unique_ptr<Request> request(static_cast<Request*>(data));
        const auto               weak = request->owner;
        LLAppViewer::instance()->postToMainCoro(
            [weak, id, type, status]
            {
                auto owner = weak.lock();
                if (!owner || !owner->current() || !owner->error.empty())
                    return;
                if (status != LL_ERR_NOERR)
                {
                    owner->error = "glTF asset fetch failed";
                    return;
                }
                try
                {
                    owner->receive(id, type);
                }
                catch (const std::exception& e)
                {
                    owner->error = e.what();
                }
            });
    }
    void fetch(const LLUUID& uuid, LLAssetType::EType type)
    {
        if (!gAssetStorage)
        {
            error = "Asset storage unavailable";
            return;
        }
        deadline       = LLFrameTimer::getTotalSeconds() + 60;
        auto request   = std::make_unique<Request>();
        request->owner = weak_from_this();
        gAssetStorage->getAssetData(uuid, type, delivered, request.release());
    }
    void receive(const LLUUID& uuid, LLAssetType::EType type)
    {
        if (!current() || !error.empty())
            return;
        if (type == LLAssetType::AT_GLTF)
        {
            if (uuid != id || asset || ready)
                return;
            LLFileSystem file(uuid, type, LLFileSystem::READ);
            const S32    size = file.getSize();
            if (size <= 0 || size > 4 * 1024 * 1024)
            {
                error = "glTF JSON exceeds admission limit";
                return;
            }
            auto encodedWorking = budget.reserve(std::size_t(size));
            if (!encodedWorking)
            {
                error = "glTF parse/decode admission exhausted";
                return;
            }
            std::string json(std::size_t(size), '\0');
            if (!file.read(reinterpret_cast<U8*>(json.data()), size))
            {
                error = "glTF JSON cache read failed";
                return;
            }
            auto jsonStorage = boost::json::make_shared_resource<BudgetJSONResource>(budget);
            auto parsedJSON  = boost::json::parse(json, jsonStorage);
            typedResidency   = budget.reserve(typedSceneBytes(parsedJSON));
            if (!typedResidency)
            {
                error = "glTF typed decode admission exhausted";
                return;
            }
            auto        parsed = std::make_shared<Asset>(parsedJSON);
            std::size_t bytes  = 0;
            if (!validate(*parsed, bytes))
            {
                error = "Invalid or oversized glTF scene";
                return;
            }
            if (json.size() > maximumBytes - bytes)
            {
                error = "glTF retained scene exceeds admission limit";
                return;
            }
            residency = budget.reserve(bytes + json.size());
            if (!residency)
            {
                error = "glTF scene budget exhausted";
                return;
            }
            asset                = std::move(parsed);
            asset->mLoadIntoVRAM = false;
            std::set<LLUUID> buffers;
            for (const auto& buffer : asset->mBuffers)
            {
                LLUUID bufferID;
                if (!LLUUID::parseUUID(buffer.mUri, &bufferID) || bufferID.isNull())
                {
                    error = "Simulator glTF buffer reference is not an asset UUID";
                    return;
                }
                buffers.insert(bufferID);
            }
            pendingBuffers = buffers;
            for (const auto& buffer : buffers)
                fetch(buffer, LLAssetType::AT_GLTF_BIN);
            if (!pendingBuffers.empty())
                return;
        }
        else if (type == LLAssetType::AT_GLTF_BIN && pendingBuffers.erase(uuid)) { /* A specific requested buffer completes exactly once. */
        }
        else
            return;
        if (pendingBuffers.empty())
        {
            if (!validateData(*asset) || !asset->prep())
            {
                error = "Shared glTF scene decoder rejected asset";
                return;
            }
            asset->updateTransforms();
            ready = true;
        }
    }
};
VSWorldGLTF::VSWorldGLTF(const LLUUID& id, VSWorldRenderer& renderer, VSUIResources& resources, VSResourceBudget budget,
                         std::function<bool()> current) :
    mImpl(std::make_shared<Impl>(id, renderer, resources, std::move(budget), std::move(current)))
{ mImpl->fetch(id, LLAssetType::AT_GLTF); }
VSWorldGLTF::VSWorldGLTF(const std::string& filename, VSWorldRenderer& renderer, VSUIResources& resources, VSResourceBudget budget,
                         std::function<bool()> current)
{
    LLUUID identity;
    identity.generate();
    mImpl = std::make_shared<Impl>(identity, renderer, resources, std::move(budget), std::move(current));
    try
    {
        mImpl->local(filename);
    }
    catch (const std::exception& exception)
    {
        mImpl->error = exception.what();
    }
}
bool VSWorldGLTF::hasPublication() const
{ return mImpl->current() && mImpl->publicationKnown; }
bool VSWorldGLTF::ready() const
{
    if (!mImpl->ready && mImpl->error.empty() && mImpl->deadline && LLFrameTimer::getTotalSeconds() >= mImpl->deadline)
    {
        mImpl->error = "glTF asset fetch timed out";
        mImpl->asset.reset();
        mImpl->residency.reset();
        mImpl->typedResidency.reset();
        mImpl->pendingBuffers.clear();
    }
    if (!mImpl->error.empty())
    {
        // Terminal decoder failures release retained CPU data. Any previously
        // admitted GPU pose remains owned by its immutable publication packets.
        mImpl->asset.reset();
        mImpl->residency.reset();
        mImpl->typedResidency.reset();
        mImpl->pendingBuffers.clear();
        for (const auto& [index, key] : mImpl->images)
            mImpl->resources.erase(key);
        mImpl->images.clear();
    }
    return mImpl->ready && mImpl->error.empty();
}
VSWorldGLTF::~VSWorldGLTF() = default;
void VSWorldGLTF::append(std::vector<VSWorldRenderer::Packet>& packets, const LLVector3& position, const LLQuaternion& rotation,
                         const LLVector3& scale, const LLVector3& eye)
{
    auto& owner = *mImpl;
    if (!owner.current())
        return;
    if (!ready())
    {
        packets.insert(packets.end(), owner.publishedPackets.begin(), owner.publishedPackets.end());
        return;
    }
    auto                                 candidates = owner.publications;
    std::vector<VSWorldRenderer::Packet> candidatePackets;
    bool                                 deferred = false;
    auto&                                asset    = *owner.asset;
    asset.update();
    std::set<unsigned>            visible;
    std::function<void(unsigned)> visit = [&](unsigned node)
    {
        if (!visible.insert(node).second)
            return;
        for (S32 child : asset.mNodes[node].mChildren)
            visit(unsigned(child));
    };
    const auto& scene = asset.mScenes[asset.mScene == INVALID_INDEX ? 0 : asset.mScene];
    for (S32 root : scene.mNodes)
        visit(unsigned(root));
    unsigned faceIndex = 0;
    for (unsigned nodeIndex : visible)
    {
        const auto& node = asset.mNodes[nodeIndex];
        if (node.mMesh == INVALID_INDEX)
            continue;
        std::vector<mat4> palette;
        if (node.mSkin != INVALID_INDEX)
        {
            const auto& skin = asset.mSkins[node.mSkin];
            for (unsigned joint = 0; joint < skin.mJoints.size(); ++joint)
                palette.push_back(asset.mNodes[skin.mJoints[joint]].mAssetMatrix * skin.mInverseBindMatricesData[joint]);
        }
        std::vector<float> signature{ position.mV[0], position.mV[1], position.mV[2], scale.mV[0], scale.mV[1], scale.mV[2] };
        signature.insert(signature.end(), rotation.mQ, rotation.mQ + 4);
        signature.insert(signature.end(), glm::value_ptr(node.mAssetMatrix), glm::value_ptr(node.mAssetMatrix) + 16);
        for (const auto& matrix : palette)
            signature.insert(signature.end(), glm::value_ptr(matrix), glm::value_ptr(matrix) + 16);
        const auto& primitives = asset.mMeshes[node.mMesh].mPrimitives;
        for (unsigned primitiveIndex = 0; primitiveIndex < primitives.size(); ++primitiveIndex)
        {
            const unsigned face      = faceIndex++;
            const auto&    primitive = primitives[primitiveIndex];
            auto&          published = candidates[{ nodeIndex, primitiveIndex }];
            if (!published.mesh || published.signature != signature)
            {
                std::vector<VSWorldRenderer::Vertex> vertices;
                for (unsigned vertex = 0; vertex < primitive.mPositions.size(); ++vertex)
                {
                    mat4 matrix = node.mAssetMatrix;
                    if (!palette.empty() && !primitive.mWeights.empty())
                    {
                        if (primitive.mWeights.size() != primitive.mPositions.size() ||
                            primitive.mJoints.size() != primitive.mPositions.size())
                        {
                            owner.error = "Incomplete glTF skin channels";
                            return;
                        }
                        matrix      = mat4(0);
                        float total = 0;
                        for (unsigned influence = 0; influence < 4; ++influence)
                        {
                            const unsigned joint  = unsigned((primitive.mJoints[vertex] >> (influence * 16)) & 0xffff);
                            const float    weight = primitive.mWeights[vertex].getF32ptr()[influence];
                            if (!std::isfinite(weight) || weight < 0 || (weight && joint >= palette.size()))
                            {
                                owner.error = "Invalid glTF joint weight";
                                return;
                            }
                            if (weight)
                                matrix += palette[joint] * weight;
                            total += weight;
                        }
                        if (!(total > 0))
                        {
                            owner.error = "Zero glTF skin weight";
                            return;
                        }
                        matrix /= total;
                    }
                    const auto* source = primitive.mPositions[vertex].getF32ptr();
                    const vec3  p      = vec3(matrix * vec4(source[0], source[1], source[2], 1));
                    vec3        n(0, 0, 1);
                    if (vertex < primitive.mNormals.size())
                    {
                        const auto* normal    = primitive.mNormals[vertex].getF32ptr();
                        const auto  transform = glm::mat3(matrix);
                        // Zero-scale nodes are valid glTF. Their triangles
                        // collapse; they must not invalidate other scene nodes.
                        if (std::abs(glm::determinant(transform)) > 1.e-12f)
                            n = glm::transpose(glm::inverse(transform)) * vec3(normal[0], normal[1], normal[2]);
                    }
                    // Match LLViewerObject::getGLTFAssetToAgentTransform:
                    // the object scale/rotation applies directly to the asset.
                    LLVector3 point(p.x * scale.mV[0], p.y * scale.mV[1], p.z * scale.mV[2]);
                    point = point * rotation + position;
                    LLVector3 normal(n.x / scale.mV[0], n.y / scale.mV[1], n.z / scale.mV[2]);
                    normal = normal * rotation;
                    normal.normalize();
                    const auto uv = vertex < primitive.mTexCoords0.size() ? primitive.mTexCoords0[vertex] : LLVector2::zero;
                    VSWorldRenderer::Vertex output{point.mV[0],point.mV[1],point.mV[2],normal.mV[0],normal.mV[1],normal.mV[2],uv.mV[0],uv.mV[1]};
                    if(vertex<primitive.mTexCoords1.size()) {output.u1=primitive.mTexCoords1[vertex].mV[0];output.v1=primitive.mTexCoords1[vertex].mV[1];}
                    if(vertex<primitive.mColors.size())
                    {
                        const auto& color=primitive.mColors[vertex];output.r=float(color.mV[0])/255;output.g=float(color.mV[1])/255;
                        output.b=float(color.mV[2])/255;output.a=float(color.mV[3])/255;
                    }
                    if(vertex<primitive.mTangents.size())
                    {
                        const auto* tangent=primitive.mTangents[vertex].getF32ptr();
                        vec3 t=glm::mat3(matrix)*vec3(tangent[0],tangent[1],tangent[2]);
                        LLVector3 transformed(t.x*scale.mV[0],t.y*scale.mV[1],t.z*scale.mV[2]);transformed=transformed*rotation;transformed.normalize();
                        output.tx=transformed.mV[0];output.ty=transformed.mV[1];output.tz=transformed.mV[2];
                        output.tw=tangent[3]*(glm::determinant(glm::mat3(matrix))<0?-1.f:1.f);
                    }
                    vertices.push_back(output);
                }
                std::vector<unsigned>     indices;
                VSWorldRenderer::Topology topology = VSWorldRenderer::Topology::Triangles;
                const auto&               source   = primitive.mIndexArray;
                // Shared normal/tangent preparation may expand a strip or fan
                // into indexed triangles. Consume the prepared CPU topology,
                // just as the reference renderer does, rather than converting
                // the original file mode a second time.
                const auto mode = primitive.mGLMode;
                if (mode == LLRender::TRIANGLES)
                    indices.assign(source.begin(), source.end());
                else if (mode == LLRender::TRIANGLE_STRIP)
                    for (unsigned i = 2; i < source.size(); ++i)
                        indices.insert(indices.end(), { source[i - 2 + (i % 2)], source[i - 1 - (i % 2)], source[i] });
                else if (mode == LLRender::TRIANGLE_FAN)
                    for (unsigned i = 2; i < source.size(); ++i)
                        indices.insert(indices.end(), { source[0], source[i - 1], source[i] });
                else if (mode == LLRender::POINTS)
                {
                    indices.assign(source.begin(), source.end());
                    topology = VSWorldRenderer::Topology::Points;
                }
                else
                {
                    topology = VSWorldRenderer::Topology::Lines;
                    if (mode == LLRender::LINES)
                        indices.assign(source.begin(), source.end());
                    else if (mode == LLRender::LINE_LOOP || mode == LLRender::LINE_STRIP)
                    {
                        for (unsigned i = 1; i < source.size(); ++i)
                            indices.insert(indices.end(), { source[i - 1], source[i] });
                        if (mode == LLRender::LINE_LOOP && source.size() > 1)
                            indices.insert(indices.end(), { source.back(), source.front() });
                    }
                    else
                    {
                        owner.error = "Unknown glTF topology";
                        return;
                    }
                }
                const unsigned group = topology == VSWorldRenderer::Topology::Triangles ? 3
                                       : topology == VSWorldRenderer::Topology::Lines   ? 2
                                                                                        : 1;
                if (indices.empty() || indices.size() % group)
                {
                    owner.error = "Invalid glTF surface topology";
                    return;
                }
                for (const auto& vertex : vertices)
                    for (float value : { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v })
                        if (!std::isfinite(value))
                        {
                            owner.error = "Nonfinite glTF geometry";
                            return;
                        }
                if (owner.renderer.publishMesh(published.mesh, vertices, indices, topology))
                    published.signature = signature;
                else
                    deferred = true;
            }
            if (!published.mesh)
            {
                deferred = true;
                continue;
            }
            VSWorldRenderer::Packet packet;
            packet.mesh            = published.mesh;
            packet.distanceSquared = (position - eye).lengthSquared();
            packet.faceIndex       = face;
            packet.gltfNode        = int(nodeIndex);
            packet.gltfPrimitive   = int(primitiveIndex);
            if (primitive.mMaterial != INVALID_INDEX)
            {
                const auto& material = asset.mMaterials[primitive.mMaterial];
                std::copy_n(glm::value_ptr(material.mPbrMetallicRoughness.mBaseColorFactor), 4, packet.color.begin());
                packet.alpha       = material.mAlphaMode == Material::AlphaMode::BLEND;
                packet.masked      = material.mAlphaMode == Material::AlphaMode::MASK;
                packet.fullbright  = material.mUnlit.mPresent;
                packet.alphaCutoff = material.mAlphaCutoff;
                packet.doubleSided = material.mDoubleSided;
                auto surface=std::make_shared<VSMaterial::State>();surface->pbr=true;
                surface->base=packet.color;surface->alpha=packet.alpha?VSWorldContracts::AlphaMode::Blend:
                    packet.masked?VSWorldContracts::AlphaMode::Mask:VSWorldContracts::AlphaMode::Opaque;
                surface->cutoff=packet.alphaCutoff;surface->doubleSided=packet.doubleSided;surface->fullbright=packet.fullbright;
                surface->metallic=material.mPbrMetallicRoughness.mMetallicFactor;surface->roughness=material.mPbrMetallicRoughness.mRoughnessFactor;
                surface->normalScale=material.mNormalTexture.mScale;surface->occlusionStrength=material.mOcclusionTexture.mStrength;
                std::copy_n(glm::value_ptr(material.mEmissiveFactor),3,surface->emissive.begin());
                const TextureInfo* infos[]{&material.mPbrMetallicRoughness.mBaseColorTexture,&material.mNormalTexture,
                    &material.mPbrMetallicRoughness.mMetallicRoughnessTexture,&material.mEmissiveTexture};
                bool channelsReady=true;
                for(unsigned channel=0;channel<4;++channel)
                {
                    const auto& info=*infos[channel];const auto texture=info.mIndex;
                    const auto set=info.getTexCoord();
                    if(set<0 || set>1 || (set==1 && primitive.mTexCoords1.size()!=primitive.mPositions.size()))
                    {owner.error="glTF material requests an absent UV channel";return;}
                    surface->uvSet[channel]=unsigned(set);
                    auto& sampling=surface->samplers[channel];sampling.mipmaps=true;
                    sampling.anisotropy=gSavedSettings.getBOOL("RenderAnisotropic")?16u:1u;
                    if(index(texture,asset.mTextures) && index(asset.mTextures[texture].mSampler,asset.mSamplers))
                    {
                        const auto& sampler=asset.mSamplers[asset.mTextures[texture].mSampler];
                        sampling.minification=(sampler.mMinFilter==9728 || sampler.mMinFilter==9984 || sampler.mMinFilter==9986)?VSWorldContracts::Filter::Nearest:VSWorldContracts::Filter::Linear;
                        sampling.magnification=sampler.mMagFilter==9728?VSWorldContracts::Filter::Nearest:VSWorldContracts::Filter::Linear;
                        sampling.mipmaps=sampler.mMinFilter>=9984;sampling.trilinear=sampler.mMinFilter>=9986;
                        const auto wrap=[](S32 value) {return value==33071?VSWorldContracts::AddressMode::Clamp:
                            value==33648?VSWorldContracts::AddressMode::Mirror:VSWorldContracts::AddressMode::Repeat;};
                        sampling.u=wrap(sampler.mWrapS);sampling.v=wrap(sampler.mWrapT);
                    }
                    const auto& source=info.mTextureTransform;
                    const float c=std::cos(source.mRotation),r=std::sin(source.mRotation);
                    surface->uv[channel].matrix={c*source.mScale.x,r*source.mScale.x,-r*source.mScale.y,c*source.mScale.y};
                    surface->uv[channel].offset={source.mOffset.x+r*source.mScale.y,1-source.mOffset.y-c*source.mScale.y};
                    if(index(texture,asset.mTextures) && index(asset.mTextures[texture].mSource,asset.mImages))
                    {
                        auto image=owner.image(unsigned(asset.mTextures[texture].mSource));
                        if(!image) {channelsReady=false;continue;}
                        if(!channel) packet.image=std::move(image);else packet.channels[channel-1]=std::move(image);
                    }
                }
                if(!channelsReady) {deferred=true;continue;}
                packet.surface=std::move(surface);
            }
            candidatePackets.push_back(std::move(packet));
        }
    }
    if (!deferred)
    {
        owner.publications     = std::move(candidates);
        owner.publishedPackets = std::move(candidatePackets);
        owner.publicationKnown = true;
    }
    packets.insert(packets.end(), owner.publishedPackets.begin(), owner.publishedPackets.end());
}
LLSD VSWorldGLTF::evidence() const
{
    LLSD result;
    result["asset"]                = mImpl->id;
    result["ready"]                = mImpl->ready;
    result["publication_known"]    = mImpl->publicationKnown;
    result["error"]                = mImpl->error;
    result["pending_buffers"]      = S32(mImpl->pendingBuffers.size());
    result["published_primitives"] = S32(mImpl->publications.size());
    result["resident_bytes"] =
        ll_sd_from_U64((mImpl->residency ? mImpl->residency->bytes() : 0) + (mImpl->typedResidency ? mImpl->typedResidency->bytes() : 0));
    return result;
}
