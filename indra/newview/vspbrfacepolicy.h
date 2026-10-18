// Original LLSelectMgr PBR face policy, shared with the real native source.
// LGPL-2.1.
#pragma once
#include <type_traits>
#include <utility>
#include "llinventorymodel.h"
#include "llviewerinventory.h"
#include "llgltfmaterial.h"
#include "lltextureentry.h"
#include "llviewertexturelist.h"
#include "llinventorydefines.h"
template<class Action>bool vs_face_mutation(Action&& action)
{
    if constexpr(std::is_void_v<std::invoke_result_t<Action>>) {std::forward<Action>(action)();return true;}
    else return bool(std::forward<Action>(action)());
}
template<class Object,class Drop,class Current>
bool vs_apply_pbr_face(Object* objectp,S32 te,LLViewerInventoryItem* mItem,const LLUUID& mMatId,Drop&& drop,Current&& current)
{
            if (!objectp || !objectp->permModify())
            {
                return false;
            }
            LLUUID asset_id = mMatId;
            if (mItem)
            {
                const LLPermissions& perm = mItem->getPermissions();
                bool from_library = perm.getOwner() == ALEXANDRIA_LINDEN_ID;
                if (objectp->isAttachment())
                {
                    bool unrestricted = (perm.getMaskBase() & PERM_ITEM_UNRESTRICTED) == PERM_ITEM_UNRESTRICTED;

                    if (!unrestricted && !from_library)
                    {
                        // Attachments are in world and in inventory simultaneously,
                        // at the moment server doesn't support such a situation.
                        return false;
                    }
                }

                if (!from_library
                    // Check if item may be copied into the object's inventory
                    && !drop(objectp, mItem))
                {
                    return false;
                }

                asset_id = mItem->getAssetUUID();
                if (asset_id.isNull())
                {
                    asset_id = BLANK_MATERIAL_ASSET_ID;
                }
            }

            if(!current())return false;

            // If this face already has the target material ID, do nothing.
            // This prevents re-sending the same ID on OK, which can cause the server
            // to drop overrides when queueApply is invoked with the OLD id.
            if (objectp->getRenderMaterialID(te) == asset_id)
            {
                return true;
            }

            // Preserve existing texture transforms when switching to PBR material
            LLTextureEntry* tep = objectp->getTE(te);
            bool should_preserve_transforms = false;
            LLPointer<LLGLTFMaterial> preserved_override;

            if (tep && asset_id.notNull())
            {
                // Only preserve transforms from existing GLTF material override
                // Do not fall back to texture entry transforms when switching between PBR materials
                LLGLTFMaterial* existing_override = tep->getGLTFMaterialOverride();
                if (existing_override)
                {
                    // Check if existing override has non-default transforms
                    const LLGLTFMaterial::TextureTransform& existing_transform = existing_override->mTextureTransform[0];
                    const LLGLTFMaterial::TextureTransform& default_transform = LLGLTFMaterial::TextureTransform();

                    if (existing_transform.mScale != default_transform.mScale ||
                        existing_transform.mOffset != default_transform.mOffset ||
                        existing_transform.mRotation != default_transform.mRotation)
                    {
                        // Preserve non-default transforms from current PBR material
                        preserved_override = new LLGLTFMaterial();
                        for (U32 i = 0; i < LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT; ++i)
                        {
                            preserved_override->mTextureTransform[i].mScale = existing_transform.mScale;
                            preserved_override->mTextureTransform[i].mOffset = existing_transform.mOffset;
                            preserved_override->mTextureTransform[i].mRotation = existing_transform.mRotation;
                        }
                        should_preserve_transforms = true;
                    }
                    // If existing override has default transforms, don't preserve anything
                }
                else
                {
                    // No existing PBR material override - check texture entry transforms
                    // This handles the case of switching from Blinn-Phong to PBR material
                    F32 existing_scale_s, existing_scale_t, existing_offset_s, existing_offset_t, existing_rotation;
                    tep->getScale(&existing_scale_s, &existing_scale_t);
                    tep->getOffset(&existing_offset_s, &existing_offset_t);
                    existing_rotation = tep->getRotation();

                    const LLGLTFMaterial::TextureTransform& default_transform = LLGLTFMaterial::TextureTransform();
                    if (existing_scale_s != default_transform.mScale.mV[0] || existing_scale_t != default_transform.mScale.mV[1] ||
                        existing_offset_s != default_transform.mOffset.mV[0] || existing_offset_t != default_transform.mOffset.mV[1] ||
                        existing_rotation != default_transform.mRotation)
                    {
                        // Preserve non-default transforms from texture entry
                        preserved_override = new LLGLTFMaterial();
                        for (U32 i = 0; i < LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT; ++i)
                        {
                            LLVector2 pbr_scale, pbr_offset;
                            F32 pbr_rotation;
                            LLGLTFMaterial::convertTextureTransformToPBR(
                                existing_scale_s, existing_scale_t,
                                existing_offset_s, existing_offset_t,
                                existing_rotation,
                                pbr_scale, pbr_offset, pbr_rotation);
                            preserved_override->mTextureTransform[i].mScale = pbr_scale;
                            preserved_override->mTextureTransform[i].mOffset = pbr_offset;
                            preserved_override->mTextureTransform[i].mRotation = pbr_rotation;
                        }
                        should_preserve_transforms = true;
                    }
                }
            }

            if(!vs_face_mutation([&]{return objectp->clearTEWaterExclusion(te);}))return false;
            // Blank out most override data on the object and send to server
            if (should_preserve_transforms && preserved_override)
            {
                // Apply material with preserved transforms
                if(!vs_face_mutation([&]{return objectp->queueApply(te,asset_id,preserved_override);}))return false;
                // Update local state
                if(!vs_face_mutation([&]{return objectp->setRenderMaterialID(te,asset_id,false,true);}))return false;
                if(!vs_face_mutation([&]{return objectp->setGLTFMaterialOverride(te,preserved_override);}))return false;
            }
            else
            {
                if(!vs_face_mutation([&]{return objectp->setRenderMaterialID(te,asset_id);}))return false;
            }

            return true;
        }
