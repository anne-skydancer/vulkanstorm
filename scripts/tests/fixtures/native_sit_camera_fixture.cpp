#include <functional>
#include <optional>
#include <memory>
#include <cassert>
using LLUUID=int;
struct Vec{int value=0;Vec operator+(Vec b)const{return {value+b.value};}Vec operator*(int q)const{return {value*q};}};using LLVector3=Vec;using LLVector3d=Vec;
struct VSCameraFocusObject{Vec renderPosition;int renderRotation;bool live=true;bool current()const{return live;}};
bool avatar=true,seated=true;void*vs_avatar_character(){return avatar?&avatar:nullptr;}struct Agent{bool isSitting(){return seated;}Vec getPosGlobalFromAgent(Vec v){return {100+v.value};}}gAgent;
struct Camera{struct State{std::function<std::optional<VSCameraFocusObject>()>sitQuery;std::function<bool()>account;};std::unique_ptr<State>mNativeFocus=std::make_unique<State>();bool mSitCameraEnabled=true,mFocusOnAvatar=true,third=true;Vec mSitCameraPos{2},mSitCameraFocus{3};bool cameraThirdPerson(){return third;}void setSitCamera(int){mSitCameraEnabled=false;mNativeFocus->sitQuery={};}bool apply(Vec&avatarEye,Vec&avatarFocus){
BLOCK
return true;
}};
int main(){for(int mask=0;mask<64;++mask){Camera c;bool account=(mask&1)==0;bool hasSource=(mask&2)==0;bool current=(mask&4)==0;c.third=(mask&8)==0;c.mFocusOnAvatar=(mask&16)==0;seated=(mask&32)==0;c.mNativeFocus->account=[&]{return account;};int calls=0;c.mNativeFocus->sitQuery=[&]()->std::optional<VSCameraFocusObject>{++calls;return hasSource?std::optional<VSCameraFocusObject>{{{7},4,current}}:std::nullopt;};Vec eye{1},focus{2};bool result=c.apply(eye,focus);assert(calls==1);if(!account){assert(!result&&eye.value==1&&focus.value==2);continue;}assert(result);if(!hasSource||!current){assert(!c.mSitCameraEnabled&&!c.mNativeFocus->sitQuery);}bool expected=account&&hasSource&&current&&c.third&&c.mFocusOnAvatar&&seated;assert(eye.value==(expected?115:1));assert(focus.value==(expected?119:2));}Camera c;c.mNativeFocus->account=[] {return true;};c.mNativeFocus->sitQuery=[&]()->std::optional<VSCameraFocusObject>{c.mNativeFocus.reset();return {};};Vec eye{1},focus{2};assert(!c.apply(eye,focus));}
