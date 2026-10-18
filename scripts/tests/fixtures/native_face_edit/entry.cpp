#include <cassert>
#include <map>
#include <memory>
#include <functional>
#include <vector>
#include <string>
using U8=unsigned char;using U32=unsigned;using S32=int;enum{TEM_CHANGE_NONE=0,TEM_CHANGE_TEXTURE=1,LL_PCODE_VOLUME=9};
struct LLTextureEntry {float s=1,t=1,alpha=1;int id=0;};struct LLMessageSystem {std::string media;std::vector<LLTextureEntry> entries;void addString(const char* name,std::string value){assert(std::string(name)=="MediaURL");media=value;}};
struct LLPrimitive {std::vector<LLTextureEntry> entries;void setNumTEs(U8 n){entries.resize(n);}void setTE(U8 i,const LLTextureEntry& value){entries.at(i)=value;}bool packTEMessage(LLMessageSystem* message)const{message->entries=entries;return true;}};
struct VSWorldScene {struct ObjectHandle {unsigned host=1,local=1,epoch=1;};struct Face {LLTextureEntry material;};struct Impl{struct Object{unsigned pcode=LL_PCODE_VOLUME,revision=1;bool dirty=false;std::vector<Face> faces;std::string mediaVersion="x-mv:0001";};unsigned host=1,epoch=1,nextRevision=2;bool retired=false;std::map<unsigned,Object> objects;};std::shared_ptr<Impl> mImpl=std::make_shared<Impl>();std::map<unsigned,std::unique_ptr<VSWorldScene>> mNeighbours;bool current(ObjectHandle token)const{return !mImpl->retired&&token.epoch==mImpl->epoch&&(token.host==mImpl->host?mImpl->objects.count(token.local):mNeighbours.count(token.host));}S32 editTextureEntry(const ObjectHandle&,U32,const std::function<S32(LLTextureEntry&)>&);bool packTextureEntries(const ObjectHandle&,LLMessageSystem&)const;};
EDIT
PACK
int main(){VSWorldScene world;auto& source=world.mImpl->objects[1];source.faces={{{2,3,.7f,15}},{{4,5,.6f,16}}};VSWorldScene::ObjectHandle token;
 assert(world.editTextureEntry(token,0,[](auto& entry){entry.s=7;return TEM_CHANGE_TEXTURE;})==1);assert(source.faces[0].material.s==7&&source.revision==2&&source.dirty);assert(source.faces[1].material.s==4);
 assert(world.editTextureEntry(token,1,[&](auto& entry){entry.alpha=.1f;++source.revision;return 1;})==0&&source.faces[1].material.alpha==.6f);
 assert(world.editTextureEntry(token,1,[](auto& entry){entry.s=9;return 0;})==0&&source.faces[1].material.s==4);
 assert(world.editTextureEntry(token,99,[](auto&){assert(false);return 1;})==0);
 LLMessageSystem message;assert(world.packTextureEntries(token,message)&&message.media==source.mediaVersion&&message.entries.size()==2);assert(message.entries[0].s==7&&message.entries[1].id==16);
 assert(world.editTextureEntry(token,0,[&](auto& entry){entry.id=333;world.mImpl->retired=true;return 1;})==0);assert(source.faces[0].material.id==15);LLMessageSystem retired;assert(!world.packTextureEntries(token,retired)&&retired.entries.empty());
 world.mImpl->retired=false;source.pcode=0;assert(!world.packTextureEntries(token,message));assert(world.editTextureEntry(token,0,[](auto&){assert(false);return 1;})==0);}
