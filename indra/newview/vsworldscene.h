// Native CPU primitive scene, independent of GL drawables. LGPL-2.1.
#pragma once
#include "vsworldrenderer.h"
#include "llhost.h"
#include "lluuid.h"
#include "v3math.h"
#include <memory>
#include <map>
#include <optional>
#include <functional>
class LLMessageSystem;
class VSUIResources;
class LLSD;
class VSWorldAssets;
class VSWorldScene
{
public:
    VSWorldScene(VSWorldRenderer&, VSUIResources&);
    ~VSWorldScene();
    void reset(bool preserveAssets = false);
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
    std::optional<FaceHandle> face(const ObjectHandle&, U32 index) const;
    bool current(const FaceHandle&) const;
    std::optional<VSWorldContracts::Material> material(const FaceHandle&) const;
    bool requestTaskInventory(LLMessageSystem&, const ObjectHandle&, const LLUUID& agent, const LLUUID& session);
    LLSD taskInventory(const ObjectHandle&) const;
    std::optional<ObjectHandle> object(const LLHost&, U32 local) const;
    bool current(const ObjectHandle&) const;
    bool freeze(const ObjectHandle&, bool);
    std::function<bool()> guard(const ObjectHandle&) const;
    std::function<bool()> guard(const FaceHandle&) const;
    bool initializeSelf(const LLUUID& agent,U64 accountGeneration=0);
    void replaceAsset(const LLUUID&);
    bool receive(LLMessageSystem&, U64 regionHandle, const LLUUID& agent);
    void requestUpdates(LLMessageSystem&, const LLHost&, const LLUUID&, const LLUUID&);
    std::vector<VSWorldRenderer::Packet> packets(const LLVector3& eye, float farClip,unsigned viewportHeight=0);
    static VSWorldRenderer::View view(const LLVector3& eye, unsigned width, unsigned height, float farClip);
    static VSWorldRenderer::View cameraView(const LLVector3& eye,const LLVector3& at,const LLVector3& up,
                                            unsigned width,unsigned height,float farClip);
    bool agentPosition(LLVector3&) const;
    LLSD evidence() const;
private:
    struct Budgets;
    VSWorldScene(VSWorldRenderer&, VSUIResources&, std::shared_ptr<VSWorldAssets>, std::shared_ptr<Budgets>);
    struct Impl;
    std::shared_ptr<Impl> mImpl;
    std::map<LLHost, std::unique_ptr<VSWorldScene>> mNeighbours;
    bool mRoot = true;
};
