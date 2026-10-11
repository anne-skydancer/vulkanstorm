// Native CPU avatar appearance, animation and Vulkan mesh publication. LGPL-2.1.
#pragma once
#include "vsworldrenderer.h"
#include "lluuid.h"
#include "v3math.h"
#include "llquaternion.h"
#include <functional>
#include <memory>
class LLMessageSystem;
class VSUIResources;
class LLSD;
class LLVolumeFace;
class VSWorldAvatar
{
public:
    using GroundQuery = std::function<bool(const LLVector3&, LLVector3&, LLVector3&)>;
    static void initializeClass();
    static void cleanupClass();
    VSWorldAvatar(const LLUUID&, bool self, U64 regionHandle, VSWorldRenderer&, VSUIResources&, GroundQuery);
    ~VSWorldAvatar();
    bool appearance(LLMessageSystem&);
    bool animations(LLMessageSystem&);
    void transform(const LLVector3&, const LLQuaternion&, const LLVector3& velocity, const LLVector3& angularVelocity);
    void append(std::vector<VSWorldRenderer::Packet>&, const LLVector3& eye, float farClip,unsigned viewportHeight,float timeDilation);
    bool attachmentTransform(unsigned point, LLVector3&, LLQuaternion&);
    bool skinRiggedFace(const LLVolumeFace&,const LLSD&,std::vector<VSWorldRenderer::Vertex>&);
    LLSD evidence() const;
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
