// Frozen original OpenGL/reference picking implementation; LGPL-2.1.
bool referenceAlpha(Face*face,bool pick_transparent){
int tc=0,p=0,n=0;
                bool ignore_alpha = false;

                const LLTextureEntry* te = face->getTextureEntry();
                if (te)
                {
                    LLMaterial* mat = te->getMaterialParams();
                    if (mat)
                    {
                        U8 mode = mat->getDiffuseAlphaMode();

                        if (mode == LLMaterial::DIFFUSE_ALPHA_MODE_EMISSIVE
                            || mode == LLMaterial::DIFFUSE_ALPHA_MODE_NONE
                            || (mode == LLMaterial::DIFFUSE_ALPHA_MODE_MASK && mat->getAlphaMaskCutoff() == 0))
                        {
                            ignore_alpha = true;
                        }
                    }
                }

                bool no_texture = !face->getTexture() || !face->getTexture()->hasGLTexture();
                bool mask       = no_texture ? false : face->getTexture()->getMask(face->surfaceToTexture(tc, p, n));
return face && (ignore_alpha || pick_transparent || no_texture || mask);
}
