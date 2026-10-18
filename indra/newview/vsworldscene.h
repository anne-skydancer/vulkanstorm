// Native CPU primitive scene, independent of GL drawables. LGPL-2.1.
#pragma once
#include "vsobjectsoundstate.h"
#include "vsworldrenderer.h"
#include "vscamerafocusobject.h"
#include "vsparceloverlay.h"
#include "vsregionterrain.h"
#include <boost/signals2.hpp>
#include "llhost.h"
#include "lluuid.h"
#include "v3math.h"
#include "llpartdata.h"
#include "llvolume.h"
#include "v3dmath.h"
#include "llquaternion.h"
#include <memory>
#include <map>
#include <optional>
#include <functional>
class LLMessageSystem;
class LLTextureEntry;
class LLMediaEntry;
class VSUIResources;
class LLSD;
class VSWorldAssets;
class VSWorldAvatar;
class LLChat;
class VSObjectEditParameters;
struct VSObjectPhysicsProperties;
struct VSObjectCostValues;
class LLNetworkData;
class LLVolume;
class LLVolumeParams;
class VSMaterialAssets;
class LLCamera;
class VSWorldScene
{
public:
    VSWorldScene(VSWorldRenderer&, VSUIResources&);
    ~VSWorldScene();
    void reset(bool preserveAssets = false);
    bool createTerrainPaintMap();
    void deleteTerrainPaintMap();
    void refreshInactiveImages();
    void configureRegion(const LLSD& handshake);
    bool openRegion(const LLHost&, U64 handle, U32 width, U32 height, U64 incarnation);
    bool activateRegion(const LLHost&, U64 handle, U32 width, U32 height, U64 incarnation);
    void retireRegion(const LLHost&);
    bool configureRegion(const LLHost&, const LLSD& handshake);
    struct ObjectHandle
    {
        LLHost host; LLUUID id; U32 local = 0;
        U64 region = 0, generation = 0;
        VSWorldContracts::OwnerKind kind = VSWorldContracts::OwnerKind::Object;
        LLUUID account;U64 accountGeneration=0;
    };
    // Identity-only, same non-joint DFS as LLViewerObject; no pose/mesh dependency.
    enum class FamilyTraversal {NonJoint,AllChildren};
    std::optional<std::vector<ObjectHandle>> linkedFamily(const ObjectHandle&,bool rootEdit=true,bool includeChildren=true,FamilyTraversal=FamilyTraversal::NonJoint) const;
    struct FaceHandle { ObjectHandle object; U32 face=0; U64 generation=0; };
    std::optional<ObjectHandle> packetObject(const VSWorldRenderer::Packet&) const;
    void appendAlphaHighlights(std::vector<VSWorldRenderer::Packet>&);
    struct Attachment
    {
        ObjectHandle object;
        LLUUID       item;
        U8           point = 0;
    };
    std::vector<Attachment> attachments() const;
    std::optional<Attachment> objectAttachment(const ObjectHandle&) const;
    struct AvatarLocation { LLUUID id; LLVector3d position; };
    std::vector<AvatarLocation> avatarLocations() const;
    struct ParticleSystem
    {
        ObjectHandle object;
        LLPartSysData data;
        LLUUID owner;
        U64 revision=0;
        bool fullMessage=false;
    };
    U64 particleRevision() const;
    std::size_t particleSystemCount() const;
    std::vector<ParticleSystem> particleSystems() const;
    std::optional<LLVector3> particlePosition(const ObjectHandle&,bool leftWrist=false) const;
    struct ObjectSound
    {
        ObjectHandle object;
        VSAudioObjectState state;
        LLUUID asset, owner;
        F32 gain = 0;
        U8 flags = 0;
    };
    std::optional<ObjectSound> objectSound(const ObjectHandle&) const;
    boost::signals2::connection observeObjectSounds(const boost::signals2::signal<void(const ObjectHandle&)>::slot_type&);
    bool derender(const ObjectHandle&);
    struct ObjectAdmission
    {
        U32 updateFlags=0,rootUpdateFlags=0;
        U8 pcode=0;
        bool orphaned=false,attachment=false,hud=false;
        U8 clickAction=0;
    };
    std::optional<ObjectAdmission> objectAdmission(const ObjectHandle&) const;
    std::optional<bool> objectSelectable(const ObjectHandle&) const;
    // Actual simulator parent-local values, independent of predicted rendering.
    struct LocalTransform{LLVector3 position,scale;LLQuaternion rotation;};
    std::optional<LocalTransform> objectLocalTransform(const ObjectHandle&) const;
    bool previewLocalTransform(const ObjectHandle&,const LocalTransform&,U32 components);
    bool rebuildObject(const ObjectHandle&);
    std::optional<bool> objectIsRootEdit(const ObjectHandle&) const;
    std::optional<LocalTransform> objectEditTransform(const ObjectHandle&) const;
    struct EditorTransform{LLVector3 localPosition,scale;LLVector3d globalPosition;LLQuaternion rotation;};
    std::optional<EditorTransform> editorTransform(const ObjectHandle&,S32 gltfNode,const LLVector3d& agentOrigin,bool renderedAttachment=true) const;
    bool previewObjectFlag(const ObjectHandle&,U32 mask,bool enabled);
    bool previewClickAction(const ObjectHandle&,U8 action);
    LLPointer<LLVolume> objectVolume(const ObjectHandle&) const;
    std::optional<std::array<LLVector3,2>> publishedObjectBounds(const ObjectHandle&,const LLVector3d& agentOrigin) const;
    std::optional<std::array<LLVector3,2>> sourceObjectBounds(const ObjectHandle&,const LLVector3d& agentOrigin) const;
    bool objectVertsVisible(const ObjectHandle&,const LLCamera&,bool allVerts,const LLVector3d& agentOrigin) const;
    struct SelectionCandidates
    {
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
        std::vector<ObjectHandle> objects;
    };
    std::optional<SelectionCandidates> selectionCandidates(const LLCamera&,const LLVector3d& agentOrigin) const;
    struct ObjectMetrics
    {
        ObjectHandle object;
        S32 lod = -1;
        bool mesh = false, rigged = false;
        std::optional<U64> faces, vertices, triangles;
        U64 publishedFaces = 0, publishedVertices = 0, publishedTriangles = 0;
        std::array<std::optional<U64>, 4> lodTriangles;
        std::optional<F32> lodRadius;
        std::optional<F32> pixelArea;
        // Original source algorithm, with genuine textures and material flags.
        std::optional<U32> renderCost;
        F32 streamingCost=0;
        U64 totalMeshBytes=0,visibleMeshBytes=0;
    };
    std::optional<ObjectMetrics> objectMetrics(const ObjectHandle&, bool requestLods = false);
    std::optional<F32> objectLinksetRenderCost(const ObjectHandle&) const;

