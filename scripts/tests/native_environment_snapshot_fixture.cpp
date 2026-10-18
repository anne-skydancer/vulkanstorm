#include <cassert>
#include <cmath>
#include <algorithm>
#include <array>
#include <memory>
#include <vector>
#include <stdexcept>
#include <string>
using S32=int;using F32=float;
constexpr float F_PI=3.14159265358979323846f;
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_SHADER
float llmax(float a,float b){return std::max(a,b);}
struct LLVector3{float mV[3];explicit LLVector3(const float*v){std::copy_n(v,3,mV);}LLVector3(float x=0,float y=0,float z=0):mV{x,y,z}{}float normalize(){float n=std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);if(n>0)for(float&v:mV)v/=n;return n;}LLVector3 operator*(float v)const{return {mV[0]*v,mV[1]*v,mV[2]*v};}LLVector3 operator%(const LLVector3&v)const{return {mV[1]*v.mV[2]-mV[2]*v.mV[1],mV[2]*v.mV[0]-mV[0]*v.mV[2],mV[0]*v.mV[1]-mV[1]*v.mV[0]};}};
struct LLVector4{float mV[4];explicit LLVector4(const float*v){std::copy_n(v,4,mV);}LLVector4():mV{}{} };
class LLGLSLShader;
SHARED_UNIFORMS
struct LLGLSLShader{enum{SG_DEFAULT,SG_SKY,SG_WATER,SG_ANY,SG_COUNT};};
struct LLShaderMgr{enum{AMBIENT,BLUE_DENSITY,BLUE_HORIZON,CLASSIC_MODE,CLOUD_COLOR,CLOUD_POS_DENSITY1,CLOUD_POS_DENSITY2,CLOUD_SCALE,CLOUD_SHADOW,CLOUD_VARIANCE,DENSITY_MULTIPLIER,DROPLET_RADIUS,GAMMA,GLOW,HAZE_DENSITY,HAZE_HORIZON,ICE_LEVEL,LIGHTNORM,MAX_Y,MOISTURE_LEVEL,MOONLIGHT_COLOR,REFLECTION_PROBE_AMBIANCE,SCENE_LIGHT_STRENGTH,SKY_AMBIENT_SCALE,SKY_HDR_SCALE,SKY_SUNLIGHT_SCALE,SUNLIGHT_COLOR,SUN_MOON_GLOW_FACTOR,SUN_UP_FACTOR,COUNT};};
struct LLSettingsBase{using ptr_t=std::shared_ptr<LLSettingsBase>;virtual~LLSettingsBase()=default;virtual void applyToUniforms(void*){}virtual void applySpecial(void*){};};
struct Sky:LLSettingsBase{int updates=0;bool sun=true,moon=true,missing=false;float value=1;
void update(){++updates;}bool getIsSunUp(){return sun;}bool getIsMoonUp(){return moon;}float getDomeRadius(){return 15000;}LLVector3 getSunDiffuse(){return {2,3,4};}LLVector3 getMoonDiffuse(){return {.1,.2,.3};}
void applyToUniforms(void*p)override{auto*g=(LLShaderUniforms*)p;for(int key=0;key<LLShaderMgr::COUNT;++key){if(missing && key==LLShaderMgr::CLOUD_POS_DENSITY2)continue;g[LLGLSLShader::SG_ANY].uniform1f(key,value+key);g[LLGLSLShader::SG_ANY].uniform3fv(key,LLVector3(value+key,value+key,value+key));}}
void applySpecial(void*p)override{auto*g=(LLShaderUniforms*)p;g[LLGLSLShader::SG_ANY].uniform3fv(LLShaderMgr::AMBIENT,LLVector3(11,11,11));g[LLGLSLShader::SG_ANY].uniform3fv(LLShaderMgr::AMBIENT,LLVector3(12,12,12));g[LLGLSLShader::SG_SKY].uniform3fv(LLShaderMgr::AMBIENT,LLVector3(17,17,17));}
};
struct Water{int updates=0;void update(){++updates;}LLVector3 getWaterFogColor(){return {.1,.3,.5};}float getModifiedWaterFogDensity(bool under){return under?12:3;}};
struct LLEnvironment{static inline LLEnvironment*live=nullptr;std::shared_ptr<Sky>sky=std::make_shared<Sky>();std::shared_ptr<Water>water=std::make_shared<Water>();static bool instanceExists(){return live;}static LLEnvironment&instance(){assert(live);return *live;}auto getCurrentSky(){return sky;}auto getCurrentWater(){return water;}float getCamHeight(){return 14400;}static void updateGLVariablesForSettings(LLShaderUniforms*,const LLSettingsBase::ptr_t&);};
ENV_PUBLISH
STATE_HEADER
STATE_SOURCE
int main(){LLVector3 eye(1,2,30),at(1,0,0),up(0,0,1);auto empty=vs_environment_state(eye,at,up,2,1,20);assert(empty.values[0][0]==0);
LLEnvironment owner;LLEnvironment::live=&owner;
auto first=vs_environment_state(eye,at,up,2,1,20);assert(first.values[0][0]==1 && first.values[0][2]==15000 && first.values[0][3]==14400);
assert(first.values[7][0]==17);assert(first.values[16][0]==2 && first.values[15][0]==3);assert(owner.sky->updates==1 && owner.water->updates==1);
owner.sky->value=9;owner.sky->sun=false;
auto edited=vs_environment_state(LLVector3(1,2,10),at,up,2,1,20);assert(edited.values[5][0]==9+LLShaderMgr::SUNLIGHT_COLOR && first.values[5][0]!=edited.values[5][0]);assert(edited.values[16][0]==.1f && edited.values[15][0]==12);
owner.sky->moon=false;auto dark=vs_environment_state(eye,at,up,2,1,20);assert(dark.values[18][0]==0 && dark.values[18][2]==0);
owner.sky->missing=true;bool refused=false;try{vs_environment_state(eye,at,up,2,1,20);}catch(const std::runtime_error&){refused=true;}assert(refused);
LLEnvironment::live=nullptr;assert(vs_environment_state(eye,at,up,2,1,20).values[0][0]==0);
LLEnvironment next;LLEnvironment::live=&next;auto relog=vs_environment_state(eye,at,up,2,1,20);assert(relog.values[5][0]==first.values[5][0] && relog.values[18][0]>0);
assert(vs_environment_state(eye,at,up,0,1,20).values[0][0]==0);}
