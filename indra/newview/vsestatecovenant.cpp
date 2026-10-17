// Shared covenant transport keeps original notecard parsing and view controllers. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsestatecovenant.h"
#include "vsnativeim.h"
#include "llagent.h"
#include "llassetstorage.h"
#include "llfilesystem.h"
#include "message.h"
#include "llslurl.h"
#include "lltrans.h"
#include "llview.h"
#include "llviewertexteditor.h"
#include <algorithm>
#include <memory>
#include <vector>
namespace
{
struct Request
{
    LLHandle<LLView> view;
    VSRegionContext region;
    std::function<bool()> account,target;
    VSCovenantCallbacks callbacks;
    LLUUID asset;
    U64 publication=0;
    bool active=true;
    bool current()const{return active && view.get() && account() && region.current() && (!target || target());}
};
std::vector<std::shared_ptr<Request>> requests;
struct AssetReply
{
    std::vector<std::pair<std::shared_ptr<Request>,U64>> owners;
    LLUUID asset;
};
template<class Callback>
void forCurrent(const AssetReply& reply,Callback action)
{
    for(const auto& [owner,publication]:reply.owners)
        if(owner->current() && owner->publication==publication && owner->asset==reply.asset)action(*owner);
}
void loaded(const LLUUID& asset_uuid,LLAssetType::EType type,void* user_data,S32 status,LLExtStat ext_status)
{
    std::unique_ptr<AssetReply> reply(static_cast<AssetReply*>(user_data));
    if(!reply || reply->asset!=asset_uuid)return;
    bool wanted=false;
    forCurrent(*reply,[&](const Request&){wanted=true;});
    if(!wanted)return;
        std::string covenant_text;
    std::unique_ptr<LLViewerTextEditor> editorp;
    if (0 == status)
    {
        LLFileSystem file(asset_uuid, type, LLFileSystem::READ);

        S32 file_length = file.getSize();

        std::vector<char> buffer(file_length+1);
        file.read((U8*)&buffer[0], file_length);
        // put a EOS at the end
        buffer[file_length] = '\0';

        if( (file_length > 19) && !strncmp( &buffer[0], "Linden text version", 19 ) )
        {
            LLViewerTextEditor::Params params;
            params.name("temp");
            params.max_text_length(file_length+1);
            LLViewerTextEditor * editor = LLUICtrlFactory::create<LLViewerTextEditor> (params);
            if( !editor->importBuffer( &buffer[0], file_length+1 ) )
            {
                LL_WARNS("Messaging") << "Problem importing estate covenant." << LL_ENDL;
                covenant_text = "Problem importing estate covenant.";
                delete editor;
            }
            else
            {
                // Version 0 (just text, doesn't include version number)
                editorp.reset(editor); // Use covenant from editorp;
            }
        }
        else
        {
            LL_WARNS("Messaging") << "Problem importing estate covenant: Covenant file format error." << LL_ENDL;
            covenant_text = "Problem importing estate covenant: Covenant file format error.";
        }
    }
    else
    {
        if( LL_ERR_ASSET_REQUEST_NOT_IN_DATABASE == status ||
            LL_ERR_FILE_EMPTY == status)
        {
            covenant_text = "Estate covenant notecard is missing from database.";
        }
        else if (LL_ERR_INSUFFICIENT_PERMISSIONS == status)
        {
            covenant_text = "Insufficient permissions to view estate covenant.";
        }
        else
        {
            covenant_text = "Unable to load estate covenant at this time.";
        }

        LL_WARNS("Messaging") << "Problem loading notecard: " << status << LL_ENDL;
    }

    forCurrent(*reply,[&](Request& owner)
    {
        if(editorp)
        {
            if(owner.callbacks.content)owner.callbacks.content(editorp.get(),asset_uuid);
        }
        else if(owner.callbacks.text)owner.callbacks.text(covenant_text,asset_uuid);
    });
}
}
void vs_request_covenant(const VSRegionContext& region,LLView& view,VSCovenantCallbacks callbacks,std::function<bool()> targetCurrent)
{
    if(!region.current() || !gMessageSystem)return;
    for(auto& owner:requests)
        if(owner->view.get()==&view)owner->active=false;
    requests.erase(std::remove_if(requests.begin(),requests.end(),[](const auto& owner){return !owner->current();}),requests.end());
    auto owner=std::make_shared<Request>();owner->view=view.getHandle();owner->region=region;
    owner->account=vs_native_im_guard();owner->target=std::move(targetCurrent);owner->callbacks=std::move(callbacks);
    if(!owner->current())return;
    requests.push_back(owner);
    gMessageSystem->newMessage("EstateCovenantRequest");
    gMessageSystem->nextBlockFast(_PREHASH_AgentData);
    gMessageSystem->addUUIDFast(_PREHASH_AgentID,gAgent.getID());
    gMessageSystem->addUUIDFast(_PREHASH_SessionID,gAgent.getSessionID());
    gMessageSystem->sendReliable(region.getHost());
}
void vs_receive_covenant(LLMessageSystem* msg)
{
    if(!msg)return;
    // No request ID/parcel ID exists in this reference reply. Current simulator,
    // account and requesting view/selection are the available ownership boundary.
    auto reply=std::make_unique<AssetReply>();
    msg->getUUID("Data","CovenantID",reply->asset);
    const auto host=msg->getSender();
    requests.erase(std::remove_if(requests.begin(),requests.end(),[](const auto& owner){return !owner->current();}),requests.end());
    for(const auto& owner:requests)
        if(owner->region.getHost()==host)
        {
            owner->asset=reply->asset;
            reply->owners.emplace_back(owner,++owner->publication);
        }
    if(reply->owners.empty())return;
    LLUUID estate_owner_id;std::string estate_name;U32 covenant_timestamp;
    msg->getUUID("Data","EstateOwnerID",estate_owner_id);
    msg->getString("Data","EstateName",estate_name);
    msg->getU32("Data","CovenantTimestamp",covenant_timestamp);
    const std::string owner_name=LLSLURL("agent",estate_owner_id,"inspect").getSLURLString();
        std::string last_modified;
    if (covenant_timestamp == 0)
    {
        last_modified = LLTrans::getString("covenant_last_modified")+LLTrans::getString("never_text");
    }
    else
    {
        last_modified = LLTrans::getString("covenant_last_modified")+"["
                        +LLTrans::getString("LTimeWeek")+"] ["
                        +LLTrans::getString("LTimeMonth")+"] ["
                        +LLTrans::getString("LTimeDay")+"] ["
                        +LLTrans::getString("LTimeHour")+"]:["
                        +LLTrans::getString("LTimeMin")+"]:["
                        +LLTrans::getString("LTimeSec")+"] ["
                        +LLTrans::getString("LTimeYear")+"]";
        LLSD substitution;
        substitution["datetime"] = (S32) covenant_timestamp;
        LLStringUtil::format (last_modified, substitution);
    }


    forCurrent(*reply,[&](Request& owner){if(owner.callbacks.estateName)owner.callbacks.estateName(estate_name);});
    forCurrent(*reply,[&](Request& owner){if(owner.callbacks.ownerName)owner.callbacks.ownerName(owner_name);});
    forCurrent(*reply,[&](Request& owner){if(owner.callbacks.lastModified)owner.callbacks.lastModified(last_modified);});
    bool wanted=false;
    forCurrent(*reply,[&](const Request&){wanted=true;});
    if(!wanted)return;
    if(reply->asset.notNull())
    {
        if(!gAssetStorage)return;
        const auto asset=reply->asset;
        gAssetStorage->getEstateAsset(host,gAgent.getID(),gAgent.getSessionID(),asset,LLAssetType::AT_NOTECARD,
                                    ET_Covenant,loaded,reply.release(),true);
    }
    else
    {
        const auto text=LLTrans::getString(estate_owner_id.isNull()?"RegionNoCovenant":"RegionNoCovenantOtherOwner");
        forCurrent(*reply,[&](Request& owner){if(owner.callbacks.text)owner.callbacks.text(text,reply->asset);});
    }
}
void vs_reset_covenants()
{
    for(auto& owner:requests)owner->active=false;
    requests.clear();
}