    std::optional<LLVolumeParams> objectVolumeParams(const ObjectHandle&) const;
    bool refreshBakeTexture(const ObjectHandle&);
    bool previewVolume(const ObjectHandle&,const LLVolumeParams&);
    bool previewMaterial(const ObjectHandle&,U8);
    std::shared_ptr<const VSObjectEditParameters> editParameters(const ObjectHandle&) const;
    bool setPhysicsProperties(const ObjectHandle&,const VSObjectPhysicsProperties&);
    bool setObjectCostValues(const ObjectHandle&,const VSObjectCostValues&);
    bool setExtraParameter(const ObjectHandle&,U16 type,const LLNetworkData&,bool inUse,bool send=true);
    std::optional<LLUUID> objectInventoryItem(const ObjectHandle&) const;
    std::optional<U32> faceCount(const ObjectHandle&) const;
    std::optional<ObjectHandle> parentObject(const ObjectHandle&) const;
    std::optional<ObjectHandle> rootObject(const ObjectHandle&,bool stopAtAvatar=true) const;
    std::optional<std::vector<ObjectHandle>> directChildren(const ObjectHandle&) const;
    std::optional<LLVector3> windVelocity(const LLHost&,const LLVector3& regionPosition) const;
    std::shared_ptr<const VSWorldRenderer::AvatarTiming> avatarTiming(const ObjectHandle&);
    std::shared_ptr<const VSWorldRenderer::AvatarTiming> attachmentTiming(const ObjectHandle&);
    struct AvatarPerformance
    {
        ObjectHandle object;LLVector3d position;std::optional<F32> gpuMilliseconds;
        U32 complexity=0;bool control=false;std::optional<F32> tuningMilliseconds;bool invisible=false,jellydoll=false,tooSlow=false,tooSlowWithoutShadows=false;
    };
    struct AttachmentPerformance
    {
        Attachment attachment;std::optional<F32> gpuMilliseconds;std::optional<U32> complexity;
    };
    std::vector<AvatarPerformance> performanceAvatars() const;
    void refreshAvatarRenderSettings();
    std::vector<AttachmentPerformance> performanceAttachments() const;

