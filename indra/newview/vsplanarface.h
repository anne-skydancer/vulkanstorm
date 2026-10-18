// Original planar alignment on genuine GL faces or current native source faces.
// LGPL-2.1, matching LLFace.
#pragma once
#include "llface.h"
#include "vsworldscene.h"
#include <optional>
struct VSPlanarFaceFrame
{
    LLQuaternion rotation;
    LLVector3 position;
    F32 scale=0;
};
std::optional<VSPlanarFaceFrame> vs_planar_face_frame(const LLVolumeFace&,const LLMatrix4&);
bool vs_align_planar_texture(const LLTextureEntry&,const VSPlanarFaceFrame&,
    const LLTextureEntry&,const VSPlanarFaceFrame&,LLVector2*,LLVector2*,F32*,LLRender::eTexIndex);
class VSPlanarFace
{
public:
    explicit VSPlanarFace(LLFace*);
    VSPlanarFace(const VSWorldScene::ObjectHandle&,S32 side);
    bool current() const;
    S32 volumeFaceCount() const;
    const LLTextureEntry* getTextureEntry() const;
    bool getPlanarProjectedParams(LLQuaternion*,LLVector3*,F32*) const;
    bool calcAlignedPlanarTE(const VSPlanarFace*,LLVector2*,LLVector2*,F32*,LLRender::eTexIndex=LLRender::DIFFUSE_MAP) const;
    bool sameFace(const VSPlanarFace&) const;
private:
    LLFace* mLegacy=nullptr;
    LLPointer<LLViewerObject> mLegacyOwner;
    VSWorldScene::ObjectHandle mObject;
    S32 mSide=-1;
    std::function<bool()> mCurrent;
    bool mNative=false;
    std::optional<VSPlanarFaceFrame> frame() const;
};
