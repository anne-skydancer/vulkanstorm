// Original texture-scale snapshots/center-pivot policy for genuine Build peers. LGPL-2.1.
#pragma once
#include "llrender.h"
#include "lltextureentry.h"
#include "llprimitive.h"
#include "llgltfmaterial.h"
#include "llfetchedgltfmaterial.h"
#include "v2math.h"
#include "v3math.h"
#include <memory>
#include <vector>
#include <cmath>

struct VSTextureScaleRatios
{
    std::vector<LLVector3> textureScaleRatios;
    std::vector<std::vector<LLVector3>> gltfScaleRatios;
    std::vector<std::vector<LLVector2>> gltfScales,gltfOffsets;
    std::shared_ptr<const void> residency;
};

template<class Object>
void vs_capture_texture_scale_ratios(Object* object,VSTextureScaleRatios& ratios)
{
    ratios.textureScaleRatios.clear();
    ratios.gltfScaleRatios.clear();
    ratios.gltfScales.clear();
    ratios.gltfOffsets.clear();

    if (object!=nullptr)
    {
        LLVector3 scale = object->getScale();

        for (U8 i = 0; i < object->getNumTEs(); i++)
        {
            F32 diffuse_s = 1.0f;
            F32 diffuse_t = 1.0f;

            LLVector3 v;
            const LLTextureEntry* tep = object->getTE(i);
            if (!tep)
                continue;

            U32 s_axis = VX;
            U32 t_axis = VY;
            LLPrimitive::getTESTAxes(i, &s_axis, &t_axis);

            tep->getScale(&diffuse_s,&diffuse_t);

            if (tep->getTexGen() == LLTextureEntry::TEX_GEN_PLANAR)
            {
                v.mV[s_axis] = diffuse_s*scale.mV[s_axis];
                v.mV[t_axis] = diffuse_t*scale.mV[t_axis];
                ratios.textureScaleRatios.push_back(v);
            }
            else
            {
                v.mV[s_axis] = diffuse_s/scale.mV[s_axis];
                v.mV[t_axis] = diffuse_t/scale.mV[t_axis];
                ratios.textureScaleRatios.push_back(v);
            }

            LLGLTFMaterial* material = tep->getGLTFMaterialOverride();
            LLVector3 material_v;
            F32 scale_x = 1;
            F32 scale_y = 1;
            std::vector<LLVector3> material_v_vec;
            std::vector<LLVector2> material_scales_vec;
            std::vector<LLVector2> material_offset_vec;
            for (U32 i = 0; i < LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT; ++i)
            {
                if (material)
                {
                    LLGLTFMaterial::TextureTransform& transform = material->mTextureTransform[i];
                    scale_x = transform.mScale[VX];
                    scale_y = transform.mScale[VY];
                    material_scales_vec.push_back(transform.mScale);
                    material_offset_vec.push_back(transform.mOffset);
                }
                else
                {
                    // Not having an override doesn't mean that there is no material
                    scale_x = 1;
                    scale_y = 1;
                    material_scales_vec.emplace_back(scale_x, scale_y);
                    material_offset_vec.emplace_back(0.f, 0.f);
                }

                if (tep->getTexGen() == LLTextureEntry::TEX_GEN_PLANAR)
                {
                    material_v.mV[s_axis] = scale_x * scale.mV[s_axis];
                    material_v.mV[t_axis] = scale_y * scale.mV[t_axis];
                }
                else
                {
                    material_v.mV[s_axis] = scale_x / scale.mV[s_axis];
                    material_v.mV[t_axis] = scale_y / scale.mV[t_axis];
                }
                material_v_vec.push_back(material_v);
            }
            ratios.gltfScaleRatios.push_back(material_v_vec);
            ratios.gltfScales.push_back(material_scales_vec);
            ratios.gltfOffsets.push_back(material_offset_vec);
        }
    }
}

