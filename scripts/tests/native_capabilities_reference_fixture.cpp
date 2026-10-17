void LLViewerRegionImpl::buildCapabilityNames(LLSD& capabilityNames)
{
    capabilityNames.append("AbuseCategories");
    capabilityNames.append("AcceptFriendship");
    capabilityNames.append("AcceptGroupInvite"); // ReadOfflineMsgs recieved messages only!!!
    capabilityNames.append("AgentPreferences");
    capabilityNames.append("AgentProfile");
    capabilityNames.append("AgentState");
    capabilityNames.append("AttachmentResources");
    capabilityNames.append("AvatarPickerSearch");
    capabilityNames.append("AvatarRenderInfo");
    capabilityNames.append("CharacterProperties");
    capabilityNames.append("ChatSessionRequest");
    capabilityNames.append("CopyInventoryFromNotecard");
    capabilityNames.append("CreateInventoryCategory");
    capabilityNames.append("DeclineFriendship");
    capabilityNames.append("DeclineGroupInvite"); // ReadOfflineMsgs recieved messages only!!!
    capabilityNames.append("DispatchRegionInfo");
    capabilityNames.append("DirectDelivery");
    capabilityNames.append("EnvironmentSettings");
    capabilityNames.append("EstateAccess");
// <FS:CR> Aurora Sim
    capabilityNames.append("DispatchOpenRegionSettings");
// </FS:CR> Aurora Sim
    capabilityNames.append("EstateChangeInfo");
    capabilityNames.append("EventQueueGet");
    capabilityNames.append("ExtEnvironment");

    // <FS:Ansariel> Force HTTP features on SL
    if (use_http_inventory()) {
    // </FS:Ansariel>
    capabilityNames.append("FetchLib2");
    capabilityNames.append("FetchLibDescendents2");
    capabilityNames.append("FetchInventory2");
    capabilityNames.append("FetchInventoryDescendents2");
    capabilityNames.append("IncrementCOFVersion");
    capabilityNames.append("RequestTaskInventory");
    AISAPI::getCapNames(capabilityNames);
    } //</FS:Ansariel>

    capabilityNames.append("InterestList");

    capabilityNames.append("InventoryThumbnailUpload");
    capabilityNames.append("GetDisplayNames");
    capabilityNames.append("GetExperiences");
    capabilityNames.append("AgentExperiences");
    capabilityNames.append("FindExperienceByName");
    capabilityNames.append("GetExperienceInfo");
    capabilityNames.append("GetAdminExperiences");
    capabilityNames.append("GetCreatorExperiences");
    capabilityNames.append("ExperiencePreferences");
    capabilityNames.append("GroupExperiences");
    capabilityNames.append("UpdateExperience");
    capabilityNames.append("IsExperienceAdmin");
    capabilityNames.append("IsExperienceContributor");
    capabilityNames.append("RegionExperiences");
    capabilityNames.append("ExperienceQuery");
    // <FS:Ansariel> [UDP Assets]
    capabilityNames.append("GetMesh");
    capabilityNames.append("GetMesh2");
    // </FS:Ansariel> [UDP Assets]
    capabilityNames.append("GetMetadata");
    capabilityNames.append("GetObjectCost");
    capabilityNames.append("GetObjectPhysicsData");
    capabilityNames.append("GetTexture"); // <FS:Ansariel> [UDP Assets]
    capabilityNames.append("GroupAPIv1");
    capabilityNames.append("GroupMemberData");
    capabilityNames.append("GroupProposalBallot");
    capabilityNames.append("HomeLocation");
    capabilityNames.append("LandResources");
    capabilityNames.append("LSLSyntax");
    capabilityNames.append("MapLayer");
    capabilityNames.append("MapLayerGod");
    capabilityNames.append("MeshUploadFlag");
    capabilityNames.append("ModifyMaterialParams");
    capabilityNames.append("ModifyRegion");
    capabilityNames.append("NavMeshGenerationStatus");
    capabilityNames.append("NewFileAgentInventory");
    capabilityNames.append("ObjectAnimation");
    capabilityNames.append("ObjectMedia");
    capabilityNames.append("ObjectMediaNavigate");
    capabilityNames.append("ObjectNavMeshProperties");
    capabilityNames.append("ParcelPropertiesUpdate");
    capabilityNames.append("ParcelVoiceInfoRequest");
    capabilityNames.append("ProductInfoRequest");
    capabilityNames.append("ProvisionVoiceAccountRequest");
    capabilityNames.append("VoiceSignalingRequest");
    capabilityNames.append("ReadOfflineMsgs"); // Requires to respond reliably: AcceptFriendship, AcceptGroupInvite, DeclineFriendship, DeclineGroupInvite
    capabilityNames.append("RegionObjects");
    capabilityNames.append("RegionSchedule");
    capabilityNames.append("RemoteParcelRequest");
    capabilityNames.append("RenderMaterials");
    capabilityNames.append("RequestTextureDownload");
    capabilityNames.append("ResourceCostSelected");
    capabilityNames.append("RetrieveNavMeshSrc");
    capabilityNames.append("SearchStatRequest");
    capabilityNames.append("SearchStatTracking");
    capabilityNames.append("SendPostcard");
    capabilityNames.append("SendUserReport");
    capabilityNames.append("SendUserReportWithScreenshot");
    capabilityNames.append("ServerReleaseNotes");
    capabilityNames.append("SetDisplayName");
    capabilityNames.append("SimConsoleAsync");
    capabilityNames.append("SimulatorFeatures");
    capabilityNames.append("StartGroupProposal");
    capabilityNames.append("TerrainNavMeshProperties");
    capabilityNames.append("TextureStats");
    capabilityNames.append("UntrustedSimulatorMessage");
    capabilityNames.append("UpdateAgentInformation");
    capabilityNames.append("UpdateAgentLanguage");
    capabilityNames.append("UpdateAvatarAppearance");
    capabilityNames.append("UpdateGestureAgentInventory");
    capabilityNames.append("UpdateGestureTaskInventory");
    capabilityNames.append("UpdateNotecardAgentInventory");
    capabilityNames.append("UpdateNotecardTaskInventory");
    capabilityNames.append("UpdateScriptAgent");
    capabilityNames.append("UpdateScriptTask");
    capabilityNames.append("UpdateSettingsAgentInventory");
    capabilityNames.append("UpdateSettingsTaskInventory");
    capabilityNames.append("UploadAgentProfileImage");
    capabilityNames.append("UpdateMaterialAgentInventory");
    capabilityNames.append("UpdateMaterialTaskInventory");
    capabilityNames.append("UploadBakedTexture");
    capabilityNames.append("UserInfo");
    capabilityNames.append("ViewerAsset");
    capabilityNames.append("ViewerBenefits");
    capabilityNames.append("ViewerMetrics");
    capabilityNames.append("ViewerStartAuction");
    capabilityNames.append("ViewerStats");
    // <FS:Beq> [FIRE-34104] Add special opensim Capability for signalling PBR Terrain support
    constexpr char OpenSimPBRTerrain[]{"VETPBR"};
    capabilityNames.append(OpenSimPBRTerrain);
    // </FS:Beq>

    // Please add new capabilities alphabetically to reduce
    // merge conflicts.
}