#include <cassert>
#include <string>
#include <string_view>
struct LLSD {std::string s;int i=-1;LLSD(const char* v):s(v){}LLSD(int v):i(v){}std::string asString()const{return s;}int asInteger()const{return i;}};
struct LLVOAvatar {enum{AV_RENDER_NORMALLY=0,AV_ALWAYS_RENDER=2};};
struct FSFloaterPerformance {static bool isNativeCallback(std::string_view,const LLSD&);};struct FSFloaterAvatarRenderSettings {static bool isNativeCallback(std::string_view,const LLSD&);};
PERFORMANCE
EXCEPTIONS
int main(){
 for(const char* verb:{"always","never","default"}){assert(FSFloaterPerformance::isNativeCallback("Settings.SetRendering",verb));assert(FSFloaterPerformance::isNativeCallback("Settings.IsSelected",verb));}
 assert(FSFloaterPerformance::isNativeCallback("Settings.IsSelected","non_default"));assert(!FSFloaterPerformance::isNativeCallback("Settings.SetRendering","non_default"));
 for(const char* verb:{"zoom","inspect"})assert(FSFloaterPerformance::isNativeCallback("Avatar.Extended",verb));
 for(const char* verb:{"delete","teleport","freeze",""})assert(!FSFloaterPerformance::isNativeCallback("Avatar.Extended",verb));
 assert(FSFloaterAvatarRenderSettings::isNativeCallback("Settings.AddNewEntry","always"));assert(FSFloaterAvatarRenderSettings::isNativeCallback("Settings.AddNewEntry","never"));assert(!FSFloaterAvatarRenderSettings::isNativeCallback("Settings.AddNewEntry","anything"));
 for(int value:{0,1,2})assert(FSFloaterAvatarRenderSettings::isNativeCallback("Avatar.ChangeRenderSetting",value));for(int value:{-1,3})assert(!FSFloaterAvatarRenderSettings::isNativeCallback("Avatar.ChangeRenderSetting",value));assert(!FSFloaterAvatarRenderSettings::isNativeCallback("Avatar.Teleport",0));
}
