// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// MEASURING BUILD ONLY: see llimpostorproof.h.

#include "llviewerprecompiledheaders.h"
#include "llimpostorproof.h"

#include <cctype>
#include <string>
#include <vector>

#include "lldate.h"
#include "lldir.h"
#include "lldrawpool.h"
#include "llfetchedgltfmaterial.h"
#include "llfile.h"
#include "llglheaders.h"
#include "llimage.h"
#include "llimagepng.h"
#include "llspatialpartition.h"
#include "llviewercontrol.h"
#include "llviewertexture.h"
#include "llvoavatar.h"
#include "pipeline.h"

namespace LLImpostorProof
{
    bool sRecording = false;
    int sLastGltfBind = 0;

    namespace
    {
        struct Item
        {
            std::string mKey;
            std::string mPath;      // "full PBR", "stripped" or "legacy"
            std::string mAlphaMode; // glTF items only
            std::string mTexture;
            std::string mMaterial;
            U32 mSeed = 0;
            bool mRigged = false;
            S32 mDraws = 0;
            S32 mBlended = 0;       // draws with blending on and colour written, both renders
            S32 mBlendedSecond = 0; // the same in the impostor's second (alpha depth) render: the fix makes it 0
        };

        std::vector<Item> sItems;
        U32 sGeneration = 0;
        std::string sSession;

        std::string proofDir()
        {
            return gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "impostor_proof");
        }

        const std::string& sessionTag()
        {
            if (sSession.empty())
            {
                sSession = LLDate::now().toHTTPDateString("%Y%m%dT%H%M%S");
            }
            return sSession;
        }

        std::string safeName(std::string s)
        {
            for (char& c : s)
            {
                if (!isalnum((unsigned char)c) && c != '-')
                {
                    c = '_';
                }
            }
            return s;
        }

