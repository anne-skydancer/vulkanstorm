#include <cassert>
#include <vector>
using U8=unsigned char;using U16=unsigned short;using U32=unsigned;
struct LLNetworkData{enum{PARAMS_EXTENDED_MESH=0x70,PARAMS_REFLECTION_PROBE=0x90};};
BODY
std::vector<U8>extra(unsigned type,unsigned size){std::vector<U8>v{1,U8(type),U8(type>>8),U8(size),0,0,0};v.resize(7+size);return v;}
int main(){U32 flags=0;bool probe=false;auto valid=extra(LLNetworkData::PARAMS_REFLECTION_PROBE,9);assert(ownerExtras(valid,flags,&probe)&&probe);for(unsigned size:{0,8,10}){probe=false;assert(!ownerExtras(extra(LLNetworkData::PARAMS_REFLECTION_PROBE,size),flags,&probe)&&!probe);}probe=false;valid.pop_back();assert(!ownerExtras(valid,flags,&probe));auto mesh=extra(LLNetworkData::PARAMS_EXTENDED_MESH,4);mesh[7]=3;assert(ownerExtras(mesh,flags,&probe)&&flags==3&&!probe);assert(ownerExtras(mesh,flags));probe=false;assert(ownerExtras({},flags,&probe)&&!probe);}
