#include <vector>
#include <cassert>
#include <cstring>
#include <array>
using U8=unsigned char;using U32=unsigned;using S32=int;
constexpr U8 UPD_POSITION=1,UPD_ROTATION=2,UPD_SCALE=4;constexpr int _PREHASH_ObjectData=1,_PREHASH_ObjectLocalID=2,_PREHASH_Type=3,_PREHASH_Data=4,MVT_LLVector3=5,MVT_LLQuaternion=6;
std::vector<int>events;
struct LLVector3{float mV[3];};struct LLQuaternion{float mV[4];LLVector3 packToVector3()const{events.push_back(9);return {{mV[0],mV[1],mV[2]}};}};
void htolememcpy(void*dst,const void*src,int kind,int count){events.push_back(10+kind);std::memcpy(dst,src,count);}
struct LLViewerObject{U32 local=7;LLVector3 pos,scale;LLQuaternion rot;U32 getLocalID(){events.push_back(5);return local;}const LLVector3&getPosition(){events.push_back(6);return pos;}LLQuaternion getRotation(){events.push_back(7);return rot;}const LLVector3&getScale(){events.push_back(8);return scale;}};
struct LLSelectNode{LLViewerObject*value;LLViewerObject*getObject(){return value;}};
struct LLMessageSystem{U32 id=0;U8 type=0;std::vector<U8>data;void nextBlockFast(int x){assert(x==1);events.push_back(1);}void addU32Fast(int x,U32 n){assert(x==2);id=n;events.push_back(2);}void addU8Fast(int x,U8 n){assert(x==3);type=n;events.push_back(3);}void addBinaryDataFast(int x,const U8*bytes,int size){assert(x==4);data.assign(bytes,bytes+size);events.push_back(4);}}message;LLMessageSystem*gMessageSystem=&message;
struct LLSelectMgr{static void packMultipleUpdate(LLSelectNode*,void*);};
ORIGINAL
KERNEL
struct Peer{LLViewerObject*value;U32 localID()const{return value->getLocalID();}const LLVector3&position()const{return value->getPosition();}LLQuaternion rotation()const{return value->getRotation();}const LLVector3&scale()const{return value->getScale();}};
int main(){for(U32 type=0;type<256;++type)for(int seed=0;seed<16;++seed){LLViewerObject object;object.local=seed+500;for(int i=0;i<3;++i){object.pos.mV[i]=(seed+i)*.25f;object.scale.mV[i]=(seed-i)*.125f;object.rot.mV[i]=(seed+i)*.03125f;}LLSelectNode node{&object};events.clear();LLSelectMgr::packMultipleUpdate(&node,&type);auto expected=message;auto calls=events;message={};events.clear();vs_pack_multiple_update(message,Peer{&object},U8(type));assert(message.id==expected.id&&message.type==expected.type&&message.data==expected.data&&events==calls);}}
