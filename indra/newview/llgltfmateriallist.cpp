/**
 * @file   llgltfmateriallist.cpp
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llgltfmateriallist.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "vsresourcebudget.h"
#endif

#include "llagent.h"
#include "llassetstorage.h"
#include "lldispatcher.h"
#include "llfetchedgltfmaterial.h"
#include "llfilesystem.h"
#include "llsdserialize.h"
#include "lltinygltfhelper.h"
#include "llviewercontrol.h"
#include "llviewergenericmessage.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llviewerstats.h"
#include "llcorehttputil.h"
#include "llagent.h"
#include "llvocache.h"
#include "llworld.h"

#include "tinygltf/tiny_gltf.h"

#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

#include <unordered_set>
#include <cmath>

LLGLTFMaterialList gGLTFMaterialList;

LLGLTFMaterialList::modify_queue_t LLGLTFMaterialList::sModifyQueue;
LLGLTFMaterialList::apply_queue_t LLGLTFMaterialList::sApplyQueue;
LLSD LLGLTFMaterialList::sUpdates;

const size_t MAX_TASK_UPDATES = 255;

#ifdef SHOW_ASSERT
// return true if given data is (probably) valid update message for ModifyMaterialParams capability
static bool is_valid_update(const LLSD& data)
{
    llassert(data.isMap());

    U32 count = 0;

    if (data.has("object_id"))
    {
        if (!data["object_id"].isUUID())
        {
            LL_WARNS() << "object_id is not a UUID" << LL_ENDL;
            return false;
        }
        ++count;
    }
    else
    {
        LL_WARNS() << "Missing required parameter: object_id" << LL_ENDL;
        return false;
    }

    if (data.has("side"))
    {
        if (!data["side"].isInteger())
        {
            LL_WARNS() << "side is not an integer" << LL_ENDL;
            return false;
        }

        if (data["side"].asInteger() < -1)
        {
            LL_WARNS() << "side is invalid" << LL_ENDL;
        }
        ++count;
    }
    else
    {
        LL_WARNS() << "Missing required parameter: side" << LL_ENDL;
        return false;
    }

    if (data.has("gltf_json"))
    {
        if (!data["gltf_json"].isString())
        {
            LL_WARNS() << "gltf_json is not a string" << LL_ENDL;
            return false;
        }
        ++count;
    }

    if (data.has("asset_id"))
    {
        if (!data["asset_id"].isUUID())
        {
            LL_WARNS() << "asset_id is not a UUID" << LL_ENDL;
            return false;
        }
        ++count;
    }

    if (count < 3)
    {
        LL_WARNS() << "Only specified object_id and side, update won't actually change anything and is just noise" << LL_ENDL;
        return false;
    }

    if (data.size() != count)
    {
        LL_WARNS() << "update data contains unrecognized parameters" << LL_ENDL;
        return false;
    }

    return true;
}
#endif

class LLGLTFMaterialOverrideDispatchHandler
{
    LOG_CLASS(LLGLTFMaterialOverrideDispatchHandler);
public:
    LLGLTFMaterialOverrideDispatchHandler() = default;
    ~LLGLTFMaterialOverrideDispatchHandler() = default;

    void addCallback(void(*callback)(const LLUUID& object_id, S32 side))
    {
        mSelectionCallbacks.push_back(callback);
    }

    void doSelectionCallbacks(const LLUUID& object_id, S32 side)
    {
        for (auto& callback : mSelectionCallbacks)
        {
            callback(object_id, side);
        }
    }

private:

    std::vector<void(*)(const LLUUID& object_id, S32 side)> mSelectionCallbacks;
};

namespace
{
    LLGLTFMaterialOverrideDispatchHandler handle_gltf_override_message;
}

void LLGLTFMaterialList::applyOverrideMessage(LLMessageSystem* msg, const std::string& data_in)
{
    boost::iostreams::stream<boost::iostreams::array_source> str(data_in.data(), data_in.size());

    LLSD data;

    LLSDSerialize::fromNotation(data, str, data_in.length());

    const LLHost& host = msg->getSender();

    LLViewerRegion* region = LLWorld::instance().getRegion(host);
    llassert(region);

    if (region)
    {
        U32 local_id = data.get("id").asInteger();
        LLUUID id;
        gObjectList.getUUIDFromLocal(id, local_id, host.getAddress(), host.getPort());
        LLViewerObject* obj = gObjectList.findObject(id);

        // NOTE: obj may be null if the viewer hasn't heard about the object yet, cache update in any case

        if (obj && gShowObjectUpdates)
        { // display a cyan blip for override updates when "Show Updates to Objects" enabled
            LLColor4 color(0.f, 1.f, 1.f, 1.f);
            gPipeline.addDebugBlip(obj->getPositionAgent(), color);
        }

        const LLSD& tes = data["te"];
        const LLSD& od = data["od"];

        constexpr U32 MAX_TES = 45;
        bool has_te[MAX_TES] = { false };

        if (tes.isArray()) // NOTE: if no "te" array exists, this is a malformed message (null out all overrides will come in as an empty te array)
        {
            LLGLTFOverrideCacheEntry cache;
            cache.mLocalId = local_id;
            cache.mObjectId = id;
            cache.mRegionHandle = region->getHandle();

            auto count = llmin(tes.size(), MAX_TES);
            for (size_t i = 0; i < count; ++i)
            {
                LLGLTFMaterial* mat = new LLGLTFMaterial(); // setTEGLTFMaterialOverride and cache will take ownership
                mat->applyOverrideLLSD(od[i]);

                S32 te = tes[i].asInteger();

                has_te[te] = true;
                cache.mSides[te] = od[i];
                cache.mGLTFMaterial[te] = mat;

                if (obj)
                {
                    obj->setTEGLTFMaterialOverride(te, mat);
                    if (obj->getTE(te) && obj->getTE(te)->isSelected())
                    {
                        handle_gltf_override_message.doSelectionCallbacks(id, te);
                    }
                }
            }

            if (obj)
            { // null out overrides on TEs that shouldn't have them
                U32 count = llmin(obj->getNumTEs(), MAX_TES);
                for (U32 i = 0; i < count; ++i)
                {
                    LLTextureEntry* te = obj->getTE(i);
                    if (!has_te[i] && te && te->getGLTFMaterialOverride())
                    {
                        obj->setTEGLTFMaterialOverride(i, nullptr);
                        handle_gltf_override_message.doSelectionCallbacks(id, i);
                    }
                }
            }

            region->cacheFullUpdateGLTFOverride(cache);
            LL_DEBUGS("GLTF") << "GLTF Material Override: " << cache.mObjectId << " " << cache.mLocalId << " " << cache.mRegionHandle << " (sides:" << (cache.mSides.size()) << ")" << LL_ENDL;
        }

    }
}

void LLGLTFMaterialList::queueOverrideUpdate(const LLUUID& id, S32 side, LLGLTFMaterial* override_data)
{
#if 0
    override_list_t& overrides = mQueuedOverrides[id];

    if (overrides.size() < side + 1)
    {
        overrides.resize(side + 1);
    }

    overrides[side] = override_data;
#endif
}

void LLGLTFMaterialList::applyQueuedOverrides(LLViewerObject* obj)
{
    LL_PROFILE_ZONE_SCOPED;

    llassert(obj);

#if 0
    const LLUUID& id = obj->getID();
    auto iter = mQueuedOverrides.find(id);

    if (iter != mQueuedOverrides.end())
    {
        override_list_t& overrides = iter->second;
        for (int i = 0; i < overrides.size(); ++i)
        {
            if (overrides[i].notNull())
            {
                if (!obj->getTE(i))
                { // object is incomplete
                    return;
                }

                if (!obj->getTE(i)->getGLTFMaterial())
                {
                    // doesn't have its base GLTF material yet, don't apply override(yet)
                    return;
                }

                S32 status = obj->setTEGLTFMaterialOverride(i, overrides[i]);
                if (status == TEM_CHANGE_NONE)
                {
                    // can't apply this yet, since failure to change the material override
                    // probably means the base material is still being fetched.  leave in
                    // the queue for later
                    //obj->setDebugText("early out 3");
                    return;
                }

                if (obj->getTE(i)->isSelected())
                {
                    handle_gltf_override_message.doSelectionCallbacks(id, i);
                }
                // success!
                overrides[i] = nullptr;
            }
        }

        mQueuedOverrides.erase(iter);
    }
#else
    // the override cache is the authoritarian source of the most recent override data
    LLViewerRegion* regionp = obj->getRegion();
    if (regionp)
    {
        regionp->applyCacheMiscExtras(obj);
    }
#endif
}

void LLGLTFMaterialList::queueModify(const LLViewerObject* obj, S32 side, const LLGLTFMaterial* mat)
{
    if (obj && obj->getRenderMaterialID(side).notNull())
    {
        if (mat == nullptr)
        {
            sModifyQueue.push_back({ obj->getID(), side, LLGLTFMaterial(), false });
        }
        else
        {
            sModifyQueue.push_back({ obj->getID(), side, *mat, true });
        }
    }
}

void LLGLTFMaterialList::queueApply(const LLViewerObject* obj, S32 side, const LLUUID& asset_id)
{
    const LLGLTFMaterial* material_override = obj->getTE(side)->getGLTFMaterialOverride();
    if (material_override)
    {
        LLGLTFMaterial* cleared_override = new LLGLTFMaterial(*material_override);
        cleared_override->setBaseMaterial();
        sApplyQueue.push_back({ obj->getID(), side, asset_id, cleared_override });
    }
    else
    {
        sApplyQueue.push_back({ obj->getID(), side, asset_id, nullptr });
    }
}

void LLGLTFMaterialList::queueApply(const LLViewerObject* obj, S32 side, const LLUUID& asset_id, const std::string &override_json)
{
    if (asset_id.isNull() || override_json.empty())
    {
        // If there is no asset, there can't be an override
        queueApply(obj, side, asset_id);
    }
    else
    {
        sApplyQueue.push_back({ obj->getID(), side, asset_id, nullptr, override_json });
    }
}

void LLGLTFMaterialList::queueApply(const LLViewerObject* obj, S32 side, const LLUUID& asset_id, const LLGLTFMaterial* material_override)
{
    if (asset_id.isNull() || material_override == nullptr)
    {
        // If there is no asset, there can't be an override
        queueApply(obj, side, asset_id);
    }
    else
    {
        LLGLTFMaterial* material = new LLGLTFMaterial(*material_override);
        sApplyQueue.push_back({ obj->getID(), side, asset_id, material });
    }

    if (sUpdates.size() >= MAX_TASK_UPDATES)
    {
        LLCoros::instance().launch("modifyMaterialCoro",
            std::bind(&LLGLTFMaterialList::modifyMaterialCoro,
                gAgent.getRegionCapability("ModifyMaterialParams"),
                sUpdates,
                std::shared_ptr<CallbackHolder>(nullptr)));

        sUpdates = LLSD::emptyArray();
    }
}

void LLGLTFMaterialList::queueUpdate(const LLSD& data)
{
    llassert(is_valid_update(data));

    if (!sUpdates.isArray())
    {
        sUpdates = LLSD::emptyArray();
    }

    sUpdates[sUpdates.size()] = data;

    if (sUpdates.size() >= MAX_TASK_UPDATES)
    {
        LLCoros::instance().launch("modifyMaterialCoro",
            std::bind(&LLGLTFMaterialList::modifyMaterialCoro,
                gAgent.getRegionCapability("ModifyMaterialParams"),
                sUpdates,
                std::shared_ptr<CallbackHolder>(nullptr)));

        sUpdates = LLSD::emptyArray();
    }
}

void LLGLTFMaterialList::flushUpdates(void(*done_callback)(bool))
{
    std::shared_ptr<CallbackHolder> callback_holder;
    if (done_callback)
    {
        callback_holder = std::make_shared<CallbackHolder>(done_callback);
    }
    while (!sModifyQueue.empty() || !sApplyQueue.empty())
    {
        flushUpdatesOnce(callback_holder);
    }
}

void LLGLTFMaterialList::flushUpdatesOnce(std::shared_ptr<CallbackHolder> callback_holder)
{
    LLSD& data = sUpdates;

    size_t i = data.size();

    while (!sModifyQueue.empty() && i < MAX_TASK_UPDATES)
    {
        ModifyMaterialData& e = sModifyQueue.front();
#ifdef SHOW_ASSERT
        // validate object has a material id
        LLViewerObject* obj = gObjectList.findObject(e.object_id);
        llassert(obj && obj->getRenderMaterialID(e.side).notNull());
#endif

        data[i]["object_id"] = e.object_id;
        data[i]["side"] = e.side;

        if (e.has_override)
        {
            data[i]["gltf_json"] = e.override_data.asJSON();
        }
        else
        {
            // Clear all overrides
            data[i]["gltf_json"] = "";
        }

        llassert(is_valid_update(data[i]));
        ++i;
        sModifyQueue.pop_front();
    }

    while (!sApplyQueue.empty() && i < MAX_TASK_UPDATES)
    {
        ApplyMaterialAssetData& e = sApplyQueue.front();
        data[i]["object_id"] = e.object_id;
        data[i]["side"] = e.side;
        data[i]["asset_id"] = e.asset_id;
        if (e.override_data)
        {
            data[i]["gltf_json"] = e.override_data->asJSON();
        }
        else if (!e.override_json.empty())
        {
            data[i]["gltf_json"] = e.override_json;
        }
        else
        {
            // Clear all overrides
            data[i]["gltf_json"] = "";
        }

        llassert(is_valid_update(data[i]));
        ++i;
        sApplyQueue.pop_front();
    }

#if 0 // debug output of data being sent to capability
    std::stringstream str;
    LLSDSerialize::serialize(data, str, LLSDSerialize::LLSD_NOTATION, LLSDFormatter::OPTIONS_PRETTY);
    LL_INFOS() << "\n" << str.str() << LL_ENDL;
#endif

    if (sUpdates.size() > 0)
    {
        LLCoros::instance().launch("modifyMaterialCoro",
            std::bind(&LLGLTFMaterialList::modifyMaterialCoro,
                gAgent.getRegionCapability("ModifyMaterialParams"),
                sUpdates,
                callback_holder));

        sUpdates = LLSD::emptyArray();
    }
}

void LLGLTFMaterialList::addSelectionUpdateCallback(void(*update_callback)(const LLUUID& object_id, S32 side))
{
    handle_gltf_override_message.addCallback(update_callback);
}

class AssetLoadUserData
{
public:
    AssetLoadUserData() {}
    tinygltf::Model mModelIn;
    LLPointer<LLFetchedGLTFMaterial> mMaterial;
#if VS_NATIVE_VULKAN
    std::function<bool()> current;
    std::shared_ptr<const VSResourceBudget::Reservation> preparation;
#endif
};

#if VS_NATIVE_VULKAN
namespace
{
bool boundedNativeMaterialJSON(const std::string& json)
{
    if (json.empty() || json.size()>1024*1024) return false;
    unsigned depth=0;
    bool quoted=false,escaped=false;
    for (const char value:json)
    {
        if (quoted)
        {
            if (escaped) escaped=false;
            else if (value=='\\') escaped=true;
            else if (value=='"') quoted=false;
        }
        else if (value=='"') quoted=true;
        else if (value=='[' || value=='{')
        {
            if (++depth>64) return false;
        }
        else if (value==']' || value=='}')
        {
            if (!depth) return false;
            --depth;
        }
    }
    // TinyGLTF still performs all syntax and material interpretation. This is
    // only an input/depth admission bound before entering its shared parser.
    return !quoted && !depth;
}
bool decodeNativeMaterial(const LLUUID& id,LLAssetType::EType type,AssetLoadUserData& data)
{
    static VSResourceBudget preparation{128*1024*1024};
    LLFileSystem file(id,type,LLFileSystem::READ);
    const S32 size=file.getSize();
    if (size<=0 || size>1024*1024) return false;
    data.preparation=preparation.reserve(std::size_t(size)*64+65536);
    if (!data.preparation) return false;
    std::vector<char> buffer(size);
    if (!file.read(reinterpret_cast<U8*>(buffer.data()),size)) return false;
    LLSD asset;
    boost::iostreams::stream<boost::iostreams::array_source> stream(buffer.data(),buffer.size());
    if (!LLSDSerialize::deserialize(asset,stream,buffer.size()) ||
        !LLGLTFMaterial::isAcceptedVersion(asset["version"].asString()) ||
        asset["type"].asString()!=LLGLTFMaterial::ASSET_TYPE || !asset["data"].isString()) return false;
    const std::string json=asset["data"].asString();
    if (!boundedNativeMaterialJSON(json)) return false;
    tinygltf::TinyGLTF loader;
    // Simulator material channels name texture UUIDs. Remote material parsing
    // must not interpret those names as access to the local filesystem or
    // decode embedded image data; image subscribers own their shared fetches.
    tinygltf::FsCallbacks fs{
        [](const std::string&,void*) {return false;},
        [](const std::string& path,void*) {return path;},
        [](std::vector<unsigned char>*,std::string*,const std::string&,void*) {return false;},
        [](std::string*,const std::string&,const std::vector<unsigned char>&,void*) {return false;},
        [](std::size_t*,std::string*,const std::string&,void*) {return false;},nullptr};
    if (!loader.SetFsCallbacks(std::move(fs))) return false;
    loader.SetImageLoader([](tinygltf::Image*,int,std::string*,std::string*,int,int,const unsigned char*,int,void*) {return false;},nullptr);
    std::string error,warning;
    if (!loader.LoadASCIIFromString(&data.mModelIn,&error,&warning,json.data(),unsigned(json.size()),"")) return false;
    const auto& model=data.mModelIn;
    if (model.materials.size()!=1 || model.images.size()>16 || model.textures.size()>16 || model.samplers.size()>16 ||
        !model.buffers.empty() || !model.bufferViews.empty() || !model.meshes.empty() || !model.nodes.empty() ||
        !model.skins.empty() || !model.animations.empty() || !model.accessors.empty()) return false;
    for (const auto& image:model.images) if (!LLUUID::validate(image.uri)) return false;
    const auto& material=model.materials.front();
    if (material.pbrMetallicRoughness.baseColorFactor.size()!=4 || material.emissiveFactor.size()!=3) return false;
    for (const double value:material.pbrMetallicRoughness.baseColorFactor) if (!std::isfinite(value)) return false;
    for (const double value:material.emissiveFactor) if (!std::isfinite(value)) return false;
    if (material.extras.IsObject())
        for (const char* key:{"override_alpha_mode","override_double_sided"})
            if (material.extras.Has(key) && !material.extras.Get(key).IsBool()) return false;
    return true;
}
}
void LLGLTFMaterialList::onNativeAssetLoadComplete(const LLUUID& id,LLAssetType::EType type,void* input,S32 status)
{
    const auto data=std::shared_ptr<AssetLoadUserData>(static_cast<AssetLoadUserData*>(input));
    if (!data->current()) return;
    if (status!=LL_ERR_NOERR)
    {
        data->mMaterial->materialComplete(false);
        return;
    }
    const auto main=LL::WorkQueue::getInstance("mainloop"),general=LL::WorkQueue::getInstance("General");
    const bool posted=main->postTo(general,[id,type,data]()
    {
        try {return decodeNativeMaterial(id,type,*data);}
        catch (const std::exception& error)
        {
            LL_WARNS("GLTF")<<"Native material decode failed: "<<error.what()<<LL_ENDL;
            return false;
        }
    },[data](bool result)
    {
        if (!data->current()) return;
        if (result)
        {
            try {data->mMaterial->setFromModel(data->mModelIn,0);}
            catch (const std::exception& error)
            {
                LL_WARNS("GLTF")<<"Native material publication failed: "<<error.what()<<LL_ENDL;
                result=false;
            }
        }
        data->mMaterial->materialComplete(result);
    });
    if (!posted) data->mMaterial->materialComplete(false);
}

LLFetchedGLTFMaterial* LLGLTFMaterialList::getNativeMaterial(const LLUUID& id)
{
    auto found=mList.find(id);
    auto tracked=mNativeMaterials.find(id);
    // Local material ownership and its file watcher remain in the shared
    // registry. Do not retire or re-fetch a local replacement on account reset.
    if (found!=mList.end() && (tracked==mNativeMaterials.end() || tracked->second.material!=found->second.get())) return found->second;
    if (found==mList.end())
    {
        if (tracked!=mNativeMaterials.end()) mNativeMaterials.erase(tracked);
        for (auto iter=mNativeMaterials.begin();mNativeMaterials.size()>=4096 && iter!=mNativeMaterials.end();)
        {
            const auto cached=mList.find(iter->first);
            if (cached==mList.end() || cached->second.get()!=iter->second.material) iter=mNativeMaterials.erase(iter);
            else if (!cached->second->isFetching() && cached->second->getNumRefs()==1)
            {mList.erase(cached);iter=mNativeMaterials.erase(iter);}
            else ++iter;
        }
        if (mNativeMaterials.size()>=4096) return nullptr;
        LLPointer<LLFetchedGLTFMaterial> material=new LLFetchedGLTFMaterial;
        found=mList.emplace(id,material).first;
        tracked=mNativeMaterials.emplace(id,NativeMaterial{material.get()}).first;
    }
    auto& entry=tracked->second;
    auto* material=entry.material;
    const double now=LLTimer::getTotalSeconds();
    if (material->isLoaded() || material->isFetching() || entry.attempts>=3 || now<entry.retryAt || !gAssetStorage) return material;
    const auto owner=VSNativeSession::active();
    if (!owner || (owner->phase()!=VSNativeSession::Phase::Connected && owner->phase()!=VSNativeSession::Phase::Connecting)) return material;
    auto* request=new AssetLoadUserData;
    request->mMaterial=material;
    request->current=owner->accountGuard(true);
    material->materialBegin();
    ++entry.attempts;entry.retryAt=now+5*entry.attempts;
    gAssetStorage->getAssetData(id,LLAssetType::AT_MATERIAL,onAssetLoadComplete,request);
    return material;
}
void LLGLTFMaterialList::resetNativeMaterials()
{
    for (const auto& [id,entry]:mNativeMaterials)
        if (const auto found=mList.find(id);found!=mList.end() && found->second.get()==entry.material) mList.erase(found);
    mNativeMaterials.clear();
}
#endif

void LLGLTFMaterialList::onAssetLoadComplete(const LLUUID& id, LLAssetType::EType asset_type, void* user_data, S32 status, LLExtStat ext_status)
{
    LL_PROFILE_ZONE_NAMED("gltf asset callback");
    AssetLoadUserData* asset_data = (AssetLoadUserData*)user_data;
#if VS_NATIVE_VULKAN
    if (asset_data->current)
    {
        onNativeAssetLoadComplete(id,asset_type,asset_data,status);
        return;
    }
#endif

    if (status != LL_ERR_NOERR)
    {
        LL_WARNS("GLTF") << "Error getting material asset data: " << LLAssetStorage::getErrorString(status) << " (" << status << ")" << LL_ENDL;
        asset_data->mMaterial->materialComplete(false);
        delete asset_data;
    }
    else
    {

        LL::WorkQueue::ptr_t main_queue = LL::WorkQueue::getInstance("mainloop");
        LL::WorkQueue::ptr_t general_queue = LL::WorkQueue::getInstance("General");

        main_queue->postTo(
            general_queue,
            [id, asset_type, asset_data]() // Work done on general queue
        {
            std::vector<char> buffer;
            {
                LL_PROFILE_ZONE_NAMED("gltf read asset");
                LLFileSystem file(id, asset_type, LLFileSystem::READ);
                auto size = file.getSize();
                if (!size)
                {
                    return false;
                }

                buffer.resize(size);
                file.read((U8*)&buffer[0], static_cast<S32>(buffer.size()));
            }

            {
                LL_PROFILE_ZONE_NAMED("gltf deserialize asset");

                LLSD asset;

                // read file into buffer
                boost::iostreams::stream<boost::iostreams::array_source> str(buffer.data(), buffer.size());
                if (LLSDSerialize::deserialize(asset, str, buffer.size()))
                {
                    if (asset.has("version") && LLGLTFMaterial::isAcceptedVersion(asset["version"].asString()))
                    {
                        if (asset.has("type") && asset["type"].asString() == LLGLTFMaterial::ASSET_TYPE)
                        {
                            if (asset.has("data") && asset["data"].isString())
                            {
                                std::string data = asset["data"];

                                std::string warn_msg, error_msg;

                                LL_PROFILE_ZONE_SCOPED;
                                tinygltf::TinyGLTF gltf;

                                if (!gltf.LoadASCIIFromString(&asset_data->mModelIn, &error_msg, &warn_msg, data.c_str(), static_cast<U32>(data.length()), ""))
                                {
                                    LL_WARNS("GLTF") << "Failed to decode material asset: "
                                        << LL_NEWLINE
                                        << warn_msg
                                        << LL_NEWLINE
                                        << error_msg
                                        << LL_ENDL;
                                    return false;
                                }
                                return true;
                            }
                        }
                    }
                }
                else
                {
                    LL_WARNS("GLTF") << "Failed to deserialize material LLSD" << LL_ENDL;
                }
            }

            return false;
        },
            [id, asset_data](bool result) // Callback to main thread
            {

            if (result)
            {
                asset_data->mMaterial->setFromModel(asset_data->mModelIn, 0/*only one index*/);
            }
            else
            {
                LL_DEBUGS("GLTF") << "Failed to get material " << id << LL_ENDL;
            }

            asset_data->mMaterial->materialComplete(true);

            delete asset_data;
        });
    }
}

