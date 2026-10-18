#include <cassert>
#include <map>
#include <string>
#include <fstream>
#include <filesystem>
#include <sstream>
using S32=int;using llifstream=std::ifstream;using llofstream=std::ofstream;
#define LL_INFOS() std::ostringstream()
#define LL_WARNS() std::ostringstream()
#define LL_ENDL ""
const int LL_PATH_PER_SL_ACCOUNT=1;
struct Dir {std::string account="a";std::string getExpandedFilename(int,const std::string& name){return account+"-"+name;}} directory;auto* gDirUtilp=&directory;
struct LLUUID {std::string value;LLUUID(std::string v):value(std::move(v)){}std::string asString()const{return value;}bool operator<(const LLUUID& r)const{return value<r.value;}};
struct LLVOAvatar {enum VisualMuteSettings {AV_RENDER_NORMALLY,AV_DO_NOT_RENDER,AV_ALWAYS_RENDER};};
struct LLSD {std::map<std::string,LLSD> map;int value=0;using map_const_iterator=std::map<std::string,LLSD>::const_iterator;static LLSD emptyMap(){return{};}LLSD& operator[](const std::string& key){return map[key];}LLSD& operator=(int v){value=v;return *this;}int asInteger()const{return value;}auto beginMap()const{return map.begin();}auto endMap()const{return map.end();}};
struct LLFile {static bool isfile(const std::string& name){return std::filesystem::is_regular_file(name);}};
struct LLSDSerialize {static void fromXMLDocument(LLSD& data,std::istream& f){std::string key;int val;while(f>>key>>val)data[key]=val;}static void toPrettyXML(const LLSD& data,std::ostream& f){for(const auto& [key,val]:data.map)f<<key<<" "<<val.value<<"\n";}};
struct Signal {unsigned disconnected=0;void operator()(const LLUUID&,LLVOAvatar::VisualMuteSettings){}void disconnect_all_slots(){++disconnected;}};
class FSAvatarRenderPersistence {public:using avatar_render_setting_t=std::map<LLUUID,LLVOAvatar::VisualMuteSettings>;avatar_render_setting_t mAvatarRenderMap;std::string mAccountFile;Signal mAvatarRenderSettingChangedCallback;~FSAvatarRenderPersistence();void init();void retireAccount();void loadAvatarRenderSettings();void saveAvatarRenderSettings();LLVOAvatar::VisualMuteSettings getAvatarRenderSettings(const LLUUID&);void setAvatarRenderSettings(const LLUUID&,LLVOAvatar::VisualMuteSettings);};
METHODS
int main(){
 const LLUUID id("resident");
 {FSAvatarRenderPersistence service;service.init();service.setAvatarRenderSettings(id,LLVOAvatar::AV_DO_NOT_RENDER);service.init();assert(service.getAvatarRenderSettings(id)==LLVOAvatar::AV_DO_NOT_RENDER);
 directory.account="b";service.retireAccount();assert(service.mAvatarRenderMap.empty()&&service.mAccountFile.empty());assert(LLFile::isfile("a-avatar_render_settings.xml")&&!LLFile::isfile("b-avatar_render_settings.xml"));
 service.init();assert(service.getAvatarRenderSettings(id)==LLVOAvatar::AV_RENDER_NORMALLY);service.setAvatarRenderSettings(id,LLVOAvatar::AV_ALWAYS_RENDER);
 directory.account="a";service.init();assert(service.getAvatarRenderSettings(id)==LLVOAvatar::AV_DO_NOT_RENDER);assert(LLFile::isfile("b-avatar_render_settings.xml"));service.retireAccount();directory.account="unrelated";}
 assert(!LLFile::isfile("unrelated-avatar_render_settings.xml"));directory.account="b";FSAvatarRenderPersistence again;again.init();assert(again.getAvatarRenderSettings(id)==LLVOAvatar::AV_ALWAYS_RENDER);
}
