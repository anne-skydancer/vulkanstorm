/**
 * @file lluiimagedecls.h
 * @brief LLUIImageDecls — shared, GL-free registry of UI image declarations
 *        parsed from the skins' textures.xml files.
 *
 * @details
 * <VulkanStorm>
 * Both render backends consume the same UI chrome declarations
 * (skins/<skin>/textures/textures.xml). Historically each backend parsed the
 * XML itself: the GL path in LLUIImageList::initFromFile()
 * (newview/llviewertexturelist.cpp) and the Vulkan path in LLVKUIImage::init()
 * (llvulkan/llvkuiimage.cpp). This module is the single shared parse: it
 * captures, per image name, the union of the fields both parsers read
 * (file_name, preload, use_mips, clip/scale pixel rects, scale_type) with
 * exactly the merge semantics the GL path has always produced (ALL_SKINS
 * ordering, generic-to-specific, a later declaration overwrites only the
 * fields it actually provides — the same result as LLInitParam::Block
 * overwriteFrom used by the GL parser).
 *
 * This code is GL-free by contract: it uses only llxml (LLXMLNode),
 * llfilesystem (LLDir), llmath (LLRect) and llcommon. It must never include
 * llrender/GL headers — see doc/vulkan/shared_assets.md.
 *
 * GPU-side behavior stays per-backend: the GL consumer
 * (LLUIImageList::initFromFile) resolves and decodes lazily through the
 * fetched-texture pipeline exactly as before; the Vulkan consumer
 * (LLVKUIImage::init) resolves eagerly via resolvePaths() and uploads to
 * Vulkan textures.
 * </VulkanStorm>
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (C) 2026, The Vulkanstorm Project
 * $/LicenseInfo$
 */

#ifndef LL_LLUIIMAGEDECLS_H
#define LL_LLUIIMAGEDECLS_H

#include <map>
#include <string>

#include "llrect.h"     // LLRect (llmath — GL-free)

namespace LLUIImageDecls
{
    // <VulkanStorm>
    // GL-free mirror of LLUIImage::EScaleStyle (llrender/lluiimage.h) and of
    // LLVKUIImage::ScaleStyle (llvulkan/llvkuiimage.h). Consumers map to their
    // own backend enum; values correspond to textures.xml's scale_type
    // attribute ("scale_inner" / "scale_outer").
    enum EScaleStyle
    {
        SCALE_INNER,
        SCALE_OUTER
    };

    // One merged UI image declaration. clip_region/scale_region are the PIXEL
    // rects read from the XML dotted sub-attributes (clip.left, clip.top, ...);
    // LLRect::null when the declaration does not provide them (matching the
    // GL path, which distinguishes "absent" by comparison with LLRect::null).
    // Per-backend normalization to UVs happens against each backend's decoded
    // image dimensions, exactly as it does today.
    struct Decl
    {
        std::string name;           // mandatory texture name (the map key)
        std::string file_name;      // declared file_name; empty when never provided
        std::string resolved_path;  // CURRENT_SKIN-resolved path; set by resolvePaths()
        LLRect      clip_region;    // pixel clip rect, LLRect::null when absent
        LLRect      scale_region;   // pixel scale rect, LLRect::null when absent
        EScaleStyle scale_style;    // scale_type, default SCALE_INNER
        bool        preload;        // preload attribute, default false
        bool        use_mips;       // use_mips attribute, default false

        Decl()
        :   scale_style(SCALE_INNER),
            preload(false),
            use_mips(false)
        {}

        // The file the GL path would load: file_name when provided, else the
        // declaration name (mirrors LLUIImageList::initFromFile).
        const std::string& getFileName() const { return file_name.empty() ? name : file_name; }
    };

    typedef std::map<std::string, Decl> decl_map_t;

    // Parse every skins' textures.xml in ALL_SKINS order (generic to specific)
    // and merge with GL-identical overwrite semantics. Idempotent: subsequent
    // calls return the cached result without re-parsing. Returns false (and
    // stays "not loaded") when no textures.xml is found, when the first
    // (most generic) file fails to parse, or when it carries no version
    // attribute — the same failure contract LLUIImageList::initFromFile had.
    bool load();

    // True after a successful load().
    bool isLoaded();

    // Resolve every declaration's image file through the CURRENT_SKIN search
    // path (LLDir::findSkinnedFilename(LLDir::TEXTURES, ...)) into
    // Decl::resolved_path (empty when no file is found). Eager, matching the
    // Vulkan consumer's existing behavior; the GL consumer does not use it
    // (its fetch pipeline resolves lazily as before). Calls load() first if
    // needed.
    void resolvePaths();

    // Look up one declaration by name; nullptr when unknown.
    const Decl* get(const std::string& name);

    // All merged declarations, keyed and ordered by name (the same iteration
    // order both backends used on their merged std::map).
    const decl_map_t& getDecls();
    // </VulkanStorm>
}

#endif // LL_LLUIIMAGEDECLS_H
