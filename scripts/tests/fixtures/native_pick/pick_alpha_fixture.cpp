
#include <cassert>
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstddef>
using U8=unsigned char;
struct LLMaterial{enum{DIFFUSE_ALPHA_MODE_NONE,DIFFUSE_ALPHA_MODE_BLEND,DIFFUSE_ALPHA_MODE_MASK,DIFFUSE_ALPHA_MODE_EMISSIVE};unsigned mode=0,cutoff=0;unsigned getDiffuseAlphaMode()const{return mode;}unsigned getAlphaMaskCutoff()const{return cutoff;}};
struct LLTextureEntry{LLMaterial*material=nullptr;LLMaterial*getMaterialParams()const{return material;}};
struct Texture{bool uploaded=true,value=true;unsigned calls=0;bool hasGLTexture()const{return uploaded;}bool getMask(int){++calls;return value;}};
struct Face{LLTextureEntry te;bool present=true;Texture*texture=nullptr;const LLTextureEntry*getTextureEntry(){return present?&te:nullptr;}Texture*getTexture(){return texture;}int surfaceToTexture(int,int,int){return 0;}};
REFERENCE
POLICY
struct Adapter{Face&face;bool present()const{return face.present;}bool hasLegacyMaterial()const{return face.te.material!=nullptr;}unsigned legacyMode()const{return face.te.material->getDiffuseAlphaMode();}unsigned cutoff()const{return face.te.material->getAlphaMaskCutoff();}unsigned emissiveMode()const{return LLMaterial::DIFFUSE_ALPHA_MODE_EMISSIVE;}unsigned noneMode()const{return LLMaterial::DIFFUSE_ALPHA_MODE_NONE;}unsigned maskMode()const{return LLMaterial::DIFFUSE_ALPHA_MODE_MASK;}bool hasTexture()const{return face.texture;}bool uploaded()const{return face.texture->uploaded;}bool mask()const{return face.texture->getMask(0);}};
int main(){for(unsigned f=0;f<512;f++){LLMaterial material;material.mode=f%4;material.cutoff=f&4?128:0;Texture texture;texture.uploaded=f&8;texture.value=f&16;Face face;face.present=f&32;face.te.material=f&64?&material:nullptr;face.texture=f&128?&texture:nullptr;bool transparent=f&256;bool expected=referenceAlpha(&face,transparent);unsigned calls=texture.calls;texture.calls=0;bool actual=VSPickPolicy::acceptsHit(Adapter{face},transparent);assert(expected==actual&&calls==texture.calls);}}
