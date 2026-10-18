// Native glTF scene ownership and CPU animation. LGPL-2.1.
#pragma once
#include "vsworldrenderer.h"
#include "vsresourcebudget.h"
#include "lluuid.h"
#include "v3math.h"
#include "llquaternion.h"
#include <functional>
#include <memory>
class VSUIResources;
class LLSD;
class LLMatrix4a;
class VSWorldGLTF
{
public:
    VSWorldGLTF(const LLUUID&, VSWorldRenderer&, VSUIResources&, VSResourceBudget, std::function<bool()> current);
    VSWorldGLTF(const std::string& filename, VSWorldRenderer&, VSUIResources&, VSResourceBudget, std::function<bool()> current);
    bool ready() const;
    bool nodeMatrix(S32 node,LLMatrix4a&) const;
    bool hasPublication() const;
    std::optional<std::array<std::array<float,3>,2>> publishedBounds() const;
    void releaseRenderingResources();
    void refreshInactiveImages();
    ~VSWorldGLTF();
    void append(std::vector<VSWorldRenderer::Packet>&, const LLVector3&, const LLQuaternion&, const LLVector3& scale, const LLVector3& eye,unsigned viewportHeight=1024);
    LLSD evidence() const;

private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
