// Shared original LLFace planar projection/alignment with actual source metadata.
// LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsplanarface.h"
#include "vssurfacecoordinates.h"
#include "llviewerwindow.h"
#include "llviewerobject.h"
#include "llvovolume.h"
#include "llagent.h"
#include "vsvulkancontext.h"
#include "lldrawable.h"
#include "llmaterial.h"

std::optional<VSPlanarFaceFrame> vs_planar_face_frame(const LLVolumeFace& vf,const LLMatrix4& vol_mat)
{
    if(vf.mNumVertices<=0 || !vf.mNormals || !vf.mTangents || !vf.mCenter)return {};
    VSPlanarFaceFrame result;
    const LLVector4a& normal4a=*vf.mNormals;
    const LLVector4a& tangent=*vf.mTangents;
    LLVector4a binormal4a;binormal4a.setCross3(normal4a,tangent);binormal4a.mul(tangent.getF32ptr()[3]);
    LLVector2 projected_binormal;
    VSSurfaceCoordinates::planarProjection(projected_binormal,normal4a,*vf.mCenter,binormal4a);
    projected_binormal-=LLVector2(.5f,.5f);
    result.scale=projected_binormal.length();
    if(!std::isfinite(result.scale) || result.scale<=0)return {};
    projected_binormal.normalize();
    F32 ang=acos(projected_binormal.mV[VY]);ang=(projected_binormal.mV[VX]<0.f)?-ang:ang;
    LLVector3 binormal(binormal4a.getF32ptr()),normal(normal4a.getF32ptr());
    binormal.rotVec(ang,normal);
    LLQuaternion local_rot(binormal%normal,binormal,normal);
    result.rotation=local_rot*vol_mat.quaternion();result.position=vol_mat.getTranslation();
    return result.position.isFinite() && result.rotation.isFinite()?std::optional<VSPlanarFaceFrame>(result):std::nullopt;
}

