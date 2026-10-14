/**
 * @file llvotree.cpp
 * @brief LLVOTree class implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llvotree.h"
#include "vsvegetation.h"

#include "lldrawpooltree.h"

#include "llviewercontrol.h"
#include "lldir.h"
#include "llprimitive.h"
#include "lltree_common.h"
#include "llxmltree.h"
#include "material_codes.h"
#include "object_flags.h"

#include "llagentcamera.h"
#include "lldrawable.h"
#include "llface.h"
#include "llviewercamera.h"
#include "llviewertexturelist.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llvolumemgr.h"
#include "llvovolume.h"
#include "llworld.h"
#include "noise.h"
#include "pipeline.h"
#include "llspatialpartition.h"
#include "llnotificationsutil.h"
#include "raytrace.h"
#include "llglslshader.h"

extern LLPipeline gPipeline;

const S32 MAX_SLICES = 32;
const F32 LEAF_LEFT = 0.52f;
const F32 LEAF_RIGHT = 0.98f;
const F32 LEAF_TOP = 1.0f;
const F32 LEAF_BOTTOM = 0.52f;
const F32 LEAF_WIDTH = 1.f;

const S32 LLVOTree::sMAX_NUM_TREE_LOD_LEVELS = 4 ;

S32 LLVOTree::sLODVertexOffset[sMAX_NUM_TREE_LOD_LEVELS];
S32 LLVOTree::sLODVertexCount[sMAX_NUM_TREE_LOD_LEVELS];
S32 LLVOTree::sLODIndexOffset[sMAX_NUM_TREE_LOD_LEVELS];
S32 LLVOTree::sLODIndexCount[sMAX_NUM_TREE_LOD_LEVELS];
S32 LLVOTree::sLODSlices[sMAX_NUM_TREE_LOD_LEVELS] = {10, 5, 4, 3};
F32 LLVOTree::sLODAngles[sMAX_NUM_TREE_LOD_LEVELS] = {30.f, 20.f, 15.f, F_ALMOST_ZERO};

F32 LLVOTree::sTreeFactor = 1.f;

LLVOTree::SpeciesMap LLVOTree::sSpeciesTable;
S32 LLVOTree::sMaxTreeSpecies = 0;

// Tree variables and functions

LLVOTree::LLVOTree(const LLUUID &id, const LLPCode pcode, LLViewerRegion *regionp):
                        LLViewerObject(id, pcode, regionp)
{
    mSpecies = 0;
    mFrameCount = 0;
    mWind = mRegionp->mWind.getVelocity(getPositionRegion());
    mTrunkLOD = 0;

    // if assert triggers, idleUpdate() needs to be revised and adjusted to new LOD levels
    llassert(sMAX_NUM_TREE_LOD_LEVELS == LLVolumeLODGroup::NUM_LODS);
}


LLVOTree::~LLVOTree()
{
    if (mData)
    {
        delete[] mData;
        mData = NULL;
    }
}

//static
bool LLVOTree::isTreeRenderingStopped()
{
    return LLVOTree::sTreeFactor < LLVOTree::sLODAngles[sMAX_NUM_TREE_LOD_LEVELS - 1] ;
}

// static
void LLVOTree::initClass()
{
    std::string xml_filename = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS,"trees.xml");

    LLXmlTree tree_def_tree;

    if (!tree_def_tree.parseFile(xml_filename))
    {
        LL_ERRS() << "Failed to parse tree file. Please re-install the viewer!" << LL_ENDL;
    }

    LLXmlTreeNode* rootp = tree_def_tree.getRoot();

    for (LLXmlTreeNode* tree_def = rootp->getFirstChild();
        tree_def;
        tree_def = rootp->getNextChild())
        {
            if (!tree_def->hasName("tree"))
            {
                LL_WARNS() << "Invalid tree definition node " << tree_def->getName() << LL_ENDL;
                continue;
            }
            F32 F32_val;
            LLUUID id;
            S32 S32_val;

            // <FS:Ansariel> FIRE-7802: Grass and tree selection in build tool
            std::string name;

            bool success{ true };
            S32 species;
            static LLStdStringHandle species_id_string = LLXmlTree::addAttributeString("species_id");
            if (!tree_def->getFastAttributeS32(species_id_string, species))
            {
                LL_WARNS() << "No species id defined" << LL_ENDL;
                continue;
            }

            if (species < 0)
            {
                LL_WARNS() << "Invalid species id " << species << LL_ENDL;
                continue;
            }

            if (sSpeciesTable.count(species))
            {
                LL_WARNS() << "Tree species " << species << " already defined! Duplicate discarded." << LL_ENDL;
                continue;
            }

            TreeSpeciesData* newTree = new TreeSpeciesData();

            static LLStdStringHandle texture_id_string = LLXmlTree::addAttributeString("texture_id");
            success &= tree_def->getFastAttributeUUID(texture_id_string, id);
            newTree->mTextureID = id;

            static LLStdStringHandle droop_string = LLXmlTree::addAttributeString("droop");
            success &= tree_def->getFastAttributeF32(droop_string, F32_val);
            newTree->mDroop = F32_val;

            static LLStdStringHandle twist_string = LLXmlTree::addAttributeString("twist");
            success &= tree_def->getFastAttributeF32(twist_string, F32_val);
            newTree->mTwist = F32_val;

            static LLStdStringHandle branches_string = LLXmlTree::addAttributeString("branches");
            success &= tree_def->getFastAttributeF32(branches_string, F32_val);
            newTree->mBranches = F32_val;

            static LLStdStringHandle depth_string = LLXmlTree::addAttributeString("depth");
            success &= tree_def->getFastAttributeS32(depth_string, S32_val);
            newTree->mDepth = S32_val;

            static LLStdStringHandle scale_step_string = LLXmlTree::addAttributeString("scale_step");
            success &= tree_def->getFastAttributeF32(scale_step_string, F32_val);
            newTree->mScaleStep = F32_val;

            static LLStdStringHandle trunk_depth_string = LLXmlTree::addAttributeString("trunk_depth");
            success &= tree_def->getFastAttributeS32(trunk_depth_string, S32_val);
            newTree->mTrunkDepth = S32_val;

            static LLStdStringHandle branch_length_string = LLXmlTree::addAttributeString("branch_length");
            success &= tree_def->getFastAttributeF32(branch_length_string, F32_val);
            newTree->mBranchLength = F32_val;

            static LLStdStringHandle trunk_length_string = LLXmlTree::addAttributeString("trunk_length");
            success &= tree_def->getFastAttributeF32(trunk_length_string, F32_val);
            newTree->mTrunkLength = F32_val;

            static LLStdStringHandle leaf_scale_string = LLXmlTree::addAttributeString("leaf_scale");
            success &= tree_def->getFastAttributeF32(leaf_scale_string, F32_val);
            newTree->mLeafScale = F32_val;

            static LLStdStringHandle billboard_scale_string = LLXmlTree::addAttributeString("billboard_scale");
            success &= tree_def->getFastAttributeF32(billboard_scale_string, F32_val);
            newTree->mBillboardScale = F32_val;

            static LLStdStringHandle billboard_ratio_string = LLXmlTree::addAttributeString("billboard_ratio");
            success &= tree_def->getFastAttributeF32(billboard_ratio_string, F32_val);
            newTree->mBillboardRatio = F32_val;

            static LLStdStringHandle trunk_aspect_string = LLXmlTree::addAttributeString("trunk_aspect");
            success &= tree_def->getFastAttributeF32(trunk_aspect_string, F32_val);
            newTree->mTrunkAspect = F32_val;

            static LLStdStringHandle branch_aspect_string = LLXmlTree::addAttributeString("branch_aspect");
            success &= tree_def->getFastAttributeF32(branch_aspect_string, F32_val);
            newTree->mBranchAspect = F32_val;

            static LLStdStringHandle leaf_rotate_string = LLXmlTree::addAttributeString("leaf_rotate");
            success &= tree_def->getFastAttributeF32(leaf_rotate_string, F32_val);
            newTree->mRandomLeafRotate = F32_val;

            static LLStdStringHandle noise_mag_string = LLXmlTree::addAttributeString("noise_mag");
            success &= tree_def->getFastAttributeF32(noise_mag_string, F32_val);
            newTree->mNoiseMag = F32_val;

            static LLStdStringHandle noise_scale_string = LLXmlTree::addAttributeString("noise_scale");
            success &= tree_def->getFastAttributeF32(noise_scale_string, F32_val);
            newTree->mNoiseScale = F32_val;

            static LLStdStringHandle taper_string = LLXmlTree::addAttributeString("taper");
            success &= tree_def->getFastAttributeF32(taper_string, F32_val);
            newTree->mTaper = F32_val;

            static LLStdStringHandle repeat_z_string = LLXmlTree::addAttributeString("repeat_z");
            success &= tree_def->getFastAttributeF32(repeat_z_string, F32_val);
            newTree->mRepeatTrunkZ = F32_val;

            // <FS:Ansariel> FIRE-7802: Grass and tree selection in build tool
            static LLStdStringHandle name_string = LLXmlTree::addAttributeString("name"); // MC
            tree_def->getFastAttributeString(name_string, name);
            newTree->mName = name;
            // </FS:Ansariel>

            sSpeciesTable[species] = newTree;

            if (species >= sMaxTreeSpecies) sMaxTreeSpecies = species + 1;

            if (!success)
            {
                // <FS:Ansariel> FIRE-7802: Grass and tree selection in build tool
                //std::string name;
                //static LLStdStringHandle name_string = LLXmlTree::addAttributeString("name");
                //tree_def->getFastAttributeString(name_string, name);
                // </FS:Ansariel>
                LL_WARNS() << "Incomplete definition of tree " << name << LL_ENDL;
            }
        }

        bool have_all_trees {true};
        std::string err;

        for (S32 i=0;i<sMaxTreeSpecies;++i)
        {
            if (!sSpeciesTable.count(i))
            {
                err.append(llformat(" %d",i));
                have_all_trees = false;
            }
        }

        if (!have_all_trees)
        {
            LLSD args;
            args["SPECIES"] = err;
            LLNotificationsUtil::add("ErrorUndefinedTrees", args);
        }
};

//static
void LLVOTree::cleanupClass()
{
    std::for_each(sSpeciesTable.begin(), sSpeciesTable.end(), DeletePairedPointer());
    sSpeciesTable.clear();
}

U32 LLVOTree::processUpdateMessage(LLMessageSystem *mesgsys,
                                          void **user_data,
                                          U32 block_num, EObjectUpdateType update_type,
                                          LLDataPacker *dp)
{
    // Do base class updates...
    U32 retval = LLViewerObject::processUpdateMessage(mesgsys, user_data, block_num, update_type, dp);

    if (  (getVelocity().lengthSquared() > 0.f)
        ||(getAcceleration().lengthSquared() > 0.f)
        ||(getAngularVelocity().lengthSquared() > 0.f))
    {
        LL_INFOS() << "ACK! Moving tree!" << LL_ENDL;
        setVelocity(LLVector3::zero);
        setAcceleration(LLVector3::zero);
        setAngularVelocity(LLVector3::zero);
    }

    if (update_type == OUT_TERSE_IMPROVED)
    {
        // Nothing else needs to be done for the terse message.
        return retval;
    }

    //
    //  Load Instance-Specific data
    //
    if (mData)
    {
        mSpecies = ((U8 *)mData)[0];
    }

    if (!sSpeciesTable.count(mSpecies))
    {
        if (sSpeciesTable.size())
        {
            SpeciesMap::const_iterator it = sSpeciesTable.begin();
            mSpecies = (*it).first;
        }
    }

    //
    //  Load Species-Specific data
    //
    static const S32 MAX_TREE_TEXTURE_VIRTURE_SIZE_RESET_INTERVAL = 32 ; //frames.
    mTreeImagep = LLViewerTextureManager::getFetchedTexture(sSpeciesTable[mSpecies]->mTextureID, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
    mTreeImagep->setMaxVirtualSizeResetInterval(MAX_TREE_TEXTURE_VIRTURE_SIZE_RESET_INTERVAL); //allow to wait for at most 16 frames to reset virtual size.

    mBranchLength = sSpeciesTable[mSpecies]->mBranchLength;
    mTrunkLength = sSpeciesTable[mSpecies]->mTrunkLength;
    mLeafScale = sSpeciesTable[mSpecies]->mLeafScale;
    mDroop = sSpeciesTable[mSpecies]->mDroop;
    mTwist = sSpeciesTable[mSpecies]->mTwist;
    mBranches = sSpeciesTable[mSpecies]->mBranches;
    mDepth = sSpeciesTable[mSpecies]->mDepth;
    mScaleStep = sSpeciesTable[mSpecies]->mScaleStep;
    mTrunkDepth = sSpeciesTable[mSpecies]->mTrunkDepth;
    mBillboardScale = sSpeciesTable[mSpecies]->mBillboardScale;
    mBillboardRatio = sSpeciesTable[mSpecies]->mBillboardRatio;
    mTrunkAspect = sSpeciesTable[mSpecies]->mTrunkAspect;
    mBranchAspect = sSpeciesTable[mSpecies]->mBranchAspect;

    // position change not caused by us, etc.  make sure to rebuild.
    gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_ALL);

    return retval;
}

void LLVOTree::idleUpdate(LLAgent &agent, const F64 &time)
{
    if (mDead || !(gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_TREE)))
    {
        return;
    }

    S32 trunk_LOD = sMAX_NUM_TREE_LOD_LEVELS ; // disabled
    F32 app_angle = getAppAngle()*LLVOTree::sTreeFactor;
    F32 distance = mDrawable->mDistanceWRTCamera * LLVOVolume::sDistanceFactor * (F_PI / 3.f);
    F32 diameter = getScale().length(); // trees have very broken scale, but length rougtly outlines proper diameter
    F32 sz = mBillboardScale * mBillboardRatio * diameter;

    for (S32 j = 0; j < sMAX_NUM_TREE_LOD_LEVELS; j++)
    {
        if (app_angle > LLVOTree::sLODAngles[j])
        {
            trunk_LOD = j;
            break;
        }
    }

    F32 tan_angle = (LLVOTree::sTreeFactor * 64 * sz) / distance;
    S32 cur_detail = LLVolumeLODGroup::getDetailFromTan(ll_round(tan_angle, 0.01f)); // larger value, better quality

    // for trunk_LOD lower value means better quality, but both trunk_LOD and cur_detail have 4 levels
    trunk_LOD = llmax(trunk_LOD, LLVolumeLODGroup::NUM_LODS - cur_detail - 1);
    trunk_LOD = llmin(trunk_LOD, sMAX_NUM_TREE_LOD_LEVELS);

    if (mReferenceBuffer.isNull())
    {
        gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_ALL);
    }
    else if (trunk_LOD != mTrunkLOD)
    {
        gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_ALL);
    }
    else
    {
        // we're not animating but we may *still* need to
        // regenerate the mesh if we moved, since position
        // and rotation are baked into the mesh.
        // *TODO: I don't know what's so special about trees
        // that they don't get REBUILD_POSITION automatically
        // at a higher level.
        const LLVector3 &this_position = getPositionRegion();
        if (this_position != mLastPosition)
        {
            gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_POSITION);
            mLastPosition = this_position;
        }
        else
        {
            const LLQuaternion &this_rotation = getRotation();

            if (this_rotation != mLastRotation)
            {
                gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_POSITION);
                mLastRotation = this_rotation;
            }
        }
    }

    mTrunkLOD = trunk_LOD;
}

void LLVOTree::render(LLAgent &agent)
{
}


void LLVOTree::setPixelAreaAndAngle(LLAgent &agent)
{
    LLVector3 center = getPositionAgent();//center of tree.
    LLVector3 viewer_pos_agent = gAgentCamera.getCameraPositionAgent();
    LLVector3 lookAt = center - viewer_pos_agent;
    F32 dist = lookAt.normVec() ;
    F32 cos_angle_to_view_dir = lookAt * LLViewerCamera::getInstance()->getXAxis() ;
    F32 radius = getScale().length()*0.5f;
    F32 range = dist - radius;

    if (range < F_ALMOST_ZERO || isHUDAttachment())     // range == zero
    {
        mAppAngle = 180.f;
    }
    else
    {
        mAppAngle = (F32) atan2( getMaxScale(), range) * RAD_TO_DEG;
    }

    F32 max_scale = mBillboardScale * getMaxScale();
    F32 area = max_scale * (max_scale*mBillboardRatio);
    // Compute pixels per meter at the given range
    F32 pixels_per_meter = LLViewerCamera::getInstance()->getViewHeightInPixels() / (tan(LLViewerCamera::getInstance()->getView()) * dist);
    mPixelArea = pixels_per_meter * pixels_per_meter * area ;

    F32 importance = LLFace::calcImportanceToCamera(cos_angle_to_view_dir, dist) ;
    mPixelArea = LLFace::adjustPixelArea(importance, mPixelArea) ;
    if (mPixelArea > LLViewerCamera::getInstance()->getScreenPixelArea())
    {
        mAppAngle = 180.f;
    }

#if 0
    // mAppAngle is a bit of voodoo;
    // use the one calculated LLViewerObject::setPixelAreaAndAngle above
    // to avoid LOD miscalculations
    mAppAngle = (F32) atan2( max_scale, range) * RAD_TO_DEG;
#endif
}

void LLVOTree::updateTextures()
{
    if (mTreeImagep)
    {
        if (gPipeline.hasRenderDebugMask(LLPipeline::RENDER_DEBUG_TEXTURE_AREA))
        {
            setDebugText(llformat("%4.0f", (F32) sqrt(mPixelArea)));
        }
    }
}


LLDrawable* LLVOTree::createDrawable(LLPipeline *pipeline)
{
    pipeline->allocDrawable(this);
    mDrawable->setLit(false);

    mDrawable->setRenderType(LLPipeline::RENDER_TYPE_TREE);

    LLDrawPoolTree *poolp = (LLDrawPoolTree*) gPipeline.getPool(LLDrawPool::POOL_TREE, mTreeImagep);

    // Just a placeholder for an actual object...
    LLFace *facep = mDrawable->addFace(poolp, mTreeImagep);
    facep->setSize(1, 3);
    facep->setTexture(LLRender::DIFFUSE_MAP, mTreeImagep);
    updateRadius();

    return mDrawable;
}


// Yes, I know this is bad.  I'll clean this up soon. - djs 04/02/02
const S32 LEAF_INDICES = 24;
const S32 LEAF_VERTICES = 16;

bool LLVOTree::updateGeometry(LLDrawable* drawable)
{
    if (mTrunkLOD >= sMAX_NUM_TREE_LOD_LEVELS)
    {
        mReferenceBuffer = nullptr;
        if (auto* face = drawable->getFace(0))
            face->setVertexBuffer(nullptr);
        return true;
    }
    updateMesh();
    return true;
}
void LLVOTree::updateMesh()
{
    auto*      face    = mDrawable->getFace(0);
    const auto species = sSpeciesTable.find(mSpecies);
    if (!face || species == sSpeciesTable.end())
        return;
    VSTreeGeometry            producer(*species->second);
    const auto                mesh   = producer.mesh(getPositionRegion(), getRotation(), getScale(), mTrunkLOD, mTrunkBend);
    LLPointer<LLVertexBuffer> buffer = new LLVertexBuffer(LLDrawPoolTree::VERTEX_DATA_MASK);
    if (!buffer->allocateBuffer(U32(mesh.vertices.size()), U32(mesh.indices.size())))
        return;
    LLStrider<LLVector3> vertices, normals;
    LLStrider<LLVector2> uv;
    LLStrider<LLColor4U> colors;
    LLStrider<U16>       indices;
    buffer->getVertexStrider(vertices);
    buffer->getNormalStrider(normals);
    buffer->getTexCoord0Strider(uv);
    buffer->getColorStrider(colors);
    buffer->getIndexStrider(indices);
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        *vertices++ = mesh.vertices[i];
        *normals++  = mesh.normals[i];
        *uv++       = mesh.uv[i];
        *colors++   = mesh.colors[i];
    }
    for (const U16 index : mesh.indices)
        *indices++ = index;
    buffer->unmapBuffer();
    face->setSize(S32(mesh.vertices.size()), S32(mesh.indices.size()));
    face->setVertexBuffer(buffer);
    mReferenceBuffer   = buffer;
    face->mCenterAgent = getPositionAgent();
    face->mCenterLocal = face->mCenterAgent;
}

void LLVOTree::updateRadius()
{
    if (mDrawable.isNull())
    {
        return;
    }

    mDrawable->setRadius(32.0f);
}

void LLVOTree::updateSpatialExtents(LLVector4a& newMin, LLVector4a& newMax)
{
    F32 radius = getScale().length()*0.05f;
    LLVector3 center = getRenderPosition();

    F32 sz = mBillboardScale*mBillboardRatio*radius*0.5f;
    LLVector3 size(sz,sz,sz);

    center += LLVector3(0, 0, size.mV[2]) * getRotation();

    newMin.load3((center-size).mV);
    newMax.load3((center+size).mV);
    LLVector4a pos;
    pos.load3(center.mV);
    mDrawable->setPositionGroup(pos);

    if (mDrawable->getNumFaces() > 0)
    {
        LLFace* facep = mDrawable->getFace(0);
        if (facep)
        {
            facep->mExtents[0] = newMin;
            facep->mExtents[1] = newMax;
        }
    }
}

bool LLVOTree::lineSegmentIntersect(const LLVector4a& start, const LLVector4a& end, S32 face, bool pick_transparent, bool pick_rigged, bool pick_unselectable, S32 *face_hitp,
                                  LLVector4a* intersection,LLVector2* tex_coord, LLVector4a* normal, LLVector4a* tangent)
{

    if (!lineSegmentBoundingBox(start, end))
    {
        return false;
    }

    const LLVector4a* exta = mDrawable->getSpatialExtents();

    //VECTORIZE THIS
    LLVector3 ext[2];
    ext[0].set(exta[0].getF32ptr());
    ext[1].set(exta[1].getF32ptr());

    LLVector3 center = (ext[1]+ext[0])*0.5f;
    LLVector3 size = (ext[1]-ext[0]);

    LLQuaternion quat = getRotation();

    center -= LLVector3(0,0,size.magVec() * 0.25f)*quat;

    size.scaleVec(LLVector3(0.25f, 0.25f, 1.f));
    size.mV[0] = llmin(size.mV[0], 1.f);
    size.mV[1] = llmin(size.mV[1], 1.f);

    LLVector3 pos, norm;

    LLVector3 start3(start.getF32ptr());
    LLVector3 end3(end.getF32ptr());

    if (linesegment_tetrahedron(start3, end3, center, size, quat, pos, norm))
    {
        if (intersection)
        {
            intersection->load3(pos.mV);
        }

        if (normal)
        {
            normal->load3(norm.mV);
        }
        return true;
    }

    return false;
}

U32 LLVOTree::getPartitionType() const
{
    return LLViewerRegion::PARTITION_TREE;
}

LLTreePartition::LLTreePartition(LLViewerRegion* regionp)
: LLSpatialPartition(0, false, regionp)
{
    mDrawableType = LLPipeline::RENDER_TYPE_TREE;
    mPartitionType = LLViewerRegion::PARTITION_TREE;
    mSlopRatio = 0.f;
    mLODPeriod = 1;
}

