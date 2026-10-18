// Shared UI construction with reviewed native service ownership. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "ao.h"
#include "llfloaterbuycurrency.h"
#include "llfloaterbuyland.h"
#include "llfloatereditextdaycycle.h"
#include "llfloaterfixedenvironment.h"
#include "llfloatermyenvironment.h"
#include "llpaneleditsky.h"
#include "llpaneleditwater.h"
#include "llsettingspicker.h"
#include "lltrackpicker.h"
#include "llvirtualtrackball.h"
#include "llxyvector.h"
#include "llfloaterland.h"
#include "llfloaterregioninfo.h"
#include "llfloaterregiondebugconsole.h"
#include "llfloaterregionrestartschedule.h"
#include "llfloatertopobjects.h"
#include "llfloaterbanduration.h"
#include "llfloaterurlentry.h"
#include "llpanelenvironment.h"
#include "llpanelexperiencelisteditor.h"
#include "llmultislider.h"
#include "llmultisliderctrl.h"
#include "llfloatersellland.h"
#include "llfloaterbuycurrencyhtml.h"
#include "llfloateraddpaymentmethod.h"
#include "fsmoneytracker.h"
#include "llfloaterinventorysettings.h"
#include "llfloatermarketplacelistings.h"
#include "llfloaterlinkreplace.h"
#include "llfloaterevent.h"
#include "llfloateremojipicker.h"
#include "llfloatercamera.h"
#include "llfloatercamerapresets.h"
#include "llfloatersavecamerapreset.h"
#include "llpanelpresetscamerapulldown.h"
#include "lljoystickbutton.h"
#include "llfloaterexperiences.h"
#include "llfloaterexperienceprofile.h"
#include "llfloaterexperiencepicker.h"
#include "llpanelexperiences.h"
#include "llpanelexperiencepicker.h"
#include "llpanelexperiencelog.h"
#include "llfloatermarketplace.h"
#include "llfloatergridstatus.h"
#include "llfloaterdestinations.h"
#include "fsfloaterteleporthistory.h"
#include "llpanelteleporthistory.h"
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
#include "llscriptfloater.h"
#include "lltoastscripttextbox.h"
#include "lltoastpanel.h"
#include "lltoastimpanel.h"
#include "lltoastnotifypanel.h"
#include "llfloateravatarpicker.h"
#include "llfloatergroups.h"
#include "llpanelemojicomplete.h"
#include "llfloaterchatmentionpicker.h"
#include "llfloaterwindowsize.h"
#include "fsfloaterprotectedfolders.h"
#include "fsfloaterstreamtitle.h"
#include "llfloaterpreviewtrash.h"
#include "llfloaternewfeaturenotification.h"
#include "fsfloaterwhitelisthelper.h"
#include "llfloatersettingscolor.h"
#include "fsfloatersplashscreensettings.h"
#include "fsfloatervolumecontrols.h"
#include "llfloaterfonttest.h"
#include "llfloaterhud.h"
#include "rlvfloaters.h"
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
#include "llpanelnearbymedia.h"
#include "llwindowshade.h"
#include "llpanelprimmediacontrols.h"
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
#include "llstatbar.h"
#include "llstatview.h"
#include "llcontainerview.h"
#include "llfloaterlagmeter.h"
#include "llfloatersettingsdebug.h"
#include "llfloaterhelpbrowser.h"
#include "llfloaterhowto.h"
#include "fsfloaterstatistics.h"
#include "llpanelvolumepulldown.h"
#include "llpanelpulldown.h"
#include "lltoolbar.h"
#include "lllocationinputctrl.h"
#include "llurllineeditorctrl.h"
#include "fsnearbychatcontrol.h"
#include "fsnearbychatvoicemonitor.h"
#include "llfavoritesbar.h"
#include "llfloater.h"
#include "llfloatercreatelandmark.h"
#include "llfloaterworldmap.h"
#include "llworldmapview.h"
#include "fsfloaterplacedetails.h"
#include "llpanelplaces.h"
#include "llpanelplaceinfo.h"
#include "llpanelplaceprofile.h"
#include "llpanellandmarkinfo.h"
#include "llpanellandmarks.h"
#include "llpanelplacestab.h"
#include "llplacesinventorypanel.h"
#include "llplacesfolderview.h"
#include "quickprefs.h"
#include "llfloaterenvironmentadjust.h"
#include "llfloaterscriptrecover.h"
#include "llfloatermyscripts.h"
#include "llfloaterscriptlimits.h"
#include "llfloaterscriptdebug.h"
#include "llfloaterabout.h"
#include "llhudview.h"
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
            const bool admitted =
                // These existing controllers use shared UI, local settings,
                // account inventory or the already-owned browser/audio service.
                t==typeid(LLPanelNearByMedia) ||
                t==typeid(LLPanelPrimMediaControls) || t==typeid(LLWindowShade) ||
                LLFloaterRegionInfo::isNativeInternalType(t) || t==typeid(LLFloaterRegionDebugConsole) ||
                t==typeid(LLFloaterRegionRestartSchedule) || t==typeid(LLFloaterTopObjects) || t==typeid(LLIconsComboBox) ||
                LLFloaterBuyCurrency::isNativeInternalType(t) ||
                LLFloaterBuyLand::isNativeInternalType(t) ||
                t==typeid(LLFloaterEnvironmentAdjust) || t==typeid(LLFloaterMyEnvironment) || t==typeid(LLFloaterFixedEnvironmentSky) || t==typeid(LLFloaterFixedEnvironmentWater) || t==typeid(LLFloaterEditExtDayCycle) || t==typeid(LLFloaterSettingsPicker) ||
                t==typeid(LLFloaterTrackPicker) || t==typeid(LLPanelSettingsWaterMainTab) ||
                t==typeid(LLPanelSettingsSkyAtmosTab) || t==typeid(LLPanelSettingsSkyCloudTab) ||
                t==typeid(LLPanelSettingsSkySunMoonTab) || t==typeid(LLPanelSettingsSkyDensityTab) ||
                t==typeid(LLVirtualTrackball) || t==typeid(LLXYVector) || LLFloaterLand::isNativeInternalType(t) ||
                t==typeid(LLFloaterBanDuration) || t==typeid(LLFloaterURLEntry) ||
                t==typeid(LLPanelExperienceListEditor) || t==typeid(LLSettingsDropTarget) ||
                t==typeid(LLMultiSlider) || t==typeid(LLMultiSliderCtrl) || LLFloaterSellLand::isNativeInternalType(t) || t == typeid(LLFloaterBuyCurrencyHTML) ||
                t == typeid(LLFloaterAddPaymentMethod) || t == typeid(FSMoneyTracker) || t == typeid(LLFloaterWindowSize) || t == typeid(FSFloaterProtectedFolders) ||
                t == typeid(FSFloaterStreamTitle) || t == typeid(FSFloaterStreamTitleHistory) ||
                t == typeid(LLFloaterChatMentionPicker) || t == typeid(LLFloaterEmojiComplete) ||
                t == typeid(LLFloaterPreviewTrash) || t == typeid(LLFloaterNewFeatureNotification) ||
                t == typeid(FSFloaterWhiteListHelper) || t == typeid(LLFloaterSettingsColor) ||
                t == typeid(FSFloaterSplashScreenSettings) || t == typeid(FSFloaterVolumeControls) ||
                t == typeid(LLFloaterFontTest) || t == typeid(LLFloaterHUD) || t == typeid(RlvFloaterStrings) ||
                t == typeid(FloaterQuickPrefs) || t == typeid(LLFloaterCreateLandmark) || t == typeid(LLFloaterWorldMap) || t == typeid(LLWorldMapView) || t == typeid(FSFloaterPlaceDetails) || t == typeid(LLPanelPlaces) || t == typeid(LLPanelPlaceInfo) || t == typeid(LLPanelPlaceProfile) || t == typeid(LLPanelLandmarkInfo) || t == typeid(LLLandmarksPanel) || t == typeid(LLFavoritesPanel) || t == typeid(LLTeleportHistoryPanel) || t == typeid(LLPlacesInventoryPanel) || t == typeid(LLPlacesFolderView) || t == typeid(FloaterAO) || t == typeid(LLFloaterSidePanelContainer) || t == typeid(LLSidepanelInventory) ||
                t == typeid(LLPanelMainInventory) || LLPanelMainInventory::isNativeInternalType(t) ||
                t == typeid(LLPanelMarketplaceInbox) || t == typeid(LLInboxInventoryPanel) ||
                t == typeid(LLInboxFolderViewFolder) || t == typeid(LLInboxFolderViewItem) || t == typeid(LLInventoryGallery) || t == typeid(LLInventoryGalleryItem) ||
                LLInventoryPanel::isNativeInternalType(t) || LLInspectRemoteObjectUtil::isNativeInternalType(t) || LLHints::isNativeInternalType(t) || t == typeid(LLStatusBar) || t == typeid(LLStatGraph) || t == typeid(LLFloaterSettingsDebug) || t == typeid(LLFloaterHelpBrowser) || t == typeid(LLFloaterHowTo) || t == typeid(LLFloaterInventorySettings) || t == typeid(LLFloaterMarketplaceListings) ||
                t == typeid(LLPanelMarketplaceListings) || t == typeid(LLFloaterAssociateListing) ||
                t == typeid(LLFloaterMarketplaceValidation) || t == typeid(LLFloaterLinkReplace) ||
                t == typeid(LLInventoryLinkReplaceDropTarget) || t == typeid(LLFloaterEmojiPicker) || LLFloaterEmojiPicker::isNativeInternalType(t) || t == typeid(LLFloaterEvent) || t == typeid(LLFloaterExperiences) ||
                t == typeid(LLFloaterExperienceProfile) || t == typeid(LLFloaterExperiencePicker) ||
                t == typeid(LLPanelExperiences) || t == typeid(LLPanelExperiencePicker) || t == typeid(LLPanelExperienceLog) || t == typeid(LLFloaterMarketplace) || t == typeid(LLFloaterGridStatus) || t == typeid(LLFloaterDestinations) || t == typeid(FSFloaterTeleportHistory) || t == typeid(LLTeleportHistoryPanel) || t == typeid(LLStatBar) || t == typeid(LLStatView) || t == typeid(LLContainerView) || t == typeid(LLFloaterLagMeter) || t == typeid(FSFloaterStatistics) ||
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
                t == typeid(LLFloaterScriptDebug) || t == typeid(LLFloaterScriptDebugOutput) ||
                t == typeid(LLFloaterScriptLimits) || t == typeid(LLPanelScriptLimitsInfo) || t == typeid(LLPanelScriptLimitsRegionMemory) || t == typeid(LLFloaterMyScripts) || t == typeid(LLFloaterScriptRecover) || t == typeid(LLPreviewLSL) || t == typeid(LLScriptEdCore) || t == typeid(LLScriptEditor) ||
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
                t == typeid(LLScriptFloater) || t == typeid(LLToastScriptTextbox) ||
                t == typeid(LLToastScriptQuestion) || t == typeid(LLToastPanel) || t == typeid(LLCheckBoxToastPanel) ||
                t == typeid(FSFloaterIM) || t == typeid(FSFloaterIMContainer) || t == typeid(FSPanelContactSets) || t == typeid(FSFloaterContacts) || t == typeid(FSFloaterPartialInventory) || t == typeid(FSChatHistory) || t == typeid(FSPanelIMControlPanel) || t == typeid(FSPanelGroupControlPanel) || t == typeid(FSPanelAdHocControlPanel) || t == typeid(FSScrollListCtrl) || t == typeid(LLChicletBar) || t == typeid(LLChicletPanel) || t == typeid(LLChicletNotificationCounterCtrl) || t == typeid(LLChicletAvatarIconCtrl) || t == typeid(LLChicletGroupIconCtrl) || t == typeid(LLChicletInvOfferIconCtrl) || t == typeid(LLChicletSpeakerCtrl) || t == typeid(LLScriptChiclet) || t == typeid(LLInvOfferChiclet) || t == typeid(LLIMP2PChiclet) || t == typeid(LLAdHocChiclet) || t == typeid(LLIMGroupChiclet) || t == typeid(LLNotificationChiclet) || t == typeid(LLIMWellChiclet) || t == typeid(LLAvatarList) || t == typeid(LLAvatarListItem) || t == typeid(LLAvatarIconCtrl) || t == typeid(LLGroupIconCtrl) || t == typeid(LLGroupList) || t == typeid(LLGroupListItem) || t == typeid(LLChatEntry) || t == typeid(LLFilterEditor) || t == typeid(LLMenuButton) || t == typeid(LLOutputMonitorCtrl) || t == typeid(LLSliderCtrl) || t == typeid(LLFlatListView) || t == typeid(LLFlatListViewEx) || t == typeid(LLMultiFloater) || t == typeid(LLDockableFloater) || t == typeid(LLContextMenu) || t == typeid(LLIMWellWindow) || t == typeid(LLNotificationWellWindow) || t == typeid(LLSysWellItem) || t == typeid(LLFloaterNotificationsTabbed) || t == typeid(LLToastIMPanel) || t == typeid(LLToastNotifyPanel) || t == typeid(LLIMToastNotifyPanel) || t == typeid(LLFloaterAvatarPicker) || t == typeid(LLFloaterGroupPicker) || t == typeid(LLPanelEmojiComplete) || LLTabContainer::isNativeTabButtonType(t) || LLFloaterAboutUtil::isFloaterType(t) || t == typeid(LLTabContainer) || t == typeid(VSPlainChat) || t == typeid(VSChatInput) || LLMenuBarGL::isNativeLoginItemType(t) || t == typeid(LLViewerMenuHolderGL) || t == typeid(LLMenuHolderGL) || t == typeid(LLMenuGL) ||
                   t == typeid(LLMenuBarGL) || t == typeid(LLMenuItemGL) || t == typeid(LLMenuItemCallGL) ||
                   t == typeid(LLMenuItemCheckGL) || t == typeid(LLMenuItemSeparatorGL) ||
                   t == typeid(LLMenuItemBranchGL) || t == typeid(LLContextMenuBranch) || t == typeid(LLMenuItemTearOffGL) ||
                   t == typeid(LLHUDView) || t == typeid(LLView) || t == typeid(LLPopupView) || t == typeid(LLRootView) || t == typeid(LLUICtrl) || t == typeid(LLPanel) ||
                   LLUICtrlFactory::isNativeLayoutType(t) || t == typeid(LLFlyoutButton) || t == typeid(LLButton) || t == typeid(LLDragAndDropButton) || t == typeid(LLBadge) || t == typeid(LLCheckBoxCtrl) || t == typeid(LLComboBox) || t == typeid(LLIconCtrl) ||
                   t == typeid(LLLineEditor) || t == typeid(LLTextEditor) || t == typeid(LLViewerTextEditor) || t == typeid(LLTextBox) || t == typeid(LLConsole) || t == typeid(LLLayoutStack) ||
                   t == typeid(LLLayoutPanel) || t == typeid(LLProgressBar) || t == typeid(LLScrollbar) || t == typeid(LLScrollContainer) ||
                   t == typeid(LLScrollListCtrl) || t == typeid(LLScrollColumnHeader) || t == typeid(LLViewBorder) || t == typeid(LLFloaterView) || t == typeid(LLFloater) ||
                   t == typeid(LLModalDialog) || t == typeid(LLFloaterTOS) ||
                   t == typeid(LLNotificationsUI::LLToast) || t == typeid(LLToastAlertPanel) ||
                   t == typeid(LLNotificationsUI::LLScreenChannel) || t == typeid(LLNotificationsUI::LLScreenChannelBase) ||
                   t == typeid(LLWindowShade) || t == typeid(LLResizeBar) || t == typeid(LLResizeHandle) ||
                   t == typeid(LLDragHandleTop) || t == typeid(LLDragHandleLeft) || t == typeid(LLMediaCtrl) || t == typeid(LLRadioGroup) || t == LLRadioGroup::itemType();
            if (!admitted && (LLFloaterCamera::isNativeWidgetType(t) || t == typeid(LLFloaterCameraPresets) || t == typeid(LLCameraPresetFlatItem) || t == typeid(LLFloaterSaveCameraPreset) || t == typeid(LLPanelPresetsCameraPulldown) || t == typeid(LLJoystickCameraRotate) || t == typeid(LLJoystickCameraTrack))) return true;
            if (!admitted) LL_WARNS("NativeUI") << "Rejected native widget type: " << t.name() << LL_ENDL;
            return admitted;
        },
        [](std::string_view name) { return
            name == "region_info" || name == "region_debug_console" || name == "top_objects" || name == "about_land" || name == "ban_duration" || name == "env_adjust_snapshot" || name == "my_environments" || name == "env_fixed_environmentent_sky" || name == "env_fixed_environmentent_water" || name == "env_edit_extdaycycle" || name == "buy_land" || name == "sell_land" || name == "buy_currency" || name == "buy_currency_html" || name == "add_payment_method" || name == "money_tracker" ||
            name == "window_size" || name == "fs_protectedfolders" || name == "fs_streamtitle" || name == "fs_streamtitlehistory" ||
            name == "chat_mention_picker" || name == "emoji_complete" || name == "preview_trash" || name == "new_feature_notification" ||
            name == "fs_whitelist_floater" || name == "settings_color" || name == "fs_splash_screen_settings" || name == "fs_volume_controls" ||
            name == "font_test" || name == "hud" || name == "rlv_strings" ||
            name == "quickprefs" || name == "add_landmark" || name == "world_map" || name == "places" || name == "fs_placedetails" || name == "settings_debug" || name == "help_browser" || name == "guidebook" || name == "inventory_settings" || name == "marketplace_listings" || name == "associate_listing" ||
            name == "marketplace_validation" || name == "linkreplace" || name == "emoji_picker" || name == "event" || name == "experiences" || name == "experience_profile" || name == "experience_search" || name == "marketplace" || name == "grid_status" || name == "destinations" || name == "fs_teleporthistory" || name == "stats" || name == "lagmeter" || name == "inspect_remote_object" || name == "web_content" || name == "search_replace" || name == "fs_nearby_chat" || name == "people" || name == "search" || name == "toybox" || name == "inventory" || name == "secondary_inventory" || name == "sl_about" || name == "message_critical" || name == "message_tos" ||
            name == "fs_impanel" || name == "fs_im_container" || name == "imcontacts" || name == "avatar_picker" ||
            name == "fs_blocklist" || name == "fs_add_contact" || name == "fs_contact_set_config" ||
            name == "mute_object_by_name" || name == "group_picker" || name == "fs_group_titles" || name == "vs_group_search" || name == "fs_group" || name == "profile" || name == "publish_classified" || name == "preferences" || name == "prefs_proxy" || name == "prefs_translation" || name == "prefs_autoreplace" ||
            name == "prefs_spellchecker" || name == "prefs_spellchecker_import" || name == "perms_default" || name == "keybind_dialog" || name == "im_well_window" || name == "notification_well_window" || name == "conversation" ||
            name == "preview_texture" || name == "preview_notecard" || name == "preview_conversation" || name == "script_floater" ||
            // Exact reviewed CPU inventory/account owners; scene/task variants stay excluded.
            name == "fs_partial_inventory" || name == "properties" || name == "item_properties" ||
            name == "change_item_thumbnail" || name == "preview_sound" || name == "preview_script" || name == "script_debug" || name == "script_debug_output" || name == "script_limits" || name == "my_scripts" || name == "script_recover" ||
            name == "script_colors" || name == "upload_image" || name == "upload_sound" ||
            name == "pay_resident" || name == "display_name" || name == "animation_overrider" || name == "camera_presets" || name == "save_camera_preset" || name == "camera" || name == "fs_camera_small" || name == "phototools_camera"; },
        [](std::string_view name) { return
            name == "land_general_panel" || name == "land_covenant_panel" || name == "land_objects_panel" ||
            name == "land_options_panel" || name == "land_audio_panel" || name == "land_media_panel" ||
            name == "land_access_panel" || name == "land_experiences_panel" || name == "land_environment_panel" || name == "panel_settings_water" || name == "panel_settings_atmos" || name == "panel_settings_cloud" ||
                name == "panel_settings_sunmoon" || name == "panel_settings_density" || name == "panel_experience_list_editor" || name == "camera_zoom_panel" || name == "experiences_panel" || name == "llpanelexperiencepicker" || name == "experience_log" || name == "llpanelmarketplacelistings" || name == "marketplace_listings_panel" || name == "objects_mapview" || name == "panel_places" || name == "panel_place_profile" || name == "panel_landmark_info" || name == "panel_people" || name == "sidepanel_inventory" || name == "panel_main_inventory" ||
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
            return name == "quickprefs" || name == "map" || name == "places" || name == "aboutland" || name == "fly" || name == "groundsit" || name == "resync_animations" || name == "derender_animated_objects" || name == "stop_animations" || name == "marketplacelistings" || name == "view" || name == "phototools_view" || name == "mouselook_view" || name == "animationoverride" || name == "settings_debug" || name == "webbrowser" || name == "defaultchatbar" || name == "howto" || name == "marketplace" || name == "gridstatus" || name == "destinations" || name == "teleport_history" || name == "statistics" || name == "search" || name == "picks" || name == "chat" || name == "inventory" || name == "people" ||
                   name == "preferences" || name == "profile" || name == "contact_sets" ||
                   name == "conversation_log" || name == "block_list" || name == "group_titles";
        },
        [](std::string_view name, const LLSD& parameter)
        {
            if (name == "RecreateLSLBridge") return true;
            if (name == "Places.OverflowMenu.Action")
                return parameter.asString() == "landmark" || parameter.asString() == "copy" || parameter.asString() == "delete" || parameter.asString() == "pick" || parameter.asString() == "add_to_favbar";
            if (name == "Places.OverflowMenu.Enable") return parameter.asString() == "can_create_pick";
            if (name == "Places.LandmarksGear.Add.Action" || name == "Places.LandmarksGear.CopyPaste.Action" ||
                name == "Places.LandmarksGear.Custom.Action" || name == "Places.LandmarksGear.Folding.Action" ||
                name == "Places.LandmarksGear.Check" || name == "Places.LandmarksGear.Enable")
            {
                const std::string action = parameter.asString();
                return action == "add_landmark" || action == "category" || action == "cut" || action == "copy" || action == "paste" || action == "delete" || action == "more_info" ||
                    action == "teleport" || action == "show_on_map" || action == "create_pick" || action == "share" || action == "restore" || action == "move_to_landmarks" || action == "move_to_favorites" ||
                    action == "copy_slurl" || action == "rename" || action == "add_landmark_root" || action == "category_root" || action == "expand_all" || action == "collapse_all" || action == "expand" || action == "collapse" || action == "sort_by_date";
            }
            if (LLPanelMainInventory::isNativeCallback(name, parameter) ||
                LLPanelPeople::isNativeCallback(std::string(name), parameter) || FSChatHistory::isNativeCallback(std::string(name), parameter)) return true;
            if (name == "DayCycle.Track" || name == "DayCycle.PlayActions") return true;
            if(name=="World.EnvSettings")return parameter.asString()=="my_environs";
            if(name=="MyEnvironments.DoCreate" || name=="MyEnvironments.DoEdit" || name=="MyEnvironments.DoApply" ||
                name=="MyEnvironments.CopyPaste" || name=="MyEnvironments.EnableAction" ||
                name=="MyEnvironments.CanApply" || name=="MyEnvironments.EnvironmentEnabled")return true;
            if (name == "Land.BuyPass" || name == "Land.EnableBuyPass") return true;
            if (name == "BuyCurrency" || name == "Land.Buy" || name == "World.EnableBuyLand") return true;
            if (name == "MoneyTrackerList.Action") return parameter.asString() == "copy" || parameter.asString() == "delete";
            if (name == "MoneyTrackerList.Enable") return parameter.asString() == "can_copy" || parameter.asString() == "can_delete";
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
                    action == "can_show_on_map" || action == "copy_uri" || action == "preview";
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
                       setting == "MenuSearch" || setting == "ShowNetStats" || setting == "MuteAudio" ||
                       setting == "FSIgnoreAdHocSessions" || setting == "FSReportIgnoredAdHocSession" ||
                       setting == "FSDontIgnoreAdHocFromFriends";
            }
            if (name == "TeleportHistory.GearMenu.Action" || name == "TeleportHistory.GearMenu.Enable")
            {
                const std::string action = parameter.asString();
                return action == "map" || action == "profile" || action == "teleport" || action == "copy_slurl" || action == "remove" ||
                       action == "clear_history" || action == "expand_all" || action == "collapse_all";
            }
            static const std::string_view callbacks[] = {
                // Shared account/notification policy; none requires a scene renderer.
                "RegionInfo.ManageRestart",
                "TopObjects.CommitObjectsList", "TopObjects.GetByObjectName", "TopObjects.GetByOwnerName",
                "TopObjects.GetByParcelName", "TopObjects.Kick", "TopObjects.Profile", "TopObjects.Refresh",
                "TopObjects.ReturnAll", "TopObjects.ReturnSelected", "TopObjects.ScriptInfo",
                "TopObjects.ShowBeacon", "TopObjects.TeleportToObject",
                "World.SetDoNotDisturb", "World.GetBusy", "View.Status.CheckDoNotDisturb",
                "World.SetAutorespond", "World.GetAutorespond",
                "World.SetAutorespondNonFriends", "World.GetAutorespondNonFriends",
                "World.SetRejectTeleportOffers", "World.GetRejectTeleportOffers",
                "World.SetRejectFriendshipRequests", "World.GetRejectFriendshipRequests",
                "World.SetRejectAllGroupInvites", "World.GetRejectAllGroupInvites",
                "Marketplace.ViewSort.Action", "Marketplace.ViewSort.CheckItem", "Avatar.OpenMarketplace", "LoadSLMarketplace", "OpenGridStatus",
                "TeleportHistory.TimeZone.Set", "TeleportHistory.TimeZone.Check",
                "TeleportHistory.TabOpen", "TeleportHistory.TabClose",
                "CameraPresets.Save", "CameraPresets.ShowPresetsList", "CameraPresets.Delete", "CameraPresets.Reset", "Presets.toggleCameraFloater", "PresetsCamera.RowClick", "CameraPresets.ChangeView", "Zoom.minus", "Zoom.plus", "Slider.value_changed", "Camera.track", "Camera.rotate", "Camera.roll_left", "Camera.roll_right",
                "Agent.toggleFlying", "Agent.getFlying", "Agent.enableFlying", "Agent.enableFlyLand",
                "Self.ForceSit", "Self.getForceSit", "Self.EnableForceSit",
                "Tools.ResyncAnimations", "Tools.StopAllAnimations", "Tools.DerenderAnimatedObjects", "Tools.Undeform",
                "Edit.TakeOff", "Edit.EnableTakeOff", "Avatar.ResetSelfSkeleton", "Avatar.ResetSelfSkeletonAndAnimations",
                "Marketplace.Toggle", "Marketplace.Enabled",
                "View.ResetView", "View.Mouselook", "View.EnableMouselook", "View.ToggleCameraFloater", "View.CheckCameraFloater",
                "MediaCtrl.Close", "MediaCtrl.Back", "MediaCtrl.Forward", "MediaCtrl.Home", "MediaCtrl.Stop", "MediaCtrl.MediaStop", "MediaCtrl.Reload", "MediaCtrl.Play", "MediaCtrl.Pause", "MediaCtrl.Open", "MediaCtrl.Zoom", "MediaCtrl.CommitURL", "MediaCtrl.MouseDown", "MediaCtrl.MouseUp", "MediaCtrl.CommitVolumeUp", "MediaCtrl.CommitVolumeDown", "MediaCtrl.Volume", "MediaCtrl.ToggleMute", "MediaCtrl.ShowVolumeSlider", "MediaCtrl.HideVolumeSlider", "MediaCtrl.SkipBack", "MediaCtrl.SkipForward",
                "MediaListCtrl.EnableAll", "MediaListCtrl.DisableAll", "MediaListCtrl.GoMediaPrefs", "MediaListCtrl.MoreLess", "SelectedMediaCtrl.Stop", "SelectedMediaCtrl.Play", "SelectedMediaCtrl.Pause", "SelectedMediaCtrl.Mute", "SelectedMediaCtrl.Volume", "SelectedMediaCtrl.Zoom", "SelectedMediaCtrl.Unzoom", "SelectedMediaCtrl.Action", "SelectedMediaCtrl.Visible",
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
                "Advanced.ShowDebugSettings", "ToggleSettingsDebug", "ToggleWebBrowser", "Help.ToggleHowTo",
                "SettingSelect", "CommitSettings", "ClickDefault", "UpdateFilter", "ClickCopy", "ClickSanityIcon",
                "LagMeter.ClickShrink", "InvOfferChiclet.Action", "Mention.CopyURI", "Mention.Chat",
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
            const auto separator=filename.find_last_of("/\\");
            const auto file=separator==std::string_view::npos?filename:filename.substr(separator+1);
            // Spatial speaker updates require LLWorld and avatars. Vintage
            // exposes this leaf directly in its otherwise shared CPU toolbar.
            // Omit it before construction, retaining all ordinary skin controls.
            return !((file=="panel_people.xml" && child=="nearby_panel") ||
                     (file=="panel_toolbar_view.xml" && child=="nearby_voice_output_monitor"));
        });
}
