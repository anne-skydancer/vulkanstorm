#include <array>
#include <algorithm>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <string>
#include <random>
#include <cmath>
#include <cassert>
using F32=float;using S32=int;using U32=unsigned;
constexpr int LL_CHARACTER_MAX_ANIMATED_JOINTS=16;
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR
struct LLVector4a{float v[4]{};const float* getF32ptr()const{return v;}float* getF32ptr(){return v;}};
struct LLVector4{float v[4];float& operator[](int i){return v[i];}};
void update_min_max(LLVector4a& low,LLVector4a& high,const LLVector4a& p){for(unsigned i=0;i<3;++i){low.v[i]=std::min(low.v[i],p.v[i]);high.v[i]=std::max(high.v[i],p.v[i]);}}
struct LLMatrix4a{
 std::array<float,3> offset{};
 static const LLMatrix4a& identity(){static LLMatrix4a value;return value;}
 void affineTransform(const LLVector4a& p,LLVector4a& result)const{for(unsigned i=0;i<3;++i)result.v[i]=p.v[i]+offset[i];}
};
struct LLJointRiggingInfo{bool rigged=false;LLVector4a extents[2];void setIsRiggedTo(bool v){rigged=v;}bool isRiggedTo()const{return rigged;}LLVector4a* getRiggedExtents(){return extents;}};
struct LLJointRiggingInfoTab{
 std::vector<LLJointRiggingInfo> data;bool dirty=true;
 void resize(int n){data.resize(n);}int size()const{return int(data.size());}bool needsUpdate()const{return dirty;}void setNeedsUpdate(bool value){dirty=value;}
 LLJointRiggingInfo& operator[](int i){return data[i];}
};
struct LLJoint{int number;int getJointNum()const{return number;}};
struct LLCharacter{std::map<std::string,LLJoint> joints;LLJoint* getJoint(const std::string& name){auto i=joints.find(name);return i==joints.end()?nullptr:&i->second;}};
using LLVOAvatar=LLCharacter;
struct LLMeshSkinInfo{std::vector<std::string> mJointNames;mutable std::vector<int> mJointNums;std::vector<LLMatrix4a> mBindPoseMatrix;};
struct LLVolumeFace{int mNumVertices=0;std::vector<LLVector4a> weights,positions;LLVector4a* mWeights=nullptr;LLVector4a* mPositions=nullptr;LLJointRiggingInfoTab mJointRiggingInfoTab;};
struct LLVolume{std::vector<LLVolumeFace> faces;int getNumVolumeFaces()const{return int(faces.size());}const LLVolumeFace& getVolumeFace(int i)const{return faces[i];}};
namespace VSResourceBudget{struct Reservation{int* alive;Reservation(int* p):alive(p){++*alive;}~Reservation(){--*alive;}};}
struct VSAvatarRiggingInfo{
 std::shared_ptr<const VSResourceBudget::Reservation> residency;struct Joint{std::string name;LLJointRiggingInfo bounds;};std::vector<Joint> joints;
 static std::shared_ptr<const VSAvatarRiggingInfo> create(const LLVolume&,const LLMeshSkinInfo&,const std::function<std::shared_ptr<const VSResourceBudget::Reservation>(std::size_t)>&);
};
template<class T>T llclamp(T n,T low,T high){return std::clamp(n,low,high);}
namespace LLSkinningUtil{void initJointNums(LLMeshSkinInfo* skin,LLVOAvatar* avatar){skin->mJointNums.clear();for(const auto& name:skin->mJointNames){auto* joint=avatar->getJoint(name);skin->mJointNums.push_back(joint?joint->number:0);}}void updateRiggingInfo(const LLMeshSkinInfo*,LLVOAvatar*,LLVolumeFace&);}
REFERENCE
CURRENT
int main(){
 std::mt19937 random(0x15ca34);std::uniform_real_distribution<float> coordinate(-5,5);
 int alive=0;auto reserve=[&](std::size_t bytes)->std::shared_ptr<const VSResourceBudget::Reservation>{assert(bytes>sizeof(VSAvatarRiggingInfo));return std::make_shared<VSResourceBudget::Reservation>(&alive);};
 for(unsigned trial=0;trial<4000;++trial){
  LLCharacter character;LLMeshSkinInfo skin;for(int i=0;i<12;++i){const auto name="joint"+std::to_string(i);skin.mJointNames.push_back(name);if(i!=3)character.joints[name]={15-i};skin.mBindPoseMatrix.push_back({{coordinate(random),coordinate(random),coordinate(random)}});}
  LLVolume volume;volume.faces.resize(1);auto& face=volume.faces[0];face.mNumVertices=24;face.weights.resize(24);face.positions.resize(24);
  for(unsigned i=0;i<24;++i){face.positions[i].v[0]=coordinate(random);face.positions[i].v[1]=coordinate(random);face.positions[i].v[2]=coordinate(random);for(unsigned k=0;k<4;++k){const float fraction[]={0,.199f,.2f,.201f,.8f};face.weights[i].v[k]=float(random()%12)+fraction[random()%5];}}
  face.mWeights=face.weights.data();face.mPositions=face.positions.data();
  LLSkinningUtil::updateRiggingInfo(&skin,&character,face);
  const auto native=VSAvatarRiggingInfo::create(volume,skin,reserve);assert(native&&alive==1);
  LLJointRiggingInfoTab mapped;mapped.resize(16);
  for(const auto& entry:native->joints){if(!entry.bounds.rigged)continue;const auto* resolved=character.getJoint(entry.name);auto& actual=mapped[resolved?resolved->number:0];actual.rigged=true;update_min_max(actual.extents[0],actual.extents[1],entry.bounds.extents[0]);update_min_max(actual.extents[0],actual.extents[1],entry.bounds.extents[1]);}
  for(int joint=0;joint<16;++joint){auto& actual=mapped[joint];auto& expected=face.mJointRiggingInfoTab[joint];assert(actual.rigged==expected.rigged);for(unsigned point=0;point<2;++point)for(unsigned axis=0;axis<3;++axis)assert(actual.extents[point].v[axis]==expected.extents[point].v[axis]);}
 }
 assert(alive==0);
 LLCharacter character;LLMeshSkinInfo skin;skin.mJointNames={"unknown"};LLVolume volume;
 assert(!VSAvatarRiggingInfo::create(volume,skin,[](std::size_t)->std::shared_ptr<const VSResourceBudget::Reservation>{return {};}));
 assert(alive==0);
 // Untrusted malformed source cannot leak its admitted reservation.
 skin.mJointNames={"joint"};volume.faces.resize(1);auto& bad=volume.faces[0];bad.mNumVertices=1;bad.positions.resize(1);bad.weights.resize(1);bad.mPositions=bad.positions.data();bad.mWeights=bad.weights.data();bad.weights[0].v[0]=NAN;
 assert(!VSAvatarRiggingInfo::create(volume,skin,reserve));assert(alive==0);
 skin.mJointNames={std::string(257,'x')};assert(!VSAvatarRiggingInfo::create(volume,skin,reserve));assert(alive==0);
}
