#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>
using U8=uint8_t;using U32=uint32_t;using U64=uint64_t;using S16=int16_t;using S32=int32_t;
constexpr S32 UUID_BYTES=16;
struct LLUUID{U8 mData[UUID_BYTES]{};bool operator==(const LLUUID&)const=default;};
using uuid_vec_t=std::vector<LLUUID>;
class LLMessageSystem;
struct LLCategory{S32 block=-1;void unpackMultiMessage(LLMessageSystem*,const char*,S32 b){block=b;}void unpackMessage(LLMessageSystem*,const char*){block=0;}};
struct LLSaleInfo:LLCategory{};
struct LLAggregatePermissions{std::string field;S32 block=-1;void unpackMessage(LLMessageSystem*,const char*,const char* f,S32 b){field=f;block=b;}};
struct LLPermissions{std::array<LLUUID,4> identities;std::array<U32,5> masks{};
 void init(LLUUID c,LLUUID o,LLUUID l,LLUUID g){identities={c,o,l,g};}
 void initMasks(U32 b,U32 o,U32 e,U32 g,U32 n){masks={b,o,e,g,n};}};
PREHASH
class LLMessageSystem{
public:
 S32 packedSize=32;bool binaryRead=false;
 static U8 token(const char* field,S32 block){U32 result=block;for(const char* p=field;*p;++p)result=result*33+U8(*p);return U8(result);}
 void getUUIDFast(const char*,const char* f,LLUUID& result,S32 b=0){for(S32 n=0;n<16;++n)result.mData[n]=token(f,b)+n;}
 void getU64Fast(const char*,const char* f,U64& value,S32 b=0){value=U64(token(f,b))<<40;}
 void getU32Fast(const char*,const char* f,U32& value,S32 b=0){value=U32(token(f,b))<<20;}
 void getS32Fast(const char*,const char* f,S32& value,S32 b=0){value=-S32(token(f,b));}
 void getS16Fast(const char*,const char* f,S16& value,S32 b=0){value=-S16(token(f,b));}
 void getStringFast(const char*,const char* f,std::string& value,S32 b=0){value=std::string(f)+":"+std::to_string(b);}
 S32 getSizeFast(const char*,S32,const char*){return packedSize;}
 void getBinaryDataFast(const char*,const char*,void* data,S32,S32 block,S32 capacity){binaryRead=true;assert(capacity>=packedSize);for(S32 n=0;n<packedSize;++n)static_cast<U8*>(data)[n]=U8(n+block);}
};
HEADER
DEFINITIONS
int main(){
 LLMessageSystem message;
 for(S32 block:{0,1,7}){
  const auto value=vs_read_object_properties(message,block);assert(value && value->complete);
  const auto& p=*value;
  for(auto entry:{std::pair{&p.id,"ObjectID"},std::pair{&p.creator_id,"CreatorID"},std::pair{&p.owner_id,"OwnerID"},std::pair{&p.group_id,"GroupID"},std::pair{&p.last_owner_id,"LastOwnerID"},std::pair{&p.item_id,"ItemID"},std::pair{&p.folder_id,"FolderID"},std::pair{&p.from_task_id,"FromTaskID"}})
   for(S32 n=0;n<16;++n)assert(entry.first->mData[n]==U8(message.token(entry.second,block)+n));
  assert(p.creation_date==(U64(message.token("CreationDate",block))<<40));
  assert(p.ownership_cost==-S32(message.token("OwnershipCost",block)));
  assert(p.inv_serial==-S16(message.token("InventorySerial",block)));
  assert(p.name=="Name:"+std::to_string(block) && p.desc=="Description:"+std::to_string(block));
  assert(p.touch_name=="TouchName:"+std::to_string(block) && p.sit_name=="SitName:"+std::to_string(block));
  assert(p.sale_info.block==block && p.category.block==block);
  assert(p.ag_perms.field=="AggregatePerms" && p.ag_texture_perms.field=="AggregatePermTextures" && p.ag_texture_perms_owner.field=="AggregatePermTexturesOwner");
  assert(p.ag_perms.block==block && p.ag_texture_perms.block==block && p.ag_texture_perms_owner.block==block);
  const auto permissions=p.permissions();
  assert((permissions.identities==std::array{p.creator_id,p.owner_id,p.last_owner_id,p.group_id}));
  assert((permissions.masks==std::array{U32(message.token("BaseMask",block))<<20,U32(message.token("OwnerMask",block))<<20,U32(message.token("EveryoneMask",block))<<20,U32(message.token("GroupMask",block))<<20,U32(message.token("NextOwnerMask",block))<<20}));
  assert(p.texture_ids.size()==2);for(S32 n=0;n<32;++n)assert(p.texture_ids[n/16].mData[n%16]==U8(n+block));
 }
 for(S32 invalid:{-1,1,15,17,513,1024}){message.packedSize=invalid;message.binaryRead=false;assert(!vs_read_object_properties(message,0));assert(!message.binaryRead);}
 message.packedSize=0;message.binaryRead=false;auto empty=vs_read_object_properties(message,0);assert(empty && empty->texture_ids.empty() && !message.binaryRead);
 message.packedSize=512;auto largest=vs_read_object_properties(message,0);assert(largest && largest->texture_ids.size()==32);
 const auto family=vs_read_object_properties_family(message);assert(!family.complete && family.creator_id==LLUUID{} && family.texture_ids.empty());
 assert(family.request_flags==(U32(message.token("RequestFlags",0))<<20));assert(family.name=="Name:0" && family.desc=="Description:0");
 assert(family.permissions().masks==vs_read_object_properties(message,0)->permissions().masks);
}