    struct ManipulationObject
    {
        ObjectHandle object,root,rootEdit;
        U32 flags=0,rootEditFlags=0;
        U8 pcode=0;
        bool attachment=false,hud=false;
        LLVector3d position,rootPosition;
        LLVector3 renderPosition;
        LLQuaternion rotation,rootRotation,renderRotation;
    };
    std::optional<ManipulationObject> manipulationObject(const ObjectHandle&,const LLVector3d& agentOrigin) const;
    struct ObjectSnapshot
    {
        ObjectHandle object;
        LLUUID rootEditID,rootID;
        bool hud=false;
        LLUUID owner;
        bool ownerKnown=false;
        std::string nameValues,mediaVersion;
        U32 updateFlags=0,rootUpdateFlags=0;
        U8 pcode=0;
        U8 material=0,clickAction=0,rootClickAction=0;
        LLVector3d position;
        LLVector3 scale;
        bool attachment=false;
    };
    std::optional<ObjectSnapshot> objectSnapshot(const ObjectHandle&) const;
    std::vector<ObjectSnapshot> objectSnapshots() const;
    struct MapObjectSnapshot
    {
        ObjectHandle object;
        U32 updateFlags=0,rootUpdateFlags=0;
        U8 pcode=0;
        LLVector3d position;
        LLVector3 scale;
        bool attachment=false;
    };
    std::vector<MapObjectSnapshot> mapObjectSnapshots() const;
    std::shared_ptr<const VSResourceBudget::Reservation> reserveSpatialBytes(std::size_t);
    struct TerrainField
    {
        U64 revision=0,incarnation=0;
        U32 width=0,height=0;
        std::function<bool()> current;
        std::function<std::optional<float>(float,float)> sample;
        // Reference LLSurface initial grid: missing vertices are individually
        // zero until received, without discarding other received corners.
        std::function<std::optional<float>(float,float)> referenceHeight;
    };
    std::optional<VSRegionTerrain> terrainSettings(const LLHost&);
    boost::signals2::connection observeTerrainSettings(const LLHost&,const boost::signals2::signal<void()>::slot_type&);
    std::optional<U64> terrainRevision(const LLHost&) const;
    std::optional<TerrainField> terrainField(const LLHost&) const;

