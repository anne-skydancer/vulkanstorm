// Real water-panel enable/postBuild methods against a strict control factory.
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <stdexcept>
using F32=float;
struct LLSD{double v=0;double asReal()const{return v;}};
struct Control{bool enabled=true;LLSD value;std::function<void(Control*,const LLSD&)>commit;void setEnabled(bool v){enabled=v;}void setCommitCallback(std::function<void(Control*,const LLSD&)> f){commit=f;}void setDefaultImageAssetID(int){}void setBlankImageAssetID(int){}LLSD getValue()const{return value;}};
using LLUICtrl=Control;using LLColorSwatchCtrl=Control;using LLTextureCtrl=Control;using LLXYVector=Control;
struct LLSettingsWater{static int GetDefaultWaterNormalAssetId(){return 5;}float fog=0,underwater=0,fresnelScale=0,fresnelOffset=0,above=0,below=0,blur=0;void setWaterFogDensity(float v){fog=v;}void setFogMod(float v){underwater=v;}void setFresnelScale(float v){fresnelScale=v;}void setFresnelOffset(float v){fresnelOffset=v;}void setScaleAbove(float v){above=v;}void setScaleBelow(float v){below=v;}void setBlurMultiplier(float v){blur=v;}};
constexpr int BLANK_OBJECT_NORMAL=7;
CONSTANTS
struct LLPanelSettingsWater{bool enabled=true;virtual~LLPanelSettingsWater()=default;virtual void setEnabled(bool v){enabled=v;}};
struct LLPanelSettingsWaterMainTab:LLPanelSettingsWater{
CHILD_STATE
std::map<std::string,Control> controls;int missing=0,dirty=0,refreshes=0;bool editable=false;LLColorSwatchCtrl*mClrFogColor=nullptr;LLTextureCtrl*mTxtNormalMap=nullptr;std::shared_ptr<LLSettingsWater>mWaterSettings;
template<class T>T*getChild(const std::string&name){auto it=controls.find(name);if(it==controls.end()){++missing;throw std::runtime_error("dummy control requested: "+name);}return&it->second;}
bool postBuild();void setEnabled(bool)override;void refresh(){++refreshes;setEnabled(editable);}void setIsDirty(){++dirty;}
void onLargeWaveChanged(){}void onSmallWaveChanged(){}void onNormalMapChanged(){}void onNormalScaleChanged(){}void onFogColorChanged(){}
void onFogDensityChanged();void onFogUnderWaterChanged();void onFresnelScaleChanged();void onFresnelOffsetChanged();void onScaleAboveChanged();void onScaleBelowChanged();void onBlurMultipChanged();
};
METHODS
int main(){LLPanelSettingsWaterMainTab panel;LLPanelSettingsWater*base=&panel;
// The actual LLPanel initialization calls virtual setEnabled before XUI children.
base->setEnabled(false);base->setEnabled(true);assert(panel.missing==0&&panel.enabled&&panel.controls.empty());
// Having one decoration child does not establish that required controls exist.
panel.controls["panel_border"]={};base->setEnabled(false);assert(panel.missing==0&&!panel.enabled);
for(const auto&name:{FIELD_WATER_FOG_COLOR,FIELD_WATER_FOG_DENSITY,FIELD_WATER_UNDERWATER_MOD,FIELD_WATER_NORMAL_MAP,FIELD_WATER_WAVE1_XY,FIELD_WATER_WAVE2_XY,FIELD_WATER_NORMAL_SCALE_X,FIELD_WATER_NORMAL_SCALE_Y,FIELD_WATER_NORMAL_SCALE_Z,FIELD_WATER_FRESNEL_SCALE,FIELD_WATER_FRESNEL_OFFSET,FIELD_WATER_SCALE_ABOVE,FIELD_WATER_SCALE_BELOW,FIELD_WATER_BLUR_MULTIP})panel.controls[name]={};
assert(panel.postBuild()&&panel.mChildrenReady&&panel.refreshes==1&&!panel.enabled&&panel.missing==0);
for(const auto&name:{FIELD_WATER_FOG_DENSITY,FIELD_WATER_UNDERWATER_MOD,FIELD_WATER_NORMAL_SCALE_X,FIELD_WATER_NORMAL_SCALE_Y,FIELD_WATER_NORMAL_SCALE_Z,FIELD_WATER_FRESNEL_SCALE,FIELD_WATER_FRESNEL_OFFSET,FIELD_WATER_SCALE_ABOVE,FIELD_WATER_SCALE_BELOW,FIELD_WATER_BLUR_MULTIP})assert(!panel.controls[name].enabled&&panel.controls[name].commit);
panel.editable=true;panel.mWaterSettings=std::make_shared<LLSettingsWater>();panel.refresh();assert(panel.enabled&&panel.controls[FIELD_WATER_BLUR_MULTIP].enabled);
for(const auto&name:{FIELD_WATER_FOG_DENSITY,FIELD_WATER_UNDERWATER_MOD,FIELD_WATER_FRESNEL_SCALE,FIELD_WATER_FRESNEL_OFFSET,FIELD_WATER_SCALE_ABOVE,FIELD_WATER_SCALE_BELOW,FIELD_WATER_BLUR_MULTIP}){panel.controls[name].value.v=2.5;panel.controls[name].commit(&panel.controls[name],{});}
assert(panel.dirty==7&&panel.mWaterSettings->fog==2.5f&&panel.mWaterSettings->underwater==2.5f&&panel.mWaterSettings->fresnelScale==2.5f&&panel.mWaterSettings->fresnelOffset==2.5f&&panel.mWaterSettings->above==2.5f&&panel.mWaterSettings->below==2.5f&&panel.mWaterSettings->blur==2.5f);
}
