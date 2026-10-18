#include <array>
#include <map>
#include <vector>
#include <set>
#include <memory>
#include <optional>
#include <cmath>
#include <cassert>
#include <algorithm>
#include <string>
using S32=int;using U32=unsigned;using F32=float;using U64=unsigned long long;
constexpr int LL_CHARACTER_MAX_ANIMATED_JOINTS=16;
struct LLVector3{float mV[3]{};LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{};explicit LLVector3(const float* p):mV{p[0],p[1],p[2]}{}};
struct LLVector4a{float v[4]{};void load3(const float* p){for(unsigned i=0;i<3;++i)v[i]=p[i];}const float* getF32ptr()const{return v;}};
void update_min_max(LLVector4a& low,LLVector4a& high,const LLVector4a& p){for(unsigned i=0;i<3;++i){low.v[i]=std::min(low.v[i],p.v[i]);high.v[i]=std::max(high.v[i],p.v[i]);}}
struct Matrix{float c=std::sqrt(.5f),s=std::sqrt(.5f);};
struct LLMatrix4a{Matrix value;void loadu(const Matrix& m){value=m;}};
void matMulBoundBox(const LLMatrix4a& m,const LLVector4a* source,LLVector4a* result){for(unsigned i=0;i<3;++i){result[0].v[i]=1e9;result[1].v[i]=-1e9;}for(unsigned vertex=0;vertex<8;++vertex){const float x=source[vertex&1].v[0],y=source[(vertex>>1)&1].v[1];LLVector4a point;point.v[0]=x*m.value.c-y*m.value.s;point.v[1]=x*m.value.s+y*m.value.c;point.v[2]=source[(vertex>>2)&1].v[2];update_min_max(result[0],result[1],point);}}
struct Joint{Matrix matrix;int number=0;int getJointNum()const{return number;}const Matrix& getWorldMatrix()const{return matrix;}};
using LLJoint=Joint;
struct LLJointRiggingInfo{bool active=false;LLVector4a extents[2];bool isRiggedTo()const{return active;}const LLVector4a* getRiggedExtents()const{return extents;}void merge(const LLJointRiggingInfo& other){if(!other.active)return;if(!active){*this=other;return;}update_min_max(extents[0],extents[1],other.extents[0]);update_min_max(extents[0],extents[1],other.extents[1]);}};
struct LLJointRiggingInfoTab{std::vector<LLJointRiggingInfo> data;void resize(int n){data.resize(n);}int size()const{return int(data.size());}LLJointRiggingInfo& operator[](int i){return data[i];}const LLJointRiggingInfo& operator[](int i)const{return data[i];}void merge(const LLJointRiggingInfoTab& src){if(data.size()<src.data.size())data.resize(src.data.size());for(unsigned i=0;i<src.data.size();++i){if(!src.data[i].active)continue;if(!data[i].active){data[i]=src.data[i];continue;}update_min_max(data[i].extents[0],data[i].extents[1],src.data[i].extents[0]);update_min_max(data[i].extents[0],data[i].extents[1],src.data[i].extents[1]);}}};
struct BudgetState{unsigned alive=0,allocations=0;bool refuse=false;};
struct Reservation{std::shared_ptr<BudgetState> state;explicit Reservation(std::shared_ptr<BudgetState> s):state(s){++s->alive;++s->allocations;}~Reservation(){--state->alive;}};
struct Budget{std::shared_ptr<BudgetState> state;std::shared_ptr<const Reservation> reserve(std::size_t){return state->refuse?nullptr:std::make_shared<Reservation>(state);}};
struct VSAvatarRiggingInfo{std::shared_ptr<const Reservation> residency;struct Entry{std::string name;LLJointRiggingInfo bounds;};std::vector<Entry> joints;};
struct Impl{std::optional<Budget> nameBudget;unsigned nameBudgetRefusals=0;struct NameRiggingCombined{std::shared_ptr<const Reservation> residency;LLJointRiggingInfoTab joints;};std::shared_ptr<const NameRiggingCombined> nameRiggingCombined;U32 nameRiggingSkeleton=0,skeletonSerial=1;std::map<std::string,Joint*> names;Joint* getJoint(const std::string& name){auto i=names.find(name);return i==names.end()?nullptr:i->second;}std::vector<std::shared_ptr<const VSAvatarRiggingInfo>> nameRiggingInputs;int mNumBones=2,mNumCollisionVolumes=0;Joint* mSkeleton[2];Joint mCollisionVolumes[1];std::set<unsigned> hudPoints;std::map<unsigned,std::unique_ptr<Joint>> attachments;};
struct VSWorldAvatar{std::unique_ptr<Impl> mImpl=std::make_unique<Impl>();bool extendRiggingBounds(const std::vector<std::shared_ptr<const VSAvatarRiggingInfo>>&,std::array<LLVector3,2>&);};
CURRENT
int main(){
 auto state=std::make_shared<BudgetState>();VSWorldAvatar avatar;Joint root,spine;spine.number=1;avatar.mImpl->names["spine"]=&spine;avatar.mImpl->names["alias"]=&spine;avatar.mImpl->mSkeleton[0]=&root;avatar.mImpl->mSkeleton[1]=&spine;avatar.mImpl->nameBudget=Budget{state};
 auto a=std::make_shared<VSAvatarRiggingInfo>(),b=std::make_shared<VSAvatarRiggingInfo>();for(auto info:{a,b}){info->joints.resize(1);info->joints[0].name="spine";info->joints[0].bounds.active=true;}
 a->joints[0].bounds.extents[0].load3(LLVector3(-2,-2,-.1f).mV);a->joints[0].bounds.extents[1].load3(LLVector3(0,0,.1f).mV);
 b->joints[0].bounds.extents[0].load3(LLVector3(0,0,-.1f).mV);b->joints[0].bounds.extents[1].load3(LLVector3(2,2,.1f).mV);
 std::vector<std::shared_ptr<const VSAvatarRiggingInfo>> sources{a,b};
 std::array<LLVector3,2> bounds{LLVector3(-.25f,-.25f,-.25f),LLVector3(.25f,.25f,.25f)};
 assert(avatar.extendRiggingBounds(sources,bounds));assert(std::abs(bounds[0].mV[0]+2*std::sqrt(2.f))<1e-6f);assert(std::abs(bounds[1].mV[0]-2*std::sqrt(2.f))<1e-6f);assert(state->allocations==1&&state->alive==1);
 // Transform-before-union would yield only +/-sqrt(2), so this explicitly
 // rejects the tempting but incorrect per-volume spatial-envelope shortcut.
 LLMatrix4a matrix;matrix.loadu(spine.matrix);LLVector4a first[2],second[2];matMulBoundBox(matrix,a->joints[0].bounds.extents,first);matMulBoundBox(matrix,b->joints[0].bounds.extents,second);
 assert(std::max(first[1].v[0],second[1].v[0])<bounds[1].mV[0]-1.f);
 // A new pose uses the actual current joint with no metadata reallocation.
 spine.matrix={1,0};bounds={LLVector3(-.25f,-.25f,-.25f),LLVector3(.25f,.25f,.25f)};assert(avatar.extendRiggingBounds(sources,bounds));assert(bounds[1].mV[0]==2&&state->allocations==1);
 // Same named envelope follows the current skeleton alias/number on rebind.
 avatar.mImpl->names["spine"]=&root;++avatar.mImpl->skeletonSerial;root.number=0;root.matrix={0,1};assert(avatar.extendRiggingBounds(sources,bounds));assert(state->allocations==2);
 const auto previous=avatar.mImpl->nameRiggingCombined;state->refuse=true;sources.pop_back();const auto unchanged=bounds;assert(!avatar.extendRiggingBounds(sources,bounds));assert(bounds[1].mV[0]==unchanged[1].mV[0]&&avatar.mImpl->nameRiggingCombined==previous&&avatar.mImpl->nameBudgetRefusals==1);
 state->refuse=false;assert(avatar.extendRiggingBounds(sources,bounds));assert(state->alive==2);assert(avatar.extendRiggingBounds({},bounds));assert(state->alive==1); // saved owner still pins its charge
}
