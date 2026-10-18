// Shared CPU environment values for native draw submission. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsenvironmentstate.h"
#include "llenvironment.h"
#include "llsettingssky.h"
#include "llsettingswater.h"
#include "llviewershadermgr.h"
#include <cmath>
#include <stdexcept>
namespace
{
// Exact reference application order: SG_ANY, then shader-specific group; the
// last assignment in a group wins, including applySpecial's auto-adjust policy.
using Groups=std::array<LLShaderUniforms,LLGLSLShader::SG_COUNT>;
std::array<float,4> vectorValue(const Groups& groups,S32 key)
{
    std::array<float,4> value{};bool found=false;
    for(const int group:{int(LLGLSLShader::SG_ANY),int(LLGLSLShader::SG_SKY)})
        for(const auto& setting:groups[group].mVector3s)if(setting.mUniform==key)
        {std::copy_n(setting.mValue.mV,3,value.begin());found=true;}
    if(!found)throw std::runtime_error("Missing shared sky vector uniform");
    return value;
}
float scalarValue(const Groups& groups,S32 key)
{
    float value=0;bool found=false;
    for(const int group:{int(LLGLSLShader::SG_ANY),int(LLGLSLShader::SG_SKY)})
    {
        for(const auto& setting:groups[group].mFloats)if(setting.mUniform==key){value=setting.mValue;found=true;}
        for(const auto& setting:groups[group].mIntegers)if(setting.mUniform==key){value=float(setting.mValue);found=true;}
    }
    if(!found)throw std::runtime_error("Missing shared sky scalar uniform");
    return value;
}
std::array<float,4> vectorValue(const LLVector3& value)
{return {value.mV[0],value.mV[1],value.mV[2],0};}
}
VSEnvironmentState vs_environment_state(const LLVector3& eye,const LLVector3& direction,const LLVector3& vertical,
    float aspect,float verticalFov,float waterHeight)
{
    VSEnvironmentState state;
    if(!LLEnvironment::instanceExists())return state;
    auto& owner=LLEnvironment::instance();
    const auto sky=owner.getCurrentSky();
    const auto water=owner.getCurrentWater();
    if(!sky || !water)return state;
    LLVector3 at=direction,up=vertical;
    if(at.normalize()<1.e-6f || up.normalize()<1.e-6f || !std::isfinite(aspect) || aspect<=0 ||
        !std::isfinite(verticalFov) || verticalFov<=0 || verticalFov>=F_PI)return state;
    LLVector3 right=at%up;if(right.normalize()<1.e-6f)return state;
    up=right%at;up.normalize();
    sky->update();water->update();
    // Only scratch allocation capacity is process-owned; values are cleared by
    // the reference publication before every frame and no setting owner escapes.
    thread_local Groups groups;
    LLEnvironment::updateGLVariablesForSettings(groups.data(),sky);
    auto& v=state.values;
    v[1]=vectorValue(at);v[2]=vectorValue(right*(std::tan(verticalFov*.5f)*aspect));
    v[3]=vectorValue(up*std::tan(verticalFov*.5f));
    v[4]=vectorValue(groups,LLShaderMgr::LIGHTNORM);
    v[5]=vectorValue(groups,LLShaderMgr::SUNLIGHT_COLOR);v[6]=vectorValue(groups,LLShaderMgr::MOONLIGHT_COLOR);
    v[7]=vectorValue(groups,LLShaderMgr::AMBIENT);v[8]=vectorValue(groups,LLShaderMgr::BLUE_HORIZON);
    v[9]=vectorValue(groups,LLShaderMgr::BLUE_DENSITY);
    v[10]={scalarValue(groups,LLShaderMgr::HAZE_HORIZON),scalarValue(groups,LLShaderMgr::HAZE_DENSITY),
        scalarValue(groups,LLShaderMgr::DENSITY_MULTIPLIER),scalarValue(groups,LLShaderMgr::MAX_Y)};
    v[11]=vectorValue(groups,LLShaderMgr::GLOW);
    v[12]={scalarValue(groups,LLShaderMgr::CLOUD_SHADOW),scalarValue(groups,LLShaderMgr::SUN_UP_FACTOR),
        scalarValue(groups,LLShaderMgr::SUN_MOON_GLOW_FACTOR),scalarValue(groups,LLShaderMgr::SKY_HDR_SCALE)};
    v[13]=vectorValue(eye);
    const auto fog=water->getWaterFogColor();v[14]={fog.mV[0],fog.mV[1],fog.mV[2],0};
    v[15]={water->getModifiedWaterFogDensity(eye.mV[2]<=waterHeight),
        1.f/llmax(v[4][2],.3f),waterHeight,1};
    const auto diffuse=sky->getIsSunUp()?sky->getSunDiffuse():sky->getMoonDiffuse();
    v[16]={diffuse.mV[0],diffuse.mV[1],diffuse.mV[2],0};
    v[17]={scalarValue(groups,LLShaderMgr::GAMMA),scalarValue(groups,LLShaderMgr::SCENE_LIGHT_STRENGTH),
        scalarValue(groups,LLShaderMgr::SKY_SUNLIGHT_SCALE),scalarValue(groups,LLShaderMgr::SKY_AMBIENT_SCALE)};
    v[18]={scalarValue(groups,LLShaderMgr::MOISTURE_LEVEL),scalarValue(groups,LLShaderMgr::DROPLET_RADIUS),
        scalarValue(groups,LLShaderMgr::ICE_LEVEL),scalarValue(groups,LLShaderMgr::REFLECTION_PROBE_AMBIANCE)};
    if(!sky->getIsSunUp() && !sky->getIsMoonUp()){v[18][0]=0;v[18][2]=0;}
    v[19]=vectorValue(groups,LLShaderMgr::CLOUD_POS_DENSITY1);v[20]=vectorValue(groups,LLShaderMgr::CLOUD_POS_DENSITY2);
    v[21]=vectorValue(groups,LLShaderMgr::CLOUD_COLOR);
    v[22]={scalarValue(groups,LLShaderMgr::CLOUD_SCALE),scalarValue(groups,LLShaderMgr::CLOUD_VARIANCE),
        scalarValue(groups,LLShaderMgr::CLASSIC_MODE),0};
    for(const auto& value:v)for(float component:value)if(!std::isfinite(component))
        throw std::runtime_error("Nonfinite shared native environment publication");
    v[0]={1,1,sky->getDomeRadius(),owner.getCamHeight()};return state;
}
