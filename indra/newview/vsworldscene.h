// Native CPU primitive scene, independent of GL drawables. LGPL-2.1.
#pragma once
#include "vsworldrenderer.h"
#include "vsparceloverlay.h"
#include "vsregionterrain.h"
#include <boost/signals2.hpp>
#include "llhost.h"
#include "lluuid.h"
#include "v3math.h"
#include "v3dmath.h"
#include "llquaternion.h"
#include <memory>
#include <map>
#include <optional>
#include <functional>
class LLMessageSystem;
class LLTextureEntry;
class VSUIResources;
class LLSD;
class VSWorldAssets;
class VSWorldAvatar;
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
    struct FaceHandle { ObjectHandle object; U32 face=0; U64 generation=0; };
    struct Attachment
    {
        ObjectHandle object;
        LLUUID       item;
        U8           point = 0;
    };
    std::vector<Attachment> attachments() const;
    struct AvatarLocation { LLUUID id; LLVector3d position; };
    std::vector<AvatarLocation> avatarLocations() const;
    struct ObjectAdmission
    {
        U32 updateFlags=0,rootUpdateFlags=0;
        U8 pcode=0;
        bool orphaned=false,attachment=false,hud=false;
    };
    std::optional<ObjectAdmission> objectAdmission(const ObjectHandle&) const;
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
        std::array<float, 2> uv{};
        S32                  gltfNode = -1, gltfPrimitive = -1;
        float                fraction = 1;
        bool                 hud      = false;
        bool                 land     = false;
    };
    std::optional<Pick>       pick(const LLVector3& start, const LLVector3& end, bool transparent = true, float lineRadius = 0,
                                   bool hud = false) const;
    std::optional<Pick>       pickHUD(float normalizedX, float normalizedY, float aspect, float zoom) const;
    std::optional<FaceHandle> face(const ObjectHandle&, U32 index) const;
    bool current(const FaceHandle&) const;
    std::optional<VSWorldContracts::Material> material(const FaceHandle&) const;
    bool copyTextureEntry(const FaceHandle&, LLTextureEntry&) const;
    std::optional<std::array<std::array<float, 3>, 2>> selectionBounds(const ObjectHandle&) const;
    bool requestTaskInventory(LLMessageSystem&, const ObjectHandle&, const LLUUID& agent, const LLUUID& session);
    LLSD taskInventory(const ObjectHandle&) const;
    std::optional<ObjectHandle> object(const LLHost&, U32 local) const;
    std::optional<ObjectHandle>          object(const LLUUID&) const;
    bool                                 loadGLTF(const ObjectHandle&, const std::string& filename);
    bool current(const ObjectHandle&) const;
    struct ObjectTransform { LLVector3d position; LLQuaternion rotation; LLVector3 scale; };
    std::optional<ObjectTransform> objectTransform(const ObjectHandle&) const;
    bool freeze(const ObjectHandle&, bool);
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