template<class Object,class CreateDefaultMaterial,class PutMaterial,class QueueModify>
void vs_adjust_texture_scale(Object* object,const VSTextureScaleRatios& ratios,bool send_to_sim,bool stretch,
                             CreateDefaultMaterial createDefaultMaterial,PutMaterial putMaterial,QueueModify queueModify)
{
        if (!object)
        {
            return;
        }

        if (!object->permModify())
        {
            return;
        }

        if (object->getNumTEs() == 0)
        {
            return;
        }

        bool send = false;

        for (U8 te_num = 0; te_num < object->getNumTEs(); te_num++)
        {
            LLTextureEntry* tep = object->getTE(te_num);

            // ND: Down the code there is a check if getTE did return a valid pointer (actually the pointer is fetched again there, can it change mid loop?)
            if( !tep )
                continue;

            bool planar = tep->getTexGen() == LLTextureEntry::TEX_GEN_PLANAR;
            if (planar == stretch)
            {
                // Figure out how S,T changed with scale operation
                U32 s_axis, t_axis;
                if (!LLPrimitive::getTESTAxes(te_num, &s_axis, &t_axis))
                {
                    continue;
                }

                LLVector3 object_scale = object->getScale();
                LLVector3 diffuse_scale_ratio  = ratios.textureScaleRatios[te_num];

                // We like these to track together. NORSPEC-96
                //
                LLVector3 normal_scale_ratio   = diffuse_scale_ratio;
                LLVector3 specular_scale_ratio = diffuse_scale_ratio;

                // Apply new scale to face
                if (planar)
                {
                    F32 diffuse_scale_s = diffuse_scale_ratio.mV[s_axis]/object_scale.mV[s_axis];
                    F32 diffuse_scale_t = diffuse_scale_ratio.mV[t_axis]/object_scale.mV[t_axis];

                    F32 normal_scale_s = normal_scale_ratio.mV[s_axis]/object_scale.mV[s_axis];
                    F32 normal_scale_t = normal_scale_ratio.mV[t_axis]/object_scale.mV[t_axis];

                    F32 specular_scale_s = specular_scale_ratio.mV[s_axis]/object_scale.mV[s_axis];
                    F32 specular_scale_t = specular_scale_ratio.mV[t_axis]/object_scale.mV[t_axis];

                    object->setTEScale(te_num, diffuse_scale_s, diffuse_scale_t);

                    if (tep && !tep->getMaterialParams().isNull())
                    {
                        LLMaterialPtr orig = tep->getMaterialParams();
                        // <FS:Zi> switchable edit texture/materials panel
                        // LLMaterialPtr p = gFloaterTools->getPanelFace()->createDefaultMaterial(orig);
                        LLMaterialPtr p = createDefaultMaterial(orig);
                        p->setNormalRepeat(normal_scale_s, normal_scale_t);
                        p->setSpecularRepeat(specular_scale_s, specular_scale_t);

                        putMaterial(object, te_num, *p);
                    }
                }
                else
                {

                    F32 diffuse_scale_s = diffuse_scale_ratio.mV[s_axis]*object_scale.mV[s_axis];
                    F32 diffuse_scale_t = diffuse_scale_ratio.mV[t_axis]*object_scale.mV[t_axis];

                    F32 normal_scale_s = normal_scale_ratio.mV[s_axis]*object_scale.mV[s_axis];
                    F32 normal_scale_t = normal_scale_ratio.mV[t_axis]*object_scale.mV[t_axis];

                    F32 specular_scale_s = specular_scale_ratio.mV[s_axis]*object_scale.mV[s_axis];
                    F32 specular_scale_t = specular_scale_ratio.mV[t_axis]*object_scale.mV[t_axis];

                    object->setTEScale(te_num, diffuse_scale_s,diffuse_scale_t);

                    LLTextureEntry* tep = object->getTE(te_num);

                    if (tep && !tep->getMaterialParams().isNull())
                    {
                        LLMaterialPtr orig = tep->getMaterialParams();
                        // <FS:Zi> switchable edit texture/materials panel
                        // LLMaterialPtr p = gFloaterTools->getPanelFace()->createDefaultMaterial(orig);
                        LLMaterialPtr p = createDefaultMaterial(orig);

                        p->setNormalRepeat(normal_scale_s, normal_scale_t);
                        p->setSpecularRepeat(specular_scale_s, specular_scale_t);

                        putMaterial(object, te_num, *p);
                    }
                }

                if (tep->getGLTFMaterial())
                {
                    LLPointer<LLGLTFMaterial> material = tep->getGLTFMaterialOverride();
                    if (!material)
                    {
                        material = new LLGLTFMaterial();
                        tep->setGLTFMaterialOverride(material);
                    }

                    F32 scale_x = 1;
                    F32 scale_y = 1;
                    F32 offset_x = 0;
                    F32 offset_y = 0;

                    if (te_num < ratios.gltfScaleRatios.size())
                    {
                        for (U32 i = 0; i < LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT; ++i)
                        {
                            LLVector3 scale_ratio = ratios.gltfScaleRatios[te_num][i];

                            if (planar)
                            {
                                scale_x = scale_ratio.mV[s_axis] / object_scale.mV[s_axis];
                                scale_y = scale_ratio.mV[t_axis] / object_scale.mV[t_axis];
                            }
                            else
                            {
                                scale_x = scale_ratio.mV[s_axis] * object_scale.mV[s_axis];
                                scale_y = scale_ratio.mV[t_axis] * object_scale.mV[t_axis];
                            }
                            material->mTextureTransform[i].mScale.set(scale_x, scale_y);

                            LLVector2 scales = ratios.gltfScales[te_num][i];
                            LLVector2 offsets = ratios.gltfOffsets[te_num][i];
                            F64 int_part = 0;
                            offset_x = (F32)modf((offsets[VX] + (scales[VX] - scale_x)) / 2, &int_part);
                            if (offset_x < 0)
                            {
                                offset_x++;
                            }
                            offset_y = (F32)modf((offsets[VY] + (scales[VY] - scale_y)) / 2, &int_part);
                            if (offset_y < 0)
                            {
                                offset_y++;
                            }
                            material->mTextureTransform[i].mOffset.set(offset_x, offset_y);
                        }
                    }
                    else
                    {
                        llassert(false); // make sure mGLTFScaleRatios is filled
                    }

                    const LLGLTFMaterial* base_material = tep->getGLTFMaterial();
                    if (base_material)
                    {
                        LLGLTFMaterial* render_material = new LLFetchedGLTFMaterial();
                        *render_material = *base_material;
                        render_material->applyOverride(*material);
                        tep->setGLTFRenderMaterial(render_material);
                    }

                    if (send_to_sim)
                    {
                        LLGLTFMaterial new_override;
                        new_override = *material;
                        queueModify(object, te_num, &new_override);
                    }
                }
                send = send_to_sim;
            }
        }

        if (send)
        {
            object->sendTEUpdate();
        }
}
