/**
 * @file lluiimagedecls.cpp
 * @brief Implementation of LLUIImageDecls — the shared, GL-free UI image
 *        declaration registry. See lluiimagedecls.h.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (C) 2026, The Vulkanstorm Project
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "lluiimagedecls.h"

// <VulkanStorm>
// GL-free dependencies ONLY: llfilesystem (skin search paths), llxml (XML
// parse), llmath (LLRect via the header), llcommon (logging). No llrender,
// no GL — see doc/vulkan/shared_assets.md.
#include "lldir.h"
#include "llerror.h"
#include "llxmlnode.h"
// </VulkanStorm>

#include <cstdio>
#include <vector>

// <VulkanStorm>
namespace
{
    LLUIImageDecls::decl_map_t    s_decls;
    bool                          s_loaded = false;

    // Read one dotted pixel rect ("clip.left"/"clip.top"/"clip.right"/
    // "clip.bottom"). textures.xml always writes all four sub-attributes;
    // a partial rect is ignored (left as LLRect::null), matching the field
    // being absent.
    bool readRectAttr(LLXMLNodePtr node, const char* base, LLRect& out)
    {
        char a[64];
        S32 l, t, r, b;
        snprintf(a, sizeof(a), "%s.left",   base); if (!node->getAttributeS32(a, l)) return false;
        snprintf(a, sizeof(a), "%s.top",    base); if (!node->getAttributeS32(a, t)) return false;
        snprintf(a, sizeof(a), "%s.right",  base); if (!node->getAttributeS32(a, r)) return false;
        snprintf(a, sizeof(a), "%s.bottom", base); if (!node->getAttributeS32(a, b)) return false;
        out.mLeft = l; out.mTop = t; out.mRight = r; out.mBottom = b;
        return true;
    }

    // Merge one textures.xml file into s_decls. Later (more specific) files
    // overwrite ONLY the fields a declaration actually provides, retaining
    // the rest — the same result as the GL parser's
    // LLInitParam::Block::overwriteFrom over its merged declarations.
    void mergeFile(const std::string& path)
    {
        LLXMLNodePtr root;
        if (!LLXMLNode::parseFile(path, root, nullptr) || root.isNull())
        {
            LL_WARNS() << "LLUIImageDecls: unable to parse UI image list file " << path << LL_ENDL;
            return;
        }
        for (LLXMLNodePtr child = root->getFirstChild(); child.notNull(); child = child->getNextSibling())
        {
            if (!child->hasName("texture")) continue;
            std::string name;
            if (!child->getAttributeString("name", name) || name.empty()) continue;

            LLUIImageDecls::Decl& decl = s_decls[name];
            decl.name = name;

            std::string file_name;
            if (child->getAttributeString("file_name", file_name) && !file_name.empty())
            {
                decl.file_name = file_name;
            }

            bool flag = false;
            if (child->getAttributeBOOL("preload", flag))  decl.preload  = flag;
            if (child->getAttributeBOOL("use_mips", flag)) decl.use_mips = flag;

            LLRect rect;
            if (readRectAttr(child, "clip",  rect)) decl.clip_region  = rect;
            if (readRectAttr(child, "scale", rect)) decl.scale_region = rect;

            std::string scale_type;
            if (child->getAttributeString("scale_type", scale_type))
            {
                decl.scale_style = (scale_type == "scale_outer")
                    ? LLUIImageDecls::SCALE_OUTER
                    : LLUIImageDecls::SCALE_INNER;
            }
        }
    }
}

namespace LLUIImageDecls
{
    bool load()
    {
        if (s_loaded) return true;

        // Same source set and ordering the GL path used: textures.xml from
        // every skin, generic to specific, so later declarations overlay
        // earlier ones.
        std::vector<std::string> textures_paths =
            gDirUtilp->findSkinnedFilenames(LLDir::TEXTURES, "textures.xml", LLDir::ALL_SKINS);
        if (textures_paths.empty())
        {
            LL_WARNS() << "LLUIImageDecls: no textures.xml found in skins directories" << LL_ENDL;
            return false;
        }

        // The first (most generic) file gets the GL path's validations: it
        // must parse and carry a version attribute.
        const std::string& first = textures_paths.front();
        {
            LLXMLNodePtr root;
            if (!LLXMLNode::parseFile(first, root, nullptr))
            {
                LL_WARNS() << "LLUIImageDecls: unable to parse UI image list file " << first << LL_ENDL;
                return false;
            }
            if (!root->hasAttribute("version"))
            {
                LL_WARNS() << "LLUIImageDecls: no valid version number in UI image list file " << first << LL_ENDL;
                return false;
            }
        }

        for (const std::string& path : textures_paths)
        {
            mergeFile(path);
        }

        s_loaded = true;
        LL_INFOS() << "LLUIImageDecls: loaded " << s_decls.size() << " UI image declarations" << LL_ENDL;
        return true;
    }

    bool isLoaded()
    {
        return s_loaded;
    }

    void resolvePaths()
    {
        if (!s_loaded && !load())
        {
            return;
        }
        // Resolve the merged filename through the current-skin search path,
        // which also permits PNG-only theme overrides (the same resolution
        // the Vulkan loader performed itself).
        for (decl_map_t::iterator it = s_decls.begin(); it != s_decls.end(); ++it)
        {
            it->second.resolved_path =
                gDirUtilp->findSkinnedFilename(LLDir::TEXTURES, it->second.getFileName());
        }
    }

    const Decl* get(const std::string& name)
    {
        decl_map_t::const_iterator it = s_decls.find(name);
        return (it != s_decls.end()) ? &it->second : nullptr;
    }

    const decl_map_t& getDecls()
    {
        return s_decls;
    }
}
// </VulkanStorm>
