// Native transport admission with the existing IM model and skinned UI. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsnativeim.h"
#include "lluploaddialog.h"
#include "llviewerdisplayname.h"
#include "vsnativesession.h"
#include "llimview.h"
#include "lggcontactsets.h"
#include "llgroupmgr.h"
#include "llgroupactions.h"
#include "llavatarpropertiesprocessor.h"
#include "llviewermessage.h"
#include "llagent.h"
#include "llmutelist.h"
#include "message.h"
#include "llinstantmessage.h"
#include "fsfloaterim.h"
#include "llfloaterreg.h"
#include "llfloater.h"
#include "llfloaterperms.h"
#include "llfloatergroupinvite.h"
#include "llfloatergroupbulkban.h"

namespace
{
U64 sOfflineGeneration = 0;
const LLUUID sNotificationEpoch = LLUUID::generateNewID();
}
void vs_native_im_receive(LLMessageSystem* message, void** context)
{
    const auto session = VSNativeSession::active();
    if (!session || session->phase() != VSNativeSession::Phase::Connected ||
        session->host() != message->getSender() || !gIMMgr) return;
    LLUUID target;
    U8 dialog;
    message->getUUID("MessageBlock", "ToAgentID", target);
    message->getU8("MessageBlock", "Dialog", dialog);
    if (target != gAgentID) return;
    // The common processor retains policy, history, notifications, autoresponse
    // and the real FS conversation UI. Scene-producing IM dialogs are separate
    // features and cannot enter through this text transport admission.
    switch (EInstantMessage(dialog))
    {
    case IM_NOTHING_SPECIAL:
    case IM_SESSION_SEND:
    case IM_TYPING_START:
    case IM_TYPING_STOP:
    case IM_DO_NOT_DISTURB_AUTO_RESPONSE:
    case IM_FRIENDSHIP_OFFERED:
    case IM_FRIENDSHIP_ACCEPTED:
    case IM_FRIENDSHIP_DECLINED_DEPRECATED:
    case IM_GROUP_INVITATION:
    case IM_GROUP_NOTICE:
    case IM_GROUP_NOTICE_REQUESTED:
    case IM_INVENTORY_OFFERED:
    case IM_INVENTORY_ACCEPTED:
    case IM_INVENTORY_DECLINED:
    case IM_TASK_INVENTORY_OFFERED:
    case IM_TASK_INVENTORY_ACCEPTED:
    case IM_TASK_INVENTORY_DECLINED:
    case IM_MESSAGEBOX:
        process_improved_im(message, context);
        break;
    default:
        LL_WARNS("NativeIM") << "Unadmitted IM dialog " << unsigned(dialog) << LL_ENDL;
    }
}

std::function<bool()> vs_native_im_guard()
{
    const auto owner = VSNativeSession::active();
    if (!owner) return [] { return true; }; // Shared OpenGL callers.
    const std::weak_ptr<VSNativeSession> weak = owner;
    const U64 generation = owner->generation();
    return [weak, generation]
    {
        const auto session = weak.lock();
        return session && session->generation() == generation &&
            session->phase() == VSNativeSession::Phase::Connected;
    };
}
void vs_native_im_install(LLMessageSystem& messages)
{
    messages.setHandlerFunc("ImprovedInstantMessage", vs_native_im_receive);
}
void vs_native_im_request_offline()
{
    const auto owner = VSNativeSession::active();
    if (!owner || owner->phase() != VSNativeSession::Phase::Connected ||
        !gIMMgr || !gMessageSystem || sOfflineGeneration == owner->generation() ||
        !LLMuteList::getInstance()->updateLoadState()) return;
    // The supported legacy delivery retains transaction/session IDs and uses
    // the same admitted ImprovedInstantMessage path as live messages. It does
    // not require an avatar or an LLViewerRegion merely to retrieve saved text.
    gMessageSystem->newMessage("RetrieveInstantMessages");
    gMessageSystem->nextBlock("AgentData");
    gMessageSystem->addUUID("AgentID", gAgentID);
    gMessageSystem->addUUID("SessionID", gAgentSessionID);
    if (gMessageSystem->sendReliable(owner->host()) > 0)
        sOfflineGeneration = owner->generation();
}
void vs_native_im_reset()
{
    LLUploadDialog::modalUploadFinished();
    sOfflineGeneration = 0;
    LLFloaterPermsDefault::setCapSent(false);
    LLFloaterGroupInvite::destroyAccountFloaters();
    LLFloaterGroupBulkBan::destroyAccountFloaters();
    // Panels retain pointers into shared group/profile account data. Destroy
    // them while those services still exist, before discarding the old login.
    for (const char* name : { "avatar_picker", "group_picker", "pay_resident", "display_name", "pay_object", "upload_image", "upload_sound", "properties", "item_properties", "change_item_thumbnail", "preview_sound", "preview_script", "script_colors", "fs_partial_inventory", "preview_texture", "preview_notecard", "preview_conversation", "conversation", "fs_add_contact", "fs_contact_set_config", "fs_blocklist", "mute_object_by_name", "publish_classified", "fs_group_titles", "vs_group_search", "fs_group", "profile", "prefs_translation", "prefs_autoreplace", "prefs_spellchecker", "prefs_spellchecker_import", "perms_default", "preferences", "prefs_proxy", "keybind_dialog" })
    {
        const auto floaters = LLFloaterReg::getFloaterList(name);
        for (auto* floater : floaters)
            if (floater) LLFloaterReg::destroyInstance(name, floater->getKey());
    }
    if (LGGContactSets::instanceExists()) LGGContactSets::instance().resetAccount();
    LLFloaterReg::destroyInstance("display_name");
    LLGroupActions::resetAccountRequests();
    LLViewerDisplayName::resetAccountRequests();
    if (LLGroupMgr::instanceExists()) LLGroupMgr::instance().clearAccountGroups();
    if (LLAvatarPropertiesProcessor::instanceExists())
        LLAvatarPropertiesProcessor::instance().resetAccountRequests();
    if (!gIMMgr || !LLIMModel::instanceExists()) return;
    // Expire model sessions, initialization timers, pending invitations and UI
    // observers together. The shared implementation owns their removal order.
    gIMMgr->disconnectAllSessions();
}

void vs_native_im_stamp_notification(LLSD& payload)
{
    if (auto owner = VSNativeSession::active())
    {
        payload["vs_notification_epoch"] = sNotificationEpoch;
        payload["vs_notification_generation"] = std::to_string(owner->generation());
    }
}
bool vs_native_im_notification_current(const LLSD& payload)
{
    const auto owner = VSNativeSession::active();
    if (!owner) return true;
    if (owner->phase() != VSNativeSession::Phase::Connected) return false;
    // Unstamped legacy persisted notifications are restored from the active
    // account's storage. Fresh native asynchronous work carries an epoch token
    // which cannot authorize an action after teardown or application restart.
    return !payload.has("vs_notification_epoch") ||
        (payload["vs_notification_epoch"].asUUID() == sNotificationEpoch &&
         payload["vs_notification_generation"].asString() == std::to_string(owner->generation()));
}
