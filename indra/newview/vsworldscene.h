// Native CPU primitive scene, independent of GL drawables. LGPL-2.1.
#pragma once
#include "vsworldrenderer.h"
#include "llhost.h"
#include "lluuid.h"
#include "v3math.h"
#include <memory>
class LLMessageSystem;
class VSUIResources;
class LLSD;
class VSWorldScene
{
public:
    VSWorldScene(VSWorldRenderer&, VSUIResources&);
    ~VSWorldScene();
    void reset(bool preserveAssets = false);
    void configureRegion(const LLSD& handshake);
    bool receive(LLMessageSystem&, U64 regionHandle, const LLUUID& agent);
    void requestUpdates(LLMessageSystem&, const LLHost&, const LLUUID&, const LLUUID&);
    std::vector<VSWorldRenderer::Packet> packets(const LLVector3& eye, float farClip,unsigned viewportHeight=0);
    static VSWorldRenderer::View view(const LLVector3& eye, unsigned width, unsigned height, float farClip);
    static VSWorldRenderer::View cameraView(const LLVector3& eye,const LLVector3& at,const LLVector3& up,
                                            unsigned width,unsigned height,float farClip);
    bool agentPosition(LLVector3&) const;
    LLSD evidence() const;
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