        std::string jsonEscape(const std::string& s)
        {
            std::string out;
            for (char c : s)
            {
                if (c == '"' || c == '\\')
                {
                    out += '\\';
                }
                if ((unsigned char)c >= 0x20)
                {
                    out += c;
                }
            }
            return out;
        }
    }

    bool fullPBR()
    {
        return gSavedSettings.getBOOL("RenderImpostorFullPBR");
    }

    bool settingChanged()
    {
        static bool last = fullPBR();
        const bool now = fullPBR();
        if (now != last)
        {
            last = now;
            LL_INFOS("ImpostorProof") << "RenderImpostorFullPBR is now " << (now ? "TRUE (after)" : "FALSE (before)")
                                      << ": every impostor regenerates" << LL_ENDL;
            return true;
        }
        return false;
    }

    void beginGeneration(LLVOAvatar* avatar)
    {
        sItems.clear();
        sRecording = avatar && gSavedSettings.getBOOL("RenderImpostorProofLog");
    }

    void noteDrawImpl(const LLDrawInfo& info)
    {
        Item item;
        item.mRigged = info.mAvatar.notNull();
        item.mSeed = info.mStochasticSeed;
        item.mTexture = info.mTexture.notNull() ? info.mTexture->getID().asString().substr(0, 8) : "none";
        if (info.mGLTFMaterial.notNull())
        {
            item.mMaterial = info.mMaterialID.asString().substr(0, 8);
            switch (info.mGLTFMaterial->mAlphaMode)
            {
            case LLGLTFMaterial::ALPHA_MODE_MASK:  item.mAlphaMode = "mask"; break;
            case LLGLTFMaterial::ALPHA_MODE_BLEND: item.mAlphaMode = "blend"; break;
            default:                               item.mAlphaMode = "opaque"; break;
            }
            // the branch LLFetchedGLTFMaterial::bind really took for this draw (it runs before the draw)
            item.mPath = sLastGltfBind == 2 ? "full PBR" : sLastGltfBind == 1 ? "stripped" : "unbound";
            sLastGltfBind = 0;
        }
        else
        {
            item.mPath = "legacy";
            item.mMaterial = info.mMaterialID.notNull() ? info.mMaterialID.asString().substr(0, 8) : "none";
        }
        item.mKey = llformat("%08x/%s/%s/%d", item.mSeed, item.mTexture.c_str(), item.mMaterial.c_str(), item.mRigged ? 1 : 0);

        GLboolean mask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
        glGetBooleanv(GL_COLOR_WRITEMASK, mask);
        const bool blended = glIsEnabled(GL_BLEND) && (mask[0] || mask[1] || mask[2]);
        const bool second = LLPipeline::sImpostorRenderAlphaDepthPass;

        for (Item& known : sItems)
        {
            if (known.mKey == item.mKey)
            {
                ++known.mDraws;
                known.mBlended += blended ? 1 : 0;
                known.mBlendedSecond += (blended && second) ? 1 : 0;
                return;
            }
        }
        item.mDraws = 1;
        item.mBlended = blended ? 1 : 0;
        item.mBlendedSecond = (blended && second) ? 1 : 0;
        sItems.push_back(item);
    }

    void endGeneration(LLVOAvatar* avatar, S32 width, S32 height)
    {
        if (!sRecording)
        {
            return;
        }
        sRecording = false;

        const bool full = LLPipeline::sImpostorFullPBR;
        const std::string tag = full ? "after" : "before";
        const std::string name = avatar->getFullname() + " " + avatar->getID().asString().substr(0, 8);
        const U32 gen = ++sGeneration;
        const std::string delim = gDirUtilp->getDirDelimiter();

        LLFile::mkdir(proofDir());
        LLFile::mkdir(proofDir() + delim + tag);

        // the finished impostor, as the billboard shows it (colour with the alpha mask); the target is still bound
        std::string png_name;
        if (width > 0 && height > 0)
        {
            LLPointer<LLImageRaw> raw = new LLImageRaw(width, height, 4);
            GLint old_read_buffer = 0, old_align = 4, old_pbo = 0;
            glGetIntegerv(GL_READ_BUFFER, &old_read_buffer);
            glGetIntegerv(GL_PACK_ALIGNMENT, &old_align);
            glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &old_pbo);
            if (old_pbo)
            {
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0); // a bound buffer would turn the read into an offset write
            }
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, raw->getData());
            glPixelStorei(GL_PACK_ALIGNMENT, old_align);
            glReadBuffer((GLenum)old_read_buffer);
            if (old_pbo)
            {
                glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)old_pbo);
            }
            png_name = tag + "/" + sessionTag() + "_" + llformat("%05u", gen) + "_" + safeName(name) + ".png";
            LLPointer<LLImagePNG> png = new LLImagePNG();
            if (png->encode(raw, 0.f))
            {
                png->save(proofDir() + delim + png_name);
            }
            else
            {
                png_name.clear();
            }
        }

        // one JSON object per line
        llofstream out(proofDir() + delim + "items.jsonl", std::ios::app);
        S32 gltf_items = 0;
        S32 blended_items = 0;
        const std::string head = "{\"session\":\"" + sessionTag() + "\",\"gen\":" + std::to_string(gen) +
            ",\"flag\":" + (full ? "true" : "false") + ",\"tag\":\"" + tag + "\",\"avatar\":\"" + jsonEscape(name) + "\"";
        for (const Item& item : sItems)
        {
            gltf_items += item.mPath == "legacy" ? 0 : 1;
            blended_items += item.mBlended > 0 ? 1 : 0;
            out << head << ",\"item\":\"" << item.mKey << "\",\"texture\":\"" << item.mTexture
                << "\",\"material\":\"" << item.mMaterial << "\",\"rigged\":" << (item.mRigged ? "true" : "false")
                << ",\"path\":\"" << item.mPath << "\",\"alpha_mode\":\"" << item.mAlphaMode
                << "\",\"draws\":" << item.mDraws << ",\"blend_count\":" << item.mBlended << ",\"blend_second\":" << item.mBlendedSecond
                << ",\"png\":\"" << jsonEscape(png_name) << "\"}\n";
            LL_INFOS("ImpostorProof") << "gen " << gen << " [" << tag << "] avatar " << name << " item " << item.mKey
                                      << " path " << item.mPath << " alpha " << item.mAlphaMode
                                      << " draws " << item.mDraws << " blended " << item.mBlended << " blended-in-second-render " << item.mBlendedSecond << LL_ENDL;
        }
        if (sItems.empty())
        {
            out << head << ",\"item\":null,\"png\":\"" << jsonEscape(png_name) << "\"}\n";
        }
        LL_INFOS("ImpostorProof") << "gen " << gen << " [" << tag << "] avatar " << name << ": " << sItems.size()
                                  << " items, " << gltf_items << " glTF, " << blended_items << " blended, png "
                                  << png_name << LL_ENDL;
        sItems.clear();
    }
}
