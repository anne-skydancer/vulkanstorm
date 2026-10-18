// Actual EEP sky construction, alternate-layout bindings and quaternion edits.
#include <cassert>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
using F32=float;constexpr float DEG_TO_RAD=0.017453292519943295f,F_TWO_PI=6.283185307179586f,F_APPROXIMATELY_ZERO=1e-5f;bool is_approx_zero(float v){return std::abs(v)<1e-6;}
struct LLQuaternion{float v=0;void setAngleAxis(float a,float,float,float){v=a;}LLQuaternion&operator*=(const LLQuaternion&o){v+=o.v;return*this;}};
struct LLColor3{float mV[3]={};LLColor3()=default;LLColor3(float a,float b,float c):mV{a,b,c}{}LLColor3 operator/(float f)const{return{mV[0]/f,mV[1]/f,mV[2]/f};}};
struct LLSD{double value=0;LLSD()=default;LLSD(double v):value(v){}double asReal()const{return value;}};
struct Control{bool enabled=true,all=true;LLSD value;LLQuaternion rotation;std::function<void(Control*,const LLSD&)>commit;
void setEnabled(bool v){enabled=v;}void setAllChildrenEnabled(bool v){all=v;}void setCommitCallback(std::function<void(Control*,const LLSD&)>f){commit=f;}void setBlankImageAssetID(int){}void setDefaultImageAssetID(int){}void setAllowNoTexture(bool){}void setRotation(const LLQuaternion&q){rotation=q;}LLQuaternion getRotation()const{return rotation;}void setValue(LLSD v){value=v;}template<class T>void set(const T&){}LLSD getValue()const{return value;}static void getAzimuthAndElevationDeg(const LLQuaternion&q,float&a,float&e){a=q.v*3;e=q.v*2;}};
using LLUICtrl=Control;using LLTextureCtrl=Control;using LLColorSwatchCtrl=Control;using LLVirtualTrackball=Control;using LLView=Control;
struct LLSettingsSky{using ptr_t=std::shared_ptr<LLSettingsSky>;static int GetDefaultCloudNoiseTextureId(){return 1;}static int GetBlankSunTextureId(){return 2;}static int GetDefaultMoonTextureId(){return 3;}int updates=0;LLQuaternion sun{1},moon{2};LLColor3 getSunlightColor(){return{.3f,.6f,.9f};}LLColor3 getGlow(){return{1,0,1};}float getStarBrightness(){return 1;}int getSunTextureId(){return 2;}int getMoonTextureId(){return 3;}float getSunScale(){return 1;}float getMoonScale(){return 2;}float getMoonBrightness(){return 3;}LLQuaternion getSunRotation(){return sun;}LLQuaternion getMoonRotation(){return moon;}void setSunRotation(const LLQuaternion&q){sun=q;}void setMoonRotation(const LLQuaternion&q){moon=q;}void update(){++updates;}};
CONSTANTS
struct LLPanelSettingsSky{virtual~LLPanelSettingsSky()=default;bool mChildrenReady=false,enabled=true,editable=false;int missing=0,dirty=0,refreshed=0;LLSettingsSky::ptr_t mSkySettings;std::map<std::string,Control>controls;
virtual void setEnabled(bool b){enabled=b;}bool getCanChangeSettings(){return editable;}void setAllChildrenEnabled(bool b){for(auto&c:controls)c.second.setEnabled(b);}void setIsDirty(){++dirty;}
template<class T>T*getChild(const std::string&n){auto i=controls.find(n);if(i==controls.end()){++missing;throw std::runtime_error("dummy "+n);}return&i->second;}template<class T>T*findChild(const std::string&n){auto i=controls.find(n);return i==controls.end()?nullptr:&i->second;}Control*getChildView(const std::string&n){return getChild<Control>(n);}Control*findChildView(const std::string&n){return findChild<Control>(n);}
};
CLASSES
struct Parser{};struct LLSimpleXUIParser:Parser{const char*mCurAttributeValueBegin=nullptr;static bool readBoolValue(Parser&,void*);};
METHODS
int main(){
 LLSimpleXUIParser parser;bool b=false;parser.mCurAttributeValueBegin="true";assert(parser.readBoolValue(parser,&b)&&b);parser.mCurAttributeValueBegin="1";assert(!parser.readBoolValue(parser,&b));
for(bool fs:{false,true}){
 LLPanelSettingsSkySunMoonTab panel;LLPanelSettingsSky*base=&panel;base->setEnabled(false);assert(!panel.enabled&&panel.missing==0);panel.controls["decoration"]={};base->setEnabled(true);panel.refresh();assert(panel.missing==0&&panel.enabled);
 for(const auto&name:ALL_FIELDS){if(fs&&(name==FIELD_SKY_SUN_ROTATION||name==FIELD_SKY_MOON_ROTATION))continue;panel.controls[name]={};}
 panel.controls[fs?"P_Sun_Setting":PANEL_SKY_SUN_LAYOUT]={};panel.controls[fs?"P_Moon_Setting":PANEL_SKY_MOON_LAYOUT]={};
 assert(panel.postBuild()&&panel.mChildrenReady&&panel.missing==0);assert(bool(panel.mSunRotation)==!fs&&bool(panel.mMoonRotation)==!fs);assert(!panel.mSunLayout->all&&!panel.mMoonLayout->all);assert(panel.controls[FIELD_SKY_SUN_BEACON].enabled&&panel.controls[FIELD_SKY_MOON_BEACON].enabled);
 panel.editable=true;panel.mSkySettings=std::make_shared<LLSettingsSky>();panel.refresh();assert(panel.mSunLayout->all&&panel.mMoonLayout->all&&panel.missing==0);assert(panel.controls[FIELD_SKY_SUN_AZIMUTH].value.value==3&&panel.controls[FIELD_SKY_MOON_ELEVATION].value.value==4);
 panel.controls[FIELD_SKY_SUN_AZIMUTH].value=120;panel.controls[FIELD_SKY_SUN_ELEVATION].value=25;panel.controls[FIELD_SKY_SUN_AZIMUTH].commit(nullptr,{});assert(panel.dirty==1&&panel.mSkySettings->updates==1);if(!fs)assert(panel.mSunRotation->rotation.v==panel.mSkySettings->sun.v);
 panel.controls[FIELD_SKY_MOON_AZIMUTH].value=90;panel.controls[FIELD_SKY_MOON_ELEVATION].value=0;panel.controls[FIELD_SKY_MOON_ELEVATION].commit(nullptr,{});assert(panel.dirty==2&&panel.mSkySettings->updates==2);if(!fs)assert(panel.mMoonRotation->rotation.v==panel.mSkySettings->moon.v);
 if(!fs){panel.mSunRotation->rotation.v=15;panel.mSunRotation->commit(nullptr,{});assert(panel.mSkySettings->sun.v==15&&panel.controls[FIELD_SKY_SUN_AZIMUTH].value.value==45);panel.mMoonRotation->rotation.v=20;panel.mMoonRotation->commit(nullptr,{});assert(panel.mSkySettings->moon.v==20&&panel.controls[FIELD_SKY_MOON_ELEVATION].value.value==40);}
 panel.editable=false;panel.refresh();assert(!panel.mSunLayout->all&&!panel.mMoonLayout->all&&panel.controls[FIELD_SKY_SUN_BEACON].enabled&&panel.missing==0);
}
OTHER_TABS
}
