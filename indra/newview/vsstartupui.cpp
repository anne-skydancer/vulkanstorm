// Required native login/progress/alert controls; optional viewer UI remains gated. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "lltabcontainer.h"
#include "fsfloaterim.h"
#include "fsfloaterimcontainer.h"
#include "fsfloatercontacts.h"
#include "fsfloaterpartialinventory.h"
#include "fspanelcontactsets.h"
#include "fschathistory.h"
#include "fspanelimcontrolpanel.h"
#include "fsscrolllistctrl.h"
#include "llchicletbar.h"
#include "llchiclet.h"
#include "llavatarlist.h"
#include "llavatarlistitem.h"
#include "llavatariconctrl.h"
#include "llgroupiconctrl.h"
#include "llgrouplist.h"
#include "llchatentry.h"
#include "llfiltereditor.h"
#include "llmenubutton.h"
#include "lloutputmonitorctrl.h"
#include "llsliderctrl.h"
#include "llflatlistview.h"
#include "llmultifloater.h"
#include "lldockablefloater.h"
#include "llsyswellwindow.h"
#include "llsyswellitem.h"
#include "llfloaternotificationstabbed.h"
#include "llnotificationlistview.h"
#include "llnotificationlistitem.h"
#include "lltoastgroupnotifypanel.h"
#include "lltoastscriptquestion.h"
#include "lltoastpanel.h"
#include "lltoastimpanel.h"
#include "lltoastnotifypanel.h"
#include "llfloateravatarpicker.h"
#include "llfloatergroups.h"
#include "llpanelemojicomplete.h"
#include "fsfloatergroup.h"
#include "fsfloatergrouptitles.h"
#include "vsgroupsearch.h"
#include "llpaneldirgroups.h"
#include "llsearchcombobox.h"
#include "llfloaterprofile.h"
#include "llfloaterdisplayname.h"
#include "llfloaterpay.h"
#include "llfloaterimagepreview.h"
#include "llfloaternamedesc.h"
#include "lluploaddialog.h"
#include "llpanelprofile.h"
#include "llfloaterpreference.h"
#include "fspanelprefs.h"
#include "fspanelpreferenceuisounds.h"
#include "llpanelvoicedevicesettings.h"
#include "llsearcheditor.h"
#include "llslider.h"
#include "llcolorswatch.h"
#include "fsdroptarget.h"
#include "llpanelprofilepicks.h"
#include "llpanelprofileclassifieds.h"
#include "llpanelavatar.h"
#include "llfloaterprofiletexture.h"
#include "lltexturectrl.h"
#include "llthumbnailctrl.h"
#include "llnamelistctrl.h"
#include "llloadingindicator.h"
#include "llspinctrl.h"
#include "llaccordionctrl.h"
#include "llaccordionctrltab.h"
#include "lltoggleablemenu.h"
#include "llpanelgroup.h"
#include "llpanelgroupcreate.h"
#include "llfloatergroupinvite.h"
#include "llpanelgroupinvite.h"
#include "llfloatergroupbulkban.h"
#include "llpanelgroupbulkban.h"
#include "llpanelgroupbulk.h"
#include "llpanelgroupgeneral.h"
#include "llpanelgrouproles.h"
#include "llpanelgroupnotices.h"
#include "llpanelgrouplandmoney.h"
#include "llpanelgroupexperiences.h"
#include "llpanelexperiences.h"
#include "llfloatercolorpicker.h"
#include "llfloatertranslationsettings.h"
#include "llfloaterautoreplacesettings.h"
#include "llfloaterspellchecksettings.h"
#include "llfloaterperms.h"
#include "llpreview.h"
#include "llpreviewnotecard.h"
#include "llpreviewtexture.h"
#include "llpreviewsound.h"
#include "llpreviewscript.h"
#include "llscripteditor.h"
#include "fslslpreprocviewer.h"
#include "llfloatergotoline.h"
#include "llfloaterscriptedprefs.h"
#include "llfloaterproperties.h"
#include "llfloatermarketplacelistings.h"
#include "llsidepaneliteminfo.h"
#include "llfloaterchangeitemthumbnail.h"
#include "llinventorypanel.h"
#include "llfolderview.h"
#include "llfolderviewitem.h"
#include "fsfloaterblocklist.h"
#include "fspanelblocklist.h"
#include "fsfloateraddtocontactset.h"
#include "fsfloatercontactsetconfiguration.h"
#include "llpanelblockedlist.h"
#include "llfloaterconversationlog.h"
#include "llfloaterconversationpreview.h"
#include "llconversationloglist.h"
#include "llconversationloglistitem.h"
#include "vsstartupui.h"
#include "vsplainchat.h"
#include "llnotificationsutil.h"
#include "lltrans.h"
#include "vsuiadmission.h"
#include "llrootview.h"
#include "llviewermenu.h"
#include "llmenugl.h"
#include "llpopupview.h"
#include "llpanel.h"
#include "llbutton.h"
#include "lldndbutton.h"
#include "llflyoutbutton.h"
#include "llbadge.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "lliconctrl.h"
#include "lllineeditor.h"
#include "lltexteditor.h"
#include "llviewertexteditor.h"
#include "lltextbox.h"
#include "llconsole.h"
#include "lllayoutstack.h"
#include "llprogressbar.h"
#include "llscrollbar.h"
#include "llscrollcontainer.h"
#include "llscrolllistctrl.h"
#include "llscrolllistcolumn.h"
#include "llviewborder.h"
#include <algorithm>
#include <iterator>
#include "lltoolbarview.h"
#include "llpanelpeople.h"
#include "fsfloaternearbychat.h"
#include "llsidetraypanelcontainer.h"
#include "llfloatersearch.h"
#include "llfloatersearchreplace.h"
#include "llfloaterwebcontent.h"
#include "llfloatertoybox.h"
#include "llnavigationbar.h"
#include "llfloatersidepanelcontainer.h"
#include "llsidepanelinventory.h"
#include "llpanelmaininventory.h"
#include "llpanelmarketplaceinbox.h"
#include "llpanelmarketplaceinboxinventory.h"
#include "llinventorygallery.h"
#include "llstatusbar.h"
#include "llhints.h"
#include "llinspectremoteobject.h"
#include "llstatgraph.h"
#include "llpanelvolumepulldown.h"
#include "llpanelpulldown.h"
#include "lltoolbar.h"
#include "lllocationinputctrl.h"
#include "llurllineeditorctrl.h"
#include "fsnearbychatcontrol.h"
#include "fsnearbychatvoicemonitor.h"
#include "llfavoritesbar.h"
#include "llfloater.h"
#include "llfloaterabout.h"
#include "lltabcontainer.h"
#include "llmediactrl.h"
#include "llradiogroup.h"
#include "llwindowshade.h"
#include "llmodaldialog.h"
#include "llfloatertos.h"
#include "lltoast.h"
#include "lltoastalertpanel.h"
#include "llscreenchannel.h"
#include "llresizebar.h"
#include "llresizehandle.h"
#include "lldraghandle.h"
std::unique_ptr<VSUIAdmission> vs_startup_ui_admission()
{
    return std::make_unique<VSUIAdmission>(
        [](const std::type_info& t)
        {
            const bool admitted = t == typeid(LLFloaterSidePanelContainer) || t == typeid(LLSidepanelInventory) ||
                t == typeid(LLPanelMainInventory) || LLPanelMainInventory::isNativeInternalType(t) ||
                t == typeid(LLPanelMarketplaceInbox) || t == typeid(LLInboxInventoryPanel) ||
                t == typeid(LLInboxFolderViewFolder) || t == typeid(LLInboxFolderViewItem) || t == typeid(LLInventoryGallery) || t == typeid(LLInventoryGalleryItem) ||
                LLInventoryPanel::isNativeInternalType(t) || LLInspectRemoteObjectUtil::isNativeInternalType(t) || LLHints::isNativeInternalType(t) || t == typeid(LLStatusBar) || t == typeid(LLStatGraph) ||
                t == typeid(LLPanelVolumePulldown) || t == typeid(LLPanelPulldown) || t == typeid(LLToolBarView) || t == typeid(LLToolBar) ||
                t == typeid(LLToolBarVertical) || t == typeid(LLToolBarButton) || LLToolBar::isNativeInternalType(t) || LLNavigationBar::isNativeInternalType(t) || t == typeid(FSNearbyChatControl) || t == typeid(FSNearbyChatVoiceControl) || t == typeid(FSFloaterNearbyChat) || t == typeid(LLPanelPeople) || t == typeid(LLSideTrayPanelContainer) || t == typeid(LLFloaterSearchReplace) || t == typeid(LLFloaterSearch) || t == typeid(LLFloaterWebContent) || t == typeid(LLFloaterToybox) ||
                t == typeid(LLPullButton) || t == typeid(LLLocationInputCtrl) || t == typeid(LLURLLineEditor) ||
                t == typeid(LLFavoritesBarCtrl) || LLFavoritesBarCtrl::isNativeInternalType(t) ||
                t == typeid(LLMultiPreview) || t == typeid(LLPreview) || t == typeid(LLPreviewTexture) || t == typeid(LLPreviewNotecard) || t == typeid(LLInventoryPanel) || t == typeid(LLAssetFilteredInventoryPanel) ||
                t == typeid(LLInventorySingleFolderPanel) || t == typeid(LLFolderView) ||
                t == typeid(LLFolderViewItem) || t == typeid(LLFolderViewFolder) ||
                t == typeid(LLFolderViewScrollContainer) || t == typeid(FSFloaterBlocklist) || t == typeid(FSPanelBlockList) ||
                t == typeid(FSFloaterAddToContactSet) || t == typeid(FSFloaterContactSetConfiguration) ||
                t == typeid(LLFloaterGetBlockedObjectName) || t == typeid(LLFloaterConversationLog) ||
                t == typeid(LLFloaterConversationPreview) || t == typeid(LLConversationLogList) ||
                t == typeid(LLConversationLogListItem) || t == typeid(LLFloaterTranslationSettings) || t == typeid(LLFloaterAutoReplaceSettings) ||
                t == typeid(LLFloaterSpellCheckerSettings) || t == typeid(LLFloaterSpellCheckerImport) ||
                t == typeid(LLFloaterPermsDefault) || t == typeid(LLFloaterPreference) ||
                t == typeid(LLPanelPreference) ||
                t == typeid(LLPanelPreferenceGraphics) ||
                t == typeid(LLPanelPreferenceControls) ||
                t == typeid(LLPanelPreferenceCrashReports) ||
                t == typeid(LLPanelPreferenceSkins) ||
                t == typeid(FSPanelPreferenceBackup) ||
                t == typeid(LLPanelPreferenceOpensim) ||
                t == typeid(FSPanelPreferenceSounds) ||
                t == typeid(FSPanelPrefs) ||
                t == typeid(FSPanelPreferenceUISounds) ||
                t == typeid(LLPanelVoiceDeviceSettings) ||
                t == typeid(LLSearchEditor) ||
                t == typeid(LLSlider) ||
                t == typeid(LLColorSwatchCtrl) ||
                t == typeid(FSCopyTransInventoryDropTarget) ||
                t == typeid(FSEmbeddedItemDropTarget) ||
                t == typeid(LLSetKeyBindDialog) ||
                t == typeid(LLFloaterPreferenceProxy) ||
                t == typeid(LLFloaterTexturePicker) || t == typeid(LLFloaterColorPicker) ||
                LLFloaterPreference::isPrivacyPanelType(t) ||
                t == typeid(FSFloaterGroup) || t == typeid(FSFloaterGroupTitles) || t == typeid(VSGroupSearch) || t == typeid(LLPanelDirGroups) || t == typeid(LLSearchComboBox) ||
                t == typeid(LLPreviewLSL) || t == typeid(LLScriptEdCore) || t == typeid(LLScriptEditor) ||
                t == typeid(FSLSLPreProcViewer) || t == typeid(LLFloaterGotoLine) || t == typeid(LLFloaterScriptEdPrefs) || t == typeid(LLPreviewSound) || t == typeid(LLFloaterProperties) || t == typeid(LLFloaterItemProperties) ||
                t == typeid(LLSidepanelItemInfo) || t == typeid(LLFloaterChangeItemThumbnail) || t == typeid(LLFloaterImagePreview) || t == typeid(LLFloaterSoundPreview) || t == typeid(LLFloaterNameDesc) || t == typeid(LLUploadDialog) || t == typeid(LLFloaterProfile) || LLFloaterDisplayNameUtil::isNativeFloaterType(t) || LLFloaterPayUtil::isNativeFloaterType(t) ||
                t == typeid(LLPanelProfile) ||
                t == typeid(LLPanelProfileSecondLife) ||
                t == typeid(LLPanelProfileFirstLife) ||
                t == typeid(LLPanelProfileNotes) ||
                t == typeid(LLPanelProfileWeb) ||
                t == typeid(LLPanelProfilePicks) ||
                t == typeid(LLPanelProfilePick) ||
                t == typeid(LLPanelProfileClassifieds) ||
                t == typeid(LLPanelProfileClassified) || t == typeid(LLPublishClassifiedFloater) ||
                t == typeid(LLProfileDropTarget) ||
                t == typeid(LLFloaterProfileTexture) ||
                t == typeid(LLProfileImageCtrl) ||
                t == typeid(LLTextureCtrl) ||
                t == typeid(LLThumbnailCtrl) ||
                t == typeid(LLNameListCtrl) ||
                t == typeid(LLLoadingIndicator) ||
                t == typeid(LLSpinCtrl) ||
                t == typeid(LLAccordionCtrl) ||
                t == typeid(LLAccordionCtrlTab) || LLAccordionCtrlTab::isNativeHeaderType(t) ||
                t == typeid(LLToggleableMenu) ||
                t == typeid(LLPanelGroup) ||
                t == typeid(LLPanelGroupCreate) || t == typeid(LLFloaterGroupInvite) ||
                t == typeid(LLPanelGroupInvite) || t == typeid(LLFloaterGroupBulkBan) ||
                t == typeid(LLPanelGroupBulkBan) || t == typeid(LLPanelGroupBulk) ||
                t == typeid(LLPanelGroupGeneral) ||
                t == typeid(LLPanelGroupRoles) ||
                t == typeid(LLPanelGroupMembersSubTab) ||
                t == typeid(LLPanelGroupRolesSubTab) ||
                t == typeid(LLPanelGroupActionsSubTab) ||
                t == typeid(LLPanelGroupBanListSubTab) ||
                t == typeid(LLPanelGroupNotices) ||
                t == typeid(LLPanelGroupLandMoney) ||
                t == typeid(LLPanelGroupExperiences) || t == typeid(LLExperienceItem) ||
                LLPanelGroupNotices::isDropTargetType(t) || LLPanelProfileSecondLife::isPermissionsFloaterType(t) ||
                t == typeid(LLNotificationListView) || t == typeid(LLNotificationListItem) ||
                t == typeid(LLGroupNotificationListItem) || t == typeid(LLGroupInviteNotificationListItem) ||
                t == typeid(LLGroupNoticeNotificationListItem) || t == typeid(LLTransactionNotificationListItem) ||
                t == typeid(LLSystemNotificationListItem) || t == typeid(LLToastGroupNotifyPanel) ||
                t == typeid(LLToastScriptQuestion) || t == typeid(LLToastPanel) || t == typeid(LLCheckBoxToastPanel) ||
                t == typeid(FSFloaterIM) || t == typeid(FSFloaterIMContainer) || t == typeid(FSPanelContactSets) || t == typeid(FSFloaterContacts) || t == typeid(FSFloaterPartialInventory) || t == typeid(FSChatHistory) || t == typeid(FSPanelIMControlPanel) || t == typeid(FSPanelGroupControlPanel) || t == typeid(FSPanelAdHocControlPanel) || t == typeid(FSScrollListCtrl) || t == typeid(LLChicletBar) || t == typeid(LLChicletPanel) || t == typeid(LLChicletNotificationCounterCtrl) || t == typeid(LLChicletAvatarIconCtrl) || t == typeid(LLChicletGroupIconCtrl) || t == typeid(LLChicletInvOfferIconCtrl) || t == typeid(LLChicletSpeakerCtrl) || t == typeid(LLScriptChiclet) || t == typeid(LLInvOfferChiclet) || t == typeid(LLIMP2PChiclet) || t == typeid(LLAdHocChiclet) || t == typeid(LLIMGroupChiclet) || t == typeid(LLNotificationChiclet) || t == typeid(LLIMWellChiclet) || t == typeid(LLAvatarList) || t == typeid(LLAvatarListItem) || t == typeid(LLAvatarIconCtrl) || t == typeid(LLGroupIconCtrl) || t == typeid(LLGroupList) || t == typeid(LLGroupListItem) || t == typeid(LLChatEntry) || t == typeid(LLFilterEditor) || t == typeid(LLMenuButton) || t == typeid(LLOutputMonitorCtrl) || t == typeid(LLSliderCtrl) || t == typeid(LLFlatListView) || t == typeid(LLFlatListViewEx) || t == typeid(LLMultiFloater) || t == typeid(LLDockableFloater) || t == typeid(LLContextMenu) || t == typeid(LLIMWellWindow) || t == typeid(LLNotificationWellWindow) || t == typeid(LLSysWellItem) || t == typeid(LLFloaterNotificationsTabbed) || t == typeid(LLToastIMPanel) || t == typeid(LLToastNotifyPanel) || t == typeid(LLIMToastNotifyPanel) || t == typeid(LLFloaterAvatarPicker) || t == typeid(LLFloaterGroupPicker) || t == typeid(LLPanelEmojiComplete) || LLTabContainer::isNativeTabButtonType(t) || LLFloaterAboutUtil::isFloaterType(t) || t == typeid(LLTabContainer) || t == typeid(VSPlainChat) || t == typeid(VSChatInput) || LLMenuBarGL::isNativeLoginItemType(t) || t == typeid(LLViewerMenuHolderGL) || t == typeid(LLMenuHolderGL) || t == typeid(LLMenuGL) ||
                   t == typeid(LLMenuBarGL) || t == typeid(LLMenuItemGL) || t == typeid(LLMenuItemCallGL) ||
                   t == typeid(LLMenuItemCheckGL) || t == typeid(LLMenuItemSeparatorGL) ||
                   t == typeid(LLMenuItemBranchGL) || t == typeid(LLContextMenuBranch) || t == typeid(LLMenuItemTearOffGL) ||
                   t == typeid(LLView) || t == typeid(LLPopupView) || t == typeid(LLRootView) || t == typeid(LLUICtrl) || t == typeid(LLPanel) ||
                   LLUICtrlFactory::isNativeLayoutType(t) || t == typeid(LLFlyoutButton) || t == typeid(LLButton) || t == typeid(LLDragAndDropButton) || t == typeid(LLBadge) || t == typeid(LLCheckBoxCtrl) || t == typeid(LLComboBox) || t == typeid(LLIconCtrl) ||
                   t == typeid(LLLineEditor) || t == typeid(LLTextEditor) || t == typeid(LLViewerTextEditor) || t == typeid(LLTextBox) || t == typeid(LLConsole) || t == typeid(LLLayoutStack) ||
                   t == typeid(LLLayoutPanel) || t == typeid(LLProgressBar) || t == typeid(LLScrollbar) || t == typeid(LLScrollContainer) ||
                   t == typeid(LLScrollListCtrl) || t == typeid(LLScrollColumnHeader) || t == typeid(LLViewBorder) || t == typeid(LLFloaterView) || t == typeid(LLFloater) ||
                   t == typeid(LLModalDialog) || t == typeid(LLFloaterTOS) ||
                   t == typeid(LLNotificationsUI::LLToast) || t == typeid(LLToastAlertPanel) ||
                   t == typeid(LLNotificationsUI::LLScreenChannel) || t == typeid(LLNotificationsUI::LLScreenChannelBase) ||
                   t == typeid(LLWindowShade) || t == typeid(LLResizeBar) || t == typeid(LLResizeHandle) ||
                   t == typeid(LLDragHandleTop) || t == typeid(LLDragHandleLeft) || t == typeid(LLMediaCtrl) || t == typeid(LLRadioGroup) || t == LLRadioGroup::itemType();
            if (!admitted) LL_WARNS("NativeUI") << "Rejected native widget type: " << t.name() << LL_ENDL;
            return admitted;
        },
        [](std::string_view name) { return name == "inspect_remote_object" || name == "web_content" || name == "search_replace" || name == "fs_nearby_chat" || name == "people" || name == "search" || name == "toybox" || name == "inventory" || name == "secondary_inventory" || name == "sl_about" || name == "message_critical" || name == "message_tos" ||
            name == "fs_impanel" || name == "fs_im_container" || name == "imcontacts" || name == "avatar_picker" ||
            name == "fs_blocklist" || name == "fs_add_contact" || name == "fs_contact_set_config" ||
            name == "mute_object_by_name" || name == "group_picker" || name == "fs_group_titles" || name == "vs_group_search" || name == "fs_group" || name == "profile" || name == "publish_classified" || name == "preferences" || name == "prefs_proxy" || name == "prefs_translation" || name == "prefs_autoreplace" ||
            name == "prefs_spellchecker" || name == "prefs_spellchecker_import" || name == "perms_default" || name == "keybind_dialog" || name == "im_well_window" || name == "notification_well_window" || name == "conversation" ||
            name == "preview_texture" || name == "preview_notecard" || name == "preview_conversation" || name == "script_floater" ||
            // Exact reviewed CPU inventory/account owners; scene/task variants stay excluded.
            name == "fs_partial_inventory" || name == "properties" || name == "item_properties" ||
            name == "change_item_thumbnail" || name == "preview_sound" || name == "preview_script" ||
            name == "script_colors" || name == "upload_image" || name == "upload_sound" ||
            name == "pay_resident" || name == "display_name"; },
        [](std::string_view name) { return name == "panel_people" || name == "sidepanel_inventory" || name == "panel_main_inventory" ||
            name == "panel_marketplace_inbox" || name == "inventory_gallery" || name == "progress_view" || name == "progress_view_mini" || name == "popup_holder" ||
            name == "status" || name == "volumepulldown_floater" || name == "navigation_bar" || name == "toolbar_view" || name == "toolbar view" || name == "native_nearby_chat" || name == "script panel" || name == "sidepanel_item_info" || name == "fs_panel_block_list_sidetray" || name == "panel_im_control_panel" || name == "panel_dir_groups" || name == "contact_sets_panel" ||
            name == "panel_preference" || name == "panel_preference_graphics" || name == "panel_preference_privacy" ||
            name == "panel_preference_controls" || name == "panel_preference_crashreports" || name == "panel_preference_skins" ||
            name == "panel_preference_backup" || name == "panel_preference_opensim" || name == "panel_preference_sounds" ||
            name == "panel_preference_firestorm" || name == "fs_panel_preference_ui_sounds" || name == "panel_voice_device_settings" ||
            name == "panel_profile" || name == "panel_profile_secondlife" || name == "panel_profile_web" ||
            name == "panel_profile_picks" || name == "panel_profile_pick" || name == "panel_profile_classifieds" ||
            name == "panel_profile_classified" || name == "panel_profile_firstlife" || name == "panel_profile_notes" ||
            name == "panel_group_info_sidetray" || name == "panel_group_creation_sidetray" || name == "panel_group_general" ||
            name == "panel_group_roles" || name == "panel_group_members_subtab" || name == "panel_group_roles_subtab" ||
            name == "panel_group_actions_subtab" || name == "panel_group_banlist_subtab" || name == "panel_group_notices" ||
            name == "panel_group_land_money" || name == "panel_group_experiences"; },
        [](std::string_view name)
        {
            LLSD substitution; substitution["CONTROL"] = std::string(name);
            LLSD args; args["MESSAGE"] = LLTrans::getString("NativeVulkanUIUnavailable", substitution);
            LLNotificationsUtil::add("GenericAlert", args);
        },
        [](std::string_view name)
        {
            return name == "search" || name == "picks" || name == "chat" || name == "inventory" || name == "people" ||
                   name == "preferences" || name == "profile" || name == "contact_sets" ||
                   name == "conversation_log" || name == "block_list" || name == "group_titles";
        },
        [](std::string_view name, const LLSD& parameter)
        {
            if (LLPanelMainInventory::isNativeCallback(name, parameter) ||
                LLPanelPeople::isNativeCallback(std::string(name), parameter) || FSChatHistory::isNativeCallback(std::string(name), parameter)) return true;
            if (name == "Profile.Commit" || name == "Profile.EnableItem")
            {
                // Existing profile callbacks own ordinary account/text/image
                // actions. Scene map/moderation/voice commands stay excluded.
                const std::string action = parameter.asString();
                return action == "im" || action == "chat_history" || action == "add_friend" ||
                    action == "remove_friend" || action == "invite_to_group" || action == "share" ||
                    action == "pay" || action == "toggle_block_agent" || action == "copy_user_id" ||
                    action == "agent_permissions" || action == "copy_display_name" || action == "copy_username" ||
                    action == "edit_display_name" || action == "edit_partner" || action == "upload_photo" ||
                    action == "change_photo" || action == "remove_photo" || action == "add_to_contact_set" ||
                    action == "copy_uri" || action == "preview";
            }
            if (name == "Profile.CheckItem") return parameter.asString() == "toggle_block_agent";
            if (name == "TopInfoBar.Action") return parameter.asString() == "copy";
            if (name == "Toolbars.EnableSetting" || name == "Toolbars.CheckSetting")
                return parameter.asString() == "icons_with_text" || parameter.asString() == "icons_only" || parameter.asString() == "text_only";
            if (name == "Toolbars.SetAlignment" || name == "Toolbars.CheckAlignment")
            {
                const auto alignment = parameter.asString();
                return alignment == "center" || alignment == "left" || alignment == "top" || alignment == "right" || alignment == "bottom";
            }
            if (name == "Toolbars.SetLayoutStyle" || name == "Toolbars.CheckLayoutStyle")
            {
                const auto layout = parameter.asString();
                return layout == "none" || layout == "equalize" || layout == "fill";
            }
            if (name == "Floater.Show" || name == "Floater.Toggle" || name == "Floater.Visible" ||
                name == "Floater.IsOpen" || name == "Floater.ToggleOrBringToFront")
                return parameter.asString() == "fs_nearby_chat" || VSUIAdmission::floater(parameter.asString());
            if (name == "SideTray.PanelPeopleTab" || name == "SideTray.CheckPanelPeopleTab")
            {
                const std::string tab = parameter.asString();
                return tab == "friends_panel" || tab == "groups_panel" || tab == "contact_sets_panel" || tab == "blocked_panel";
            }
            if (name == "ToggleControl" || name == "CheckControl")
            {
                const std::string setting = parameter.asString();
                return setting == "ShowChatMiniIcons" || setting == "FSTypingChevronPrefix" ||
                       setting == "FSNearbyChatbar" || setting == "FSShowChatChannel" || setting == "FSShowEmojiButton" ||
                       setting == "FSShowChatType" || setting == "FSShowIMSendButton" || setting == "LockToolbars" || setting == "MainChatbarVisible" || setting == "ChatHistoryTearOff" ||
                       setting == "FSUseBuiltInHistory" || setting == "PlainTextChatHistory" ||
                       setting == "ShowNavbarNavigationPanel" || setting == "ShowNavbarFavoritesPanel" ||
                       setting == "NavBarShowCoordinates" || setting == "NavBarShowParcelProperties" ||
                       setting == "MenuSearch" || setting == "ShowNetStats" || setting == "MuteAudio";
            }
            static const std::string_view callbacks[] = {
                "Toybox.RestoreDefaults", "Toybox.ClearAll", "Toolbars.RemoveSelectedCommand",
                "File.Quit", "File.CloseWindow", "File.EnableCloseWindow", "File.CloseAllWindows",
                "File.EnableCloseAllWindows", "File.CloseWindowGroup", "File.EnableCloseWindowGroup",
                "File.UploadImage", "File.UploadSound", "File.UploadBulk", "File.EnableUpload",
                "Edit.Copy", "Edit.Cut", "Edit.Delete", "Edit.Deselect", "Edit.Paste", "Edit.Redo",
                "Edit.SelectAll", "Edit.Undo", "Edit.EnableCopy", "Edit.EnableCut", "Edit.EnableDelete",
                "Edit.EnableDeselect", "Edit.EnablePaste", "Edit.EnableRedo", "Edit.EnableSelectAll", "Edit.EnableUndo",
                "SpellCheck.AddToDictionary", "SpellCheck.AddToIgnore", "SpellCheck.EnableAddToDictionary",
                "SpellCheck.EnableAddToIgnore", "SpellCheck.ReplaceWithSuggestion", "SpellCheck.VisibleSuggestion",
                "Favorites.DoToSelected", "Favorites.EnableSelected", "PromptShowURL", "ShowHelp",
                "Avatar.ToggleSearch", "Avatar.SearchVisible", "Avatar.TogglePicks",
                "Avatar.ToggleMyProfile", "Avatar.IsMyProfileOpen", "IMChicletMenu.Action",
                "IMSession.Menu.Action", "IMSession.Menu.Enable", "ChatOptions.Action", "ChatOptions.Check",
                "ChatOptions.Visible", "ChatOptions.Enable", "IMWellChicletMenu.Action", "IMWellChicletMenu.EnableItem",
                "NotificationWellChicletMenu.Action", "NotificationWellChicletMenu.EnableItem",
                "InvOfferChiclet.Action", "Mention.CopyURI", "Mention.Chat",
                "Url.Open", "Url.OpenInternal", "Url.OpenExternal", "Url.Execute", "Url.Teleport", "Url.Block", "Url.Unblock",
                "Url.CopyLabel", "Url.CopyUrl", "Url.ShowProfile", "Url.SendIM", "Url.AddFriend", "Url.RemoveFriend",
                "Url.EnableShowProfile", "Url.EnableSendIM", "Url.EnableAddFriend", "Url.EnableRemoveFriend",
                "FS.ViewLog", "FS.EnableViewLog", "FS.AddToContactSet", "FS.BlockAvatar", "FS.CheckIsAgentBlocked", "FS.EnableBlockAvatar",
                "FS.JoinGroup", "FS.LeaveGroup", "FS.ActivateGroup", "FS.EnableJoinGroup", "FS.EnableLeaveGroup", "FS.EnableActivateGroup",
                "FS.WaitingForGroupData", "FS.HaveGroupData"
            };
            return std::find(std::begin(callbacks), std::end(callbacks), name) != std::end(callbacks);
        },
        [](std::string_view filename, std::string_view child)
        {
            constexpr std::string_view people_file = "panel_people.xml";
            return !(child == "nearby_panel" && filename.size() >= people_file.size() &&
                     filename.substr(filename.size() - people_file.size()) == people_file);
        });
}
