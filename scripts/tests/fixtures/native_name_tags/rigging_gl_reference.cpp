void LLSkinningUtil::updateRiggingInfo(const LLMeshSkinInfo* skin, LLVOAvatar *avatar, LLVolumeFace& vol_face)
{
    if (vol_face.mJointRiggingInfoTab.needsUpdate())
    {
        S32 num_verts = vol_face.mNumVertices;
        S32 num_joints = static_cast<S32>(skin->mJointNames.size());
        if (num_verts > 0 && vol_face.mWeights && num_joints > 0)
        {
            LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
            initJointNums(const_cast<LLMeshSkinInfo*>(skin), avatar);
            if (vol_face.mJointRiggingInfoTab.size()==0)
            {
                vol_face.mJointRiggingInfoTab.resize(LL_CHARACTER_MAX_ANIMATED_JOINTS);
                LLJointRiggingInfoTab &rig_info_tab = vol_face.mJointRiggingInfoTab;
                for (S32 i=0; i<vol_face.mNumVertices; i++)
                {
                    LLVector4a& pos = vol_face.mPositions[i];
                    F32 *weights = vol_face.mWeights[i].getF32ptr();
                    LLVector4 wght;
                    S32 idx[4];
                    F32 scale = 0.0f;
                    // FIXME unpacking of weights should be pulled into a common function and optimized if possible.
                    for (U32 k = 0; k < 4; k++)
                    {
                        F32 w = weights[k];
                        idx[k] = llclamp((S32) floorf(w), (S32)0, (S32)LL_CHARACTER_MAX_ANIMATED_JOINTS-1);
                        wght[k] = w - idx[k];
                    }

                    for (U32 k=0; k<4; ++k)
                    {
                        S32 joint_index = idx[k];
                        if (wght[k] > 0.2f && num_joints > joint_index)
                        {
                            S32 joint_num = skin->mJointNums[joint_index];
                            if (joint_num >= 0 && joint_num < LL_CHARACTER_MAX_ANIMATED_JOINTS)
                            {
                                rig_info_tab[joint_num].setIsRiggedTo(true);

                                size_t bind_poses_size = skin->mBindPoseMatrix.size();
                                const LLMatrix4a& mat = bind_poses_size > joint_index ? skin->mBindPoseMatrix[joint_index] : LLMatrix4a::identity();
                                LLVector4a pos_joint_space;

                                mat.affineTransform(pos, pos_joint_space);

                                LLVector4a *extents = rig_info_tab[joint_num].getRiggedExtents();
                                update_min_max(extents[0], extents[1], pos_joint_space);
                            }
                        }
                    }
                }
                vol_face.mJointRiggingInfoTab.setNeedsUpdate(false);
            }
        }
    }
}
