// CPU skin assets published through native UI facades. LGPL-2.1.
#include "vsuiimageprovider.h"
#include "lldir.h"
#include "llimage.h"
#include "llviewerprecompiledheaders.h"
#include "llxmlnode.h"
#include "llxuiparser.h"
#include "vsuiresources.h"
#include <filesystem>
#include <map>
#include <stdexcept>
namespace
{
struct Image : LLInitParam::Block<Image>
{
    Mandatory<std::string> name;
    Optional<std::string> file_name;
    Optional<bool> preload, use_mips;
    Optional<LLRect> scale, clip;
    Optional<std::string> scale_type;
    Image()
        : name("name"), file_name("file_name"), preload("preload", false), use_mips("use_mips", false), scale("scale"),
          clip("clip"), scale_type("scale_type", "scale_inner")
    {
    }
};
struct Images : LLInitParam::Block<Images>
{
    Mandatory<S32> version;
    Multiple<Image> textures;
    Images() : version("version"), textures("texture")
    {
    }
};
} // namespace
struct VSUIImageProvider::Impl
{
    VSUIResources &resources;
    std::map<std::string, Image> declarations;
    std::map<std::string, LLUIImagePtr> cache;
    explicit Impl(VSUIResources &r) : resources(r)
    {
        const auto paths = gDirUtilp->findSkinnedFilenames(LLDir::TEXTURES, "textures.xml", LLDir::ALL_SKINS);
        if (paths.empty())
            throw std::runtime_error("Native UI skin image definitions are missing");
        Images images;
        LLXUIParser parser;
        for (const auto &path : paths)
        {
            LLXMLNodePtr root;
            if (!LLXMLNode::parseFile(path, root, nullptr))
                throw std::runtime_error("Native UI skin image definitions cannot be parsed: " + path);
            parser.readXUI(root, images, path);
        }
        if (!images.validateBlock())
            throw std::runtime_error("Invalid native UI skin image definitions");
        for (const auto &image : images.textures)
            declarations[image.name()].overwriteFrom(image);
    }
};
VSUIImageProvider::VSUIImageProvider(VSUIResources &resources) : mImpl(std::make_unique<Impl>(resources))
{
}
VSUIImageProvider::~VSUIImageProvider()
{
    cleanUp();
}
void VSUIImageProvider::cleanUp()
{
    for (const auto &entry : mImpl->cache)
        mImpl->resources.erase("skin:" + entry.first);
    mImpl->cache.clear();
}
LLPointer<LLUIImage> VSUIImageProvider::getUIImageByID(const LLUUID &id, S32)
{
    if (id.isNull())
        return nullptr;
    throw std::runtime_error("Remote UUID UI image is outside native local-skin admission: " + id.asString());
}
LLPointer<LLUIImage> VSUIImageProvider::getUIImage(const std::string &name, S32)
{
    if (name.empty() || name == "none")
        return nullptr;
    auto &c = *mImpl;
    if (auto found = c.cache.find(name); found != c.cache.end())
        return found->second;
    Image declaration;
    if (auto found = c.declarations.find(name); found != c.declarations.end())
        declaration = found->second;
    const std::string file = declaration.file_name.isProvided() ? declaration.file_name() : name;
    const std::filesystem::path relative(file);
    if (relative.is_absolute() || relative.has_root_name())
        throw std::runtime_error("Absolute UI skin asset path");
    for (const auto &part : relative)
        if (part == "..")
            throw std::runtime_error("Parent traversal in UI skin asset path");
    const auto path = gDirUtilp->findSkinnedFilename(LLDir::TEXTURES, file);
    LLPointer<LLImageFormatted> formatted = LLImageFormatted::createFromExtension(file);
    LLPointer<LLImageRaw> raw = new LLImageRaw;
    if (path.empty() || !formatted || !formatted->load(path) || !formatted->decode(raw, 0))
        throw std::runtime_error("Native UI skin image decode failed: " + file);
    auto image = c.resources.publish("skin:" + name, *raw);
    auto region = [&](const LLRect &r, S32 width, S32 height) {
        return LLRectf(llclamp(static_cast<F32>(r.mLeft) / width, 0.f, 1.f),
                       llclamp(static_cast<F32>(r.mTop) / height, 0.f, 1.f),
                       llclamp(static_cast<F32>(r.mRight) / width, 0.f, 1.f),
                       llclamp(static_cast<F32>(r.mBottom) / height, 0.f, 1.f));
    };
    if (declaration.clip.isProvided())
        image->setClipRegion(region(declaration.clip(), raw->getWidth(), raw->getHeight()));
    if (declaration.scale.isProvided())
        image->setScaleRegion(region(declaration.scale(), image->getWidth(), image->getHeight()));
    if (declaration.scale_type() != "scale_inner" && declaration.scale_type() != "scale_outer")
        throw std::runtime_error("Invalid native UI image scale style: " + name);
    image->setScaleStyle(declaration.scale_type() == "scale_outer" ? LLUIImage::SCALE_OUTER : LLUIImage::SCALE_INNER);
    c.cache.emplace(name, image);
    image->onImageLoaded();
    LL_INFOS("NativeUI") << "CPU skin asset: " << path << LL_ENDL;
    return image;
}