LLFetchedGLTFMaterial* LLGLTFMaterialList::getMaterial(const LLUUID& id)
{
    LL_PROFILE_ZONE_SCOPED;
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active()) return getNativeMaterial(id);
#endif
    uuid_mat_map_t::iterator iter = mList.find(id);
    if (iter == mList.end())
    {
        LL_PROFILE_ZONE_NAMED("gltf fetch")
        LLFetchedGLTFMaterial* mat = new LLFetchedGLTFMaterial();
        mList[id] = mat;

        if (!mat->mFetching)
        {
            mat->materialBegin();

            AssetLoadUserData *user_data = new AssetLoadUserData();
            user_data->mMaterial = mat;

            gAssetStorage->getAssetData(id, LLAssetType::AT_MATERIAL, onAssetLoadComplete, (void*)user_data);
        }

        return mat;
    }

    return iter->second;
}

void LLGLTFMaterialList::addMaterial(const LLUUID& id, LLFetchedGLTFMaterial* material)
{
    mList[id] = material;
}

void LLGLTFMaterialList::removeMaterial(const LLUUID& id)
{
    mList.erase(id);
}

void LLGLTFMaterialList::flushMaterials()
{
    // Similar variant to what textures use
    static const S32 MIN_UPDATE_COUNT = gSavedSettings.getS32("TextureFetchUpdateMinCount");       // default: 32
    //update MIN_UPDATE_COUNT or 5% of materials, whichever is greater
    U32 update_count = llmax((U32)MIN_UPDATE_COUNT, (U32)mList.size() / 20);
    update_count = llmin(update_count, (U32)mList.size());

    const F64 MAX_INACTIVE_TIME = 30.f;
    F64 cur_time = LLTimer::getTotalSeconds();

    // advance iter one past the last key we updated
    uuid_mat_map_t::iterator iter = mList.find(mLastUpdateKey);
    if (iter != mList.end()) {
        ++iter;
    }

    while (update_count-- > 0)
    {
        if (iter == mList.end())
        {
            iter = mList.begin();
        }

        LLPointer<LLFetchedGLTFMaterial> material = iter->second;
        if (material->getNumRefs() == 2) // this one plus one from the list
        {

            if (!material->mActive
                && cur_time > material->mExpectedFlusTime)
            {
                iter = mList.erase(iter);
            }
            else
            {
                if (material->mActive)
                {
                    material->mExpectedFlusTime = cur_time + MAX_INACTIVE_TIME;
                    material->mActive = false;
                }
                ++iter;
            }
        }
        else
        {
            material->mActive = true;
            ++iter;
        }
    }

    if (iter != mList.end())
    {
        mLastUpdateKey = iter->first;
    }
    else
    {
        mLastUpdateKey.setNull();
    }

    {
        using namespace LLStatViewer;
        sample(NUM_MATERIALS, static_cast<F64>(mList.size()));
    }
}