bool vs_align_planar_texture(const LLTextureEntry& entry,const VSPlanarFaceFrame& self,
    const LLTextureEntry& alignEntry,const VSPlanarFaceFrame& alignTo,LLVector2* res_st_offset,LLVector2* res_st_scale,F32* res_st_rot,LLRender::eTexIndex map)
{
    const auto* tep=&entry;const auto* orig_tep=&alignEntry;
    if(orig_tep->getTexGen()!=LLTextureEntry::TEX_GEN_PLANAR || tep->getTexGen()!=LLTextureEntry::TEX_GEN_PLANAR ||
        self.scale<=0 || alignTo.scale<=0)return false;
    F32 map_rot = 0.f, map_scaleS = 0.f, map_scaleT = 0.f, map_offsS = 0.f, map_offsT = 0.f;

    LLMaterial* mat = orig_tep->getMaterialParams();
    if (!mat && map != LLRender::DIFFUSE_MAP)
    {
        LL_WARNS_ONCE("llface") << "Face is set to use specular or normal map but has no material, defaulting to diffuse" << LL_ENDL;
        map = LLRender::DIFFUSE_MAP;
    }

    switch (map)
    {
    case LLRender::DIFFUSE_MAP:
        map_rot = orig_tep->getRotation();
        map_scaleS = orig_tep->mScaleS;
        map_scaleT = orig_tep->mScaleT;
        map_offsS = orig_tep->mOffsetS;
        map_offsT = orig_tep->mOffsetT;
        break;
    case LLRender::NORMAL_MAP:
        if (mat->getNormalID().isNull())
        {
            return false;
        }
        map_rot = mat->getNormalRotation();
        map_scaleS = mat->getNormalRepeatX();
        map_scaleT = mat->getNormalRepeatY();
        map_offsS = mat->getNormalOffsetX();
        map_offsT = mat->getNormalOffsetY();
        break;
    case LLRender::SPECULAR_MAP:
        if (mat->getSpecularID().isNull())
        {
            return false;
        }
        map_rot = mat->getSpecularRotation();
        map_scaleS = mat->getSpecularRepeatX();
        map_scaleT = mat->getSpecularRepeatY();
        map_offsS = mat->getSpecularOffsetX();
        map_offsT = mat->getSpecularOffsetY();
        break;
    default: /*make compiler happy*/
        break;
    }

    const auto& orig_pos=alignTo.position;const auto& this_pos=self.position;
    const auto& orig_face_rot=alignTo.rotation;const auto& this_face_rot=self.rotation;
    const auto orig_proj_scale=alignTo.scale;const auto this_proj_scale=self.scale;
    // The rotation of "this face's" texture:
    LLQuaternion orig_st_rot = LLQuaternion(map_rot, LLVector3::z_axis) * orig_face_rot;
    LLQuaternion this_st_rot = orig_st_rot * ~this_face_rot;
    F32 x_ang, y_ang, z_ang;
    this_st_rot.getEulerAngles(&x_ang, &y_ang, &z_ang);
    *res_st_rot = z_ang;

    // Offset and scale of "this face's" texture:
    LLVector3 centers_dist = (this_pos - orig_pos) * ~orig_st_rot;
    LLVector3 st_scale(map_scaleS, map_scaleT, 1.f);
    st_scale *= orig_proj_scale;
    centers_dist.scaleVec(st_scale);
    LLVector2 orig_st_offset(map_offsS, map_offsT);

    *res_st_offset = orig_st_offset + (LLVector2)centers_dist;
    res_st_offset->mV[VX] -= (S32)res_st_offset->mV[VX];
    res_st_offset->mV[VY] -= (S32)res_st_offset->mV[VY];

    st_scale /= this_proj_scale;
    *res_st_scale = (LLVector2)st_scale;
    return true;
}
VSPlanarFace::VSPlanarFace(LLFace* face):mLegacy(face),mLegacyOwner(face?face->getViewerObject():nullptr),mSide(face?face->getTEOffset():-1){}
VSPlanarFace::VSPlanarFace(const VSWorldScene::ObjectHandle& object,S32 side):mObject(object),mSide(side),mNative(true)
{
#if VS_NATIVE_VULKAN
    if(gViewerWindow && gViewerWindow->nativeContext())mCurrent=gViewerWindow->nativeContext()->world().guard(object);
#endif
}
bool VSPlanarFace::current() const
{
    if(mSide<0)return false;
    if(!mNative)
        return mLegacyOwner && !mLegacyOwner->isDead() && mLegacyOwner->mDrawable &&
            mSide<mLegacyOwner->mDrawable->getNumFaces() && mLegacyOwner->mDrawable->getFace(mSide)==mLegacy;
#if VS_NATIVE_VULKAN
    return mCurrent && mCurrent() && gViewerWindow && gViewerWindow->nativeContext() &&
        mSide<gViewerWindow->nativeContext()->world().textureEntryCount(mObject);
#else
    return false;
#endif
}
S32 VSPlanarFace::volumeFaceCount() const
{
    if(!current())return 0;
    if(!mNative)
    {
        const auto* volume=mLegacyOwner->getVolume();return volume?volume->getNumVolumeFaces():0;
    }
#if VS_NATIVE_VULKAN
    const auto volume=gViewerWindow->nativeContext()->world().objectVolume(mObject);
    return volume?volume->getNumVolumeFaces():0;
#else
    return 0;
#endif
}
const LLTextureEntry* VSPlanarFace::getTextureEntry() const
{
    if(!current())return nullptr;
    if(!mNative)return mLegacy->getTextureEntry();
#if VS_NATIVE_VULKAN
    return gViewerWindow->nativeContext()->world().materialEntry(mObject,U32(mSide));
#else
    return nullptr;
#endif
}
std::optional<VSPlanarFaceFrame> VSPlanarFace::frame() const
{
    if(!current())return {};
    if(!mNative)
    {
        const auto* volume=mLegacyOwner->getVolume();if(!volume || mSide>=volume->getNumVolumeFaces())return {};
        return vs_planar_face_frame(volume->getVolumeFace(mSide),mLegacy->getWorldMatrix());
    }
#if VS_NATIVE_VULKAN
    auto& world=gViewerWindow->nativeContext()->world();
    const auto volume=world.objectVolume(mObject);
    const auto pose=world.focusObject(mObject,gAgent.getPosGlobalFromAgent(LLVector3::zero));
    if(!volume || mSide>=volume->getNumVolumeFaces() || !pose || !pose->current())return {};
    // LLDrawable enforces scale1; both rigid and LLVolumeImplFlexible worldmatrix
    // reference this exact rigid drawable frame. Do not use assetToAgentMatrix.
    const auto result=vs_planar_face_frame(volume->getVolumeFace(mSide),pose->renderMatrix);
    return current()?result:std::nullopt;
#else
    return {};
#endif
}
bool VSPlanarFace::getPlanarProjectedParams(LLQuaternion* rotation,LLVector3* position,F32* scale) const
{
    const auto value=frame();if(!value)return false;
    *rotation=value->rotation;*position=value->position;*scale=value->scale;return true;
}
bool VSPlanarFace::calcAlignedPlanarTE(const VSPlanarFace* alignTo,LLVector2* offset,LLVector2* scale,F32* rotation,LLRender::eTexIndex map) const
{
    if(!alignTo || !current() || !alignTo->current())return false;
    const auto self=frame(),other=alignTo->frame();
    const auto* te=getTextureEntry();const auto* otherTe=alignTo->getTextureEntry();
    if(!self || !other || !te || !otherTe)return false;
    return vs_align_planar_texture(*te,*self,*otherTe,*other,offset,scale,rotation,map);
}
bool VSPlanarFace::sameFace(const VSPlanarFace& other) const
{
    if(mNative!=other.mNative || !current() || !other.current())return false;
    if(!mNative)return mLegacy==other.mLegacy;
    return mObject.host==other.mObject.host && mObject.id==other.mObject.id && mObject.local==other.mObject.local &&
        mObject.region==other.mObject.region && mObject.generation==other.mObject.generation &&
        mObject.kind==other.mObject.kind && mObject.account==other.mObject.account &&
        mObject.accountGeneration==other.mObject.accountGeneration && mSide==other.mSide;
}