    struct Pick
    {
        FaceHandle           face;
        LLVector3            position, normal;
        std::array<float, 2> uv{-1,-1},st{-1,-1};
        std::array<float,4> tangent{};
        std::array<S32,2> xy{-1,-1};
        bool surfaceKnown=false;
        S32                  gltfNode = -1, gltfPrimitive = -1;
        float                fraction = 1;
        bool                 hud      = false;
        bool                 land     = false;
        bool                 avatar   = false;
    };
    struct PickOptions
    {
        bool transparent=false,rigged=false,unselectable=false,reflectionProbe=false;
        std::optional<ObjectHandle> exactObject; // Reference cursorIntersect(this_object), no global spatial gate.
        std::function<bool()> buildVisible;
        std::function<bool(const ObjectHandle&)> canSelect;
        std::function<bool()> renderSelfAvatar;
    };
    std::optional<Pick> pick(const LLVector3& start,const LLVector3& end,const PickOptions&,float lineRadius=0,bool hud=false) const;
    std::optional<Pick>       pick(const LLVector3& start, const LLVector3& end, bool transparent = true, float lineRadius = 0,
                                   bool hud = false) const;
    std::optional<Pick>       pickHUD(float normalizedX, float normalizedY, float aspect, float zoom,const PickOptions* options=nullptr) const;
    std::optional<FaceHandle> face(const ObjectHandle&, U32 index) const;
    bool current(const FaceHandle&) const;
    std::optional<VSWorldContracts::Material> material(const FaceHandle&) const;
    bool copyTextureEntry(const FaceHandle&, LLTextureEntry&) const;
    S32 textureEntryCount(const ObjectHandle&) const;
    // CPU/controller entry, independent of the previous GPU publication.
    LLTextureEntry* materialEntry(const ObjectHandle&, U32);
    VSMaterialAssets* legacyMaterialAssets(const LLHost&);
    void tickLegacyMaterials();
    S32 editTextureEntry(const ObjectHandle&,U32,const std::function<S32(LLTextureEntry&)>&);
    bool packTextureEntries(const ObjectHandle&,LLMessageSystem&) const;
    boost::signals2::connection observeMaterialUpdates(const boost::signals2::signal<void(const ObjectHandle&, S32)>::slot_type&);
    bool copyMaterialEntry(const ObjectHandle&, U32, LLTextureEntry&) const;
    struct ResolvedTexture
    {
        bool present=false,missing=false;
        LLUUID asset;
        VSUIRenderer::Image image;
        LLPointer<LLImageRaw> raw;
        U64 generation=0;
        explicit operator bool() const{return present;}
    };
    // Resolve the actual source image without changing the TE/magic asset ID.
    ResolvedTexture resolveTexture(const ObjectHandle&,const LLUUID&,bool includePixels=true) const;
    ResolvedTexture diffuseTexture(const ObjectHandle&,U32 side,bool includePixels=true) const;
    LLUUID renderMaterialID(const ObjectHandle&, U32) const;
    bool previewMaterialID(const ObjectHandle&, U32, const LLUUID&);
    bool setLegacyMaterialPreview(const ObjectHandle&,bool);
    bool legacyMaterialPreview(const ObjectHandle&) const;
    LLUUID mediaTexture(const ObjectHandle&,U32 face) const;
    const LLMediaEntry* mediaEntry(const ObjectHandle&,U32 face) const;
    bool syncMediaData(const ObjectHandle&,U32,const LLSD&,bool merge,bool ignoreAgent,bool updatePlayer=true);
    bool sendMediaDataUpdate(const ObjectHandle&);
    bool mediaDataReady(const ObjectHandle&) const;
    bool prepareMediaData(const ObjectHandle&);
    std::optional<std::array<std::array<float, 3>, 2>> selectionBounds(const ObjectHandle&) const;
    bool requestTaskInventory(LLMessageSystem&, const ObjectHandle&, const LLUUID& agent, const LLUUID& session);
    LLSD taskInventory(const ObjectHandle&) const;
    std::optional<ObjectHandle> object(const LLHost&, U32 local) const;
    std::optional<ObjectHandle> object(const LLHost&, const LLUUID&) const;
    std::optional<ObjectHandle>          object(const LLUUID&) const;
    bool                                 loadGLTF(const ObjectHandle&, const std::string& filename);
    bool current(const ObjectHandle&) const;
    bool setIcon(const ObjectHandle&,const std::string& imageName,bool scriptError=false);
    void refreshNameTags();
    void appendVoice(std::vector<VSWorldRenderer::Packet>&);
    struct DebugBox { std::array<std::array<float,3>,2> bounds; std::array<float,4> color; F32 width=1; bool depthTest=true,hud=false; };
    void appendObjectBeacons(std::vector<VSWorldRenderer::Packet>&,std::vector<DebugBox>&);
    void appendAppearanceAttachments(std::vector<VSWorldRenderer::Packet>&, VSWorldRenderer&);
    void invalidateNameTags(const LLUUID& id=LLUUID::null);
    struct ObjectTransform { LLVector3d position; LLQuaternion rotation; LLVector3 scale; LLVector3 regionPosition; };
    std::optional<ObjectTransform> objectTransform(const ObjectHandle&) const;
    std::optional<VSCameraFocusObject> focusObject(const ObjectHandle&,const LLVector3d& agentOrigin) const;
    std::optional<std::array<LLVector3,8>> mediaFaceBounds(const ObjectHandle&,U32 faceIndex,const LLVector3d& agentOrigin) const;
    bool freeze(const ObjectHandle&, bool);
    bool freezeObject(const ObjectHandle&,bool);
    bool setSelectionState(const ObjectHandle&,bool);
    bool initializeSelectionMotion(const ObjectHandle&);
    bool initializeDeselectionMotion(const ObjectHandle&);
    std::function<bool()> guard(const ObjectHandle&) const;
    std::function<bool()> guard(const FaceHandle&) const;
    bool initializeSelf(const LLUUID& agent,U64 accountGeneration=0);
    void replaceAsset(const LLUUID&);
    void                                 derenderAnimatedObjects();
    bool receive(LLMessageSystem&, U64 regionHandle, const LLUUID& agent);
    void requestUpdates(LLMessageSystem&, const LLHost&, const LLUUID&, const LLUUID&);
    std::vector<VSWorldRenderer::Packet> packets(const LLVector3& eye, float farClip, unsigned viewportHeight = 0,
                                                 unsigned viewportWidth = 0);
    static VSWorldRenderer::View view(const LLVector3& eye, unsigned width, unsigned height, float farClip);
    static VSWorldRenderer::View cameraView(const LLVector3& eye,const LLVector3& at,const LLVector3& up,
                                            unsigned width,unsigned height,float farClip,float verticalFov = 1.0471975512f);
    bool agentPosition(LLVector3&) const;
    void nameTagChat(const LLChat&);
    void nameTagTyping(const LLUUID&, bool);
    bool nameTagTyping(const LLUUID&) const;
    VSWorldAvatar*                       selfAvatar() const;
    VSWorldAvatar*                       avatarForObject(const LLUUID&) const;
    void                                 resyncAvatarAnimations();
    LLUUID                               rootID(const LLUUID&) const;
    bool                                 isAttachment(const LLUUID&) const;
    LLSD evidence() const;
#if VS_VULKAN_DIAGNOSTICS
    LLSD terrainStatus() const;
    bool skyCurrent() const;
#endif
private:
    void publishGroundFrame(const LLVector3& eye);
    struct Budgets;
    VSWorldScene(VSWorldRenderer&, VSUIResources&, std::shared_ptr<VSWorldAssets>, std::shared_ptr<Budgets>);
    struct Impl;
    std::shared_ptr<Impl> mImpl;
    std::map<LLHost, std::unique_ptr<VSWorldScene>> mNeighbours;
    bool mRoot = true;
};