// static
void LLGLTFMaterialList::modifyMaterialCoro(std::string cap_url, LLSD overrides, std::shared_ptr<CallbackHolder> callback_holder)
{
    LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t
        httpAdapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("modifyMaterialCoro", httpPolicy);
    LLCore::HttpRequest::ptr_t httpRequest = std::make_shared<LLCore::HttpRequest>();
    LLCore::HttpOptions::ptr_t httpOpts = std::make_shared<LLCore::HttpOptions>();
    LLCore::HttpHeaders::ptr_t httpHeaders;

    httpOpts->setFollowRedirects(true);

    LL_DEBUGS("GLTF") << "Applying override via ModifyMaterialParams cap: " << overrides << LL_ENDL;

    LLSD result = httpAdapter->postAndSuspend(httpRequest, cap_url, overrides, httpOpts, httpHeaders);

    LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

    bool success = true;
    if (!status)
    {
        LL_WARNS("GLTF") << "Failed to modify material." << LL_ENDL;
        success = false;
    }
    else if (!result["success"].asBoolean())
    {
        LL_WARNS("GLTF") << "Failed to modify material: " << result["message"] << LL_ENDL;
        success = false;
    }

    if (callback_holder)
    {
        // Set to false even if something went through
        // since at the moment it get used to refresh UI
        // if update failed
        callback_holder->mSuccess &= success;
    }
}

