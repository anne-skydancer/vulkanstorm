// Frozen native render branch immediately before common cost extraction.
VSWorldContracts::AlphaMode original(const LLTextureEntry& entry,const std::shared_ptr<VSWorldContracts::Image>& image,
 VSWorldContracts::AlphaMode alpha,bool explicitAlpha,bool hud,bool riggedOwner)
{
 struct Face{const LLTextureEntry&material;}face{entry};
 struct Surface{VSWorldContracts::AlphaMode alpha;}surface{alpha};
            if(!explicitAlpha && image)
            {
                const auto description=VSUIRenderer::imageDescription(image);
                if(surface.alpha==VSWorldContracts::AlphaMode::Blend || description.alpha!=VSWorldContracts::AlphaMode::Opaque)
                {
                    struct FaceAlpha
                    {
                        const LLTextureEntry& entry;const VSWorldContracts::Image& image;bool isHud,isRigged;
                        bool present()const{return true;}
                        bool pbr()const{return false;}
                        bool noAlpha()const{return LLPipeline::sNoAlpha;}
                        bool rigged()const{return isRigged;}
                        bool legacyBlend()const{return false;}
                        F32 alpha()const{return entry.getColor().mV[3];}
                        F32 glow()const{return entry.getGlow();}
                        bool hud()const{return isHud;}
                        bool imageMask()const{return image.alphaMask;}
                        bool fullbright()const{return entry.getFullbright();}
                        bool autoMaskNonDeferred()const
                        {static LLCachedControl<bool> value(gSavedSettings,"RenderAutoMaskAlphaNonDeferred");return value;}
                        bool autoMaskDeferred()const
                        {static LLCachedControl<bool> value(gSavedSettings,"RenderAutoMaskAlphaDeferred");return value;}
                    };
                    surface.alpha=vs_face_can_render_as_mask(FaceAlpha{face.material,description,hud,bool(riggedOwner)})?
                        VSWorldContracts::AlphaMode::Mask:VSWorldContracts::AlphaMode::Blend;
                }
            }
    return surface.alpha;
}
