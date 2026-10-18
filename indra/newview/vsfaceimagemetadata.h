// Actual source image metadata for the shared Face controller, without GL facade textures.
// LGPL-2.1.
#pragma once
#include "llpointer.h"
#include "llviewertexture.h"
class VSMaterialObject;
class LLViewerObject;
class VSFaceImageMetadata
{
public:
    explicit operator bool()const{return mPresent;}
    const VSFaceImageMetadata* operator->()const{return this;}
    LLUUID getID()const{return mLegacy?mLegacy->getID():mID;}
    LLGLenum getPrimaryFormat()const;
    bool isMissingAsset()const{return mLegacy?mLegacy->isMissingAsset():mMissing;}
private:
    friend VSFaceImageMetadata vs_face_image(LLViewerObject*,S32);
    friend VSFaceImageMetadata vs_face_image(const VSMaterialObject*,S32);
    LLPointer<LLViewerTexture> mLegacy;
    LLUUID mID;
    bool mPresent=false,mMissing=false;
    S32 mComponents=0;
    LLGLenum mSourceFormat=0;
};
VSFaceImageMetadata vs_face_image(LLViewerObject*,S32);
#if VS_NATIVE_VULKAN
VSFaceImageMetadata vs_face_image(const VSMaterialObject*,S32);
#endif
