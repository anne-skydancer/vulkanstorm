// Frozen original shared material override controller, LGPL-2.1.
class LLRenderMaterialOverrideFunctor : public LLSelectedNodeFunctor
{
public:
    LLRenderMaterialOverrideFunctor(
        LLMaterialEditor * me,
        const LLUUID &report_on_object_id,
        S32 report_on_te)
    : mEditor(me)
    , mSuccess(false)
    , mObjectId(report_on_object_id)
    , mObjectTE(report_on_te)
    {
    }

    virtual bool apply(LLSelectNode* nodep) override
    {
        LLViewerObject* objectp = nodep->getObject();
        if (!objectp || !objectp->permModify() || !objectp->getVolume())
        {
            return false;
        }
        S32 num_tes = llmin((S32)objectp->getNumTEs(), (S32)objectp->getNumFaces()); // avatars have TEs but no faces

        // post override from given object and te to the simulator
        // requestData should have:
        //  object_id - UUID of LLViewerObject
        //  side - S32 index of texture entry
        //  gltf_json - String of GLTF json for override data

        for (S32 te = 0; te < num_tes; ++te)
        {
            if (!nodep->isTESelected(te))
            {
                continue;
            }

            // Get material from object
            // Selection can cover multiple objects, and live editor is
            // supposed to overwrite changed values only
            LLTextureEntry* tep = objectp->getTE(te);

            if (tep->getGLTFMaterial() == nullptr)
            {
                // overrides are not supposed to work or apply if
                // there is no base material to work from
                continue;
            }

            LLPointer<LLGLTFMaterial> material = tep->getGLTFMaterialOverride();
            // make a copy to not invalidate existing
            // material for multiple objects
            if (material.isNull())
            {
                // Start with a material override which does not make any changes
                material = new LLGLTFMaterial();
            }
            else
            {
                material = new LLGLTFMaterial(*material);
            }

            U32 changed_flags = mEditor->getUnsavedChangesFlags();
            U32 reverted_flags = mEditor->getRevertedChangesFlags();

            LLPointer<LLGLTFMaterial> revert_mat;
            if (nodep->mSavedGLTFOverrideMaterials.size() > te)
            {
                if (nodep->mSavedGLTFOverrideMaterials[te].notNull())
                {
                    revert_mat = nodep->mSavedGLTFOverrideMaterials[te];
                }
                else
                {
                    // mSavedGLTFOverrideMaterials[te] being present but null
                    // means we need to use a default value
                    revert_mat = new LLGLTFMaterial();
                }
            }
            // else can not revert at all

            // Override object's values with values from editor where appropriate
            if (changed_flags & MATERIAL_BASE_COLOR_DIRTY)
            {
                material->setBaseColorFactor(mEditor->getBaseColor(), true);
            }
            else if ((reverted_flags & MATERIAL_BASE_COLOR_DIRTY) && revert_mat.notNull())
            {
                material->setBaseColorFactor(revert_mat->mBaseColor, false);
            }

            if (changed_flags & MATERIAL_BASE_COLOR_TEX_DIRTY)
            {
                material->setBaseColorId(mEditor->getBaseColorId(), true);
                LLUUID tracking_id = mEditor->getLocalTextureTrackingIdFromFlag(MATERIAL_BASE_COLOR_TEX_DIRTY);
                if (tracking_id.notNull())
                {
                    LLLocalBitmapMgr::getInstance()->associateGLTFMaterial(tracking_id, material);
                }
            }
            else if ((reverted_flags & MATERIAL_BASE_COLOR_TEX_DIRTY) && revert_mat.notNull())
            {
                material->setBaseColorId(revert_mat->mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR], false);
                LLUUID tracking_id = mEditor->getLocalTextureTrackingIdFromFlag(MATERIAL_BASE_COLOR_TEX_DIRTY);
                if (tracking_id.notNull())
                {
                    LLLocalBitmapMgr::getInstance()->associateGLTFMaterial(tracking_id, material);
                }
            }

            if (changed_flags & MATERIAL_NORMAL_TEX_DIRTY)
            {
                material->setNormalId(mEditor->getNormalId(), true);
                LLUUID tracking_id = mEditor->getLocalTextureTrackingIdFromFlag(MATERIAL_NORMAL_TEX_DIRTY);
                if (tracking_id.notNull())
                {
                    LLLocalBitmapMgr::getInstance()->associateGLTFMaterial(tracking_id, material);
                }
            }
            else if ((reverted_flags & MATERIAL_NORMAL_TEX_DIRTY) && revert_mat.notNull())
            {
                material->setNormalId(revert_mat->mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_NORMAL], false);
                LLUUID tracking_id = mEditor->getLocalTextureTrackingIdFromFlag(MATERIAL_NORMAL_TEX_DIRTY);
                if (tracking_id.notNull())
                {
                    LLLocalBitmapMgr::getInstance()->associateGLTFMaterial(tracking_id, material);
                }
            }

