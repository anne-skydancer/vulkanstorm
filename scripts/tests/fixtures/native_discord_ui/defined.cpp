#include <cassert>
#include <array>
using U32=unsigned;using U8=unsigned char;using ETextureIndex=int;namespace LLAvatarAppearanceDefines{using ETextureIndex=int;}namespace LLWearableType{using EType=int;}
struct LLUUID{int id=0;bool notNull()const{return id!=0;}bool operator!=(const LLUUID&o)const{return id!=o.id;}};const LLUUID IMG_DEFAULT{1},IMG_DEFAULT_AVATAR{2};
struct Entry{LLUUID id;const LLUUID&getID()const{return id;}};
struct Dict{int getTEWearableType(int){return 0;}}dictionary,*sAvatarDictionary=&dictionary;struct Wearables{unsigned getWearableCount(int){return 1;}}gAgentWearables;
struct LLVOAvatarSelf{Entry texture;bool isIndexLocalTexture(int)const{return false;}LLUUID getLocalTextureID(int,unsigned)const{return texture.id;}const Entry*getTEImage(int)const{return &texture;}bool isTextureDefined(int,unsigned)const;};
ORIGINAL
struct Base{virtual bool isTextureDefined(int,unsigned)const=0;};struct Native:Base{struct Textures{std::array<Entry,1>entry;unsigned getNumTEs()const{return 1;}const Entry*getTE(U8 i)const{return &entry[i];}}textures;
NATIVE
};
int main(){for(int id=1;id<20;++id){LLVOAvatarSelf original;Native native;original.texture.id={id};native.textures.entry[0].id={id};assert(original.isTextureDefined(0,0)==native.isTextureDefined(0,0));}Native native;assert(!native.isTextureDefined(1,0));assert(!native.isTextureDefined(-1,0));}