            if (changed_flags & MATERIAL_METALLIC_ROUGHTNESS_TEX_DIRTY)
            {
                material->setOcclusionRoughnessMetallicId(mEditor->getMetallicRoughnessId(), true);
                LLUUID tracking_id = mEditor->getLocalTextureTrackingIdFromFlag(MATERIAL_METALLIC_ROUGHTNESS_TEX_DIRTY);
                if (tracking_id.notNull())
                {
                    LLLocalBitmapMgr::getInstance()->associateGLTFMaterial(tracking_id, material);
                }
            }
            else if ((reverted_flags & MATERIAL_METALLIC_ROUGHTNESS_TEX_DIRTY) && revert_mat.notNull())
            {
                material->setOcclusionRoughnessMetallicId(revert_mat->mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS], false);
                LLUUID tracking_id = mEditor->getLocalTextureTrackingIdFromFlag(MATERIAL_METALLIC_ROUGHTNESS_TEX_DIRTY);
                if (tracking_id.notNull())
                {
                    LLLocalBitmapMgr::getInstance()->associateGLTFMaterial(tracking_id, material);
                }
            }

            if (changed_flags & MATERIAL_METALLIC_ROUGHTNESS_METALNESS_DIRTY)
            {
                material->setMetallicFactor(mEditor->getMetalnessFactor(), true);
            }
            else if ((reverted_flags & MATERIAL_METALLIC_ROUGHTNESS_METALNESS_DIRTY) && revert_mat.notNull())
            {
                material->setMetallicFactor(revert_mat->mMetallicFactor, false);
            }

            if (changed_flags & MATERIAL_METALLIC_ROUGHTNESS_ROUGHNESS_DIRTY)
            {
                material->setRoughnessFactor(mEditor->getRoughnessFactor(), true);
            }
            else if ((reverted_flags & MATERIAL_METALLIC_ROUGHTNESS_ROUGHNESS_DIRTY) && revert_mat.notNull())
            {
                material->setRoughnessFactor(revert_mat->mRoughnessFactor, false);
            }

            if (changed_flags & MATERIAL_EMISIVE_COLOR_DIRTY)
            {
                material->setEmissiveColorFactor(LLColor3(mEditor->getEmissiveColor()), true);
            }
            else if ((reverted_flags & MATERIAL_EMISIVE_COLOR_DIRTY) && revert_mat.notNull())
            {
                material->setEmissiveColorFactor(revert_mat->mEmissiveColor, false);
            }

            if (changed_flags & MATERIAL_EMISIVE_TEX_DIRTY)
            {
                material->setEmissiveId(mEditor->getEmissiveId(), true);
                LLUUID tracking_id = mEditor->getLocalTextureTrackingIdFromFlag(MATERIAL_EMISIVE_TEX_DIRTY);
                if (tracking_id.notNull())
                {
                    LLLocalBitmapMgr::getInstance()->associateGLTFMaterial(tracking_id, material);
                }
            }
            else if ((reverted_flags & MATERIAL_EMISIVE_TEX_DIRTY) && revert_mat.notNull())
            {
                material->setEmissiveId(revert_mat->mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_EMISSIVE], false);
                LLUUID tracking_id = mEditor->getLocalTextureTrackingIdFromFlag(MATERIAL_EMISIVE_TEX_DIRTY);
                if (tracking_id.notNull())
                {
                    LLLocalBitmapMgr::getInstance()->associateGLTFMaterial(tracking_id, material);
                }
            }

            if (changed_flags & MATERIAL_DOUBLE_SIDED_DIRTY)
            {
                material->setDoubleSided(mEditor->getDoubleSided(), true);
            }
            else if ((reverted_flags & MATERIAL_DOUBLE_SIDED_DIRTY) && revert_mat.notNull())
            {
                material->setDoubleSided(revert_mat->mDoubleSided, false);
            }

            if (changed_flags & MATERIAL_ALPHA_MODE_DIRTY)
            {
                material->setAlphaMode(mEditor->getAlphaMode(), true);
            }
            else if ((reverted_flags & MATERIAL_ALPHA_MODE_DIRTY) && revert_mat.notNull())
            {
                material->setAlphaMode(revert_mat->mAlphaMode, false);
            }

            if (changed_flags & MATERIAL_ALPHA_CUTOFF_DIRTY)
            {
                material->setAlphaCutoff(mEditor->getAlphaCutoff(), true);
            }
            else if ((reverted_flags & MATERIAL_ALPHA_CUTOFF_DIRTY) && revert_mat.notNull())
            {
                material->setAlphaCutoff(revert_mat->mAlphaCutoff, false);
            }

            if (mObjectTE == te
                && mObjectId == objectp->getID())
            {
                mSuccess = true;
            }
            LLGLTFMaterialList::queueModify(objectp, te, material);
        }
        return true;
    }

    static void modifyCallback(bool success)
    {
        if (!success)
        {
            // something went wrong update selection
            LLMaterialEditor::updateLive();
        }
        // else we will get updateLive() from panel face
    }

    bool getResult() { return mSuccess; }

private:
    LLMaterialEditor * mEditor;
    LLUUID mObjectId;
    S32 mObjectTE;
    bool mSuccess;
};
