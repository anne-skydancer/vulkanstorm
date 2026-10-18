#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <cassert>
using LLUUID=int;using sparam_t=std::vector<std::string>;using LLDispatcher=int;
struct Log{template<class T>Log&operator<<(const T&){return*this;}}testLog;
#define LL_WARNS(x) testLog
#define LL_DEBUGS(x) testLog
#define LL_INFOS(x) testLog
#define LL_ENDL 0
struct LLSD{std::map<std::string,LLSD>map;std::string text;int integer=0;bool boolean=false,mapping=false;LLSD&operator[](const std::string&key){mapping=true;return map[key];}bool has(const std::string&key)const{return map.contains(key);}bool isMap()const{return mapping;}std::string asString()const{return text;}int asInteger()const{return integer;}bool asBoolean()const{return boolean;}}message;
struct LLSDSerialize{static bool deserialize(LLSD&out,std::istream&,std::size_t){out=message;return true;}};
struct LLURI{static std::string unescape(const std::string&s){return s;}};
struct LLRect{int left,top,right,bottom;LLRect(int l,int t,int r,int b):left(l),top(t),right(r),bottom(b){}};
struct LLFloaterWebContent{struct Params{std::string url;bool trusted_content=false,show_page_title=true,allow_address_entry=true;struct Rect{LLRect value{0,0,0,0};void setValue(const LLRect&r){value=r;}}preferred_media_size;};};
struct LLFloater{unsigned closes=0;bool canClose=true;std::string title;void closeHostedFloater(){++closes;}void setCanClose(bool b){canClose=b;}void setTitle(const std::string&s){title=s;}};
struct LLFloaterReg{inline static LLFloater floater;inline static LLFloaterWebContent::Params params;inline static std::string shown;static LLFloater*findInstance(const char*){return&floater;}static void toggleInstanceOrBringToFront(const char*name,const LLFloaterWebContent::Params&p){shown=name;params=p;}static void showInstance(const char*name,const LLFloaterWebContent::Params&p){shown=name;params=p;}static bool isRegistered(const std::string&){return false;}};
CONSTANTS
struct LLUrlFloaterDispatchHandler{bool operator()(const LLDispatcher*,const std::string&,const LLUUID&,const sparam_t&);};
METHOD
int main(){LLUrlFloaterDispatchHandler handler;message[KEY_FLOATER].text=FLOATER_GUIDEBOOK;message[KEY_URL].text="https://example.test/guide";auto&p=message[KEY_PARAMS];p[KEY_TRUSTED_CONTENT].boolean=true;p[KEY_WIDTH].integer=799;p[KEY_HEGHT].integer=600;p[KEY_CAN_CLOSE].boolean=false;p[KEY_TITLE].text="Actual Guide";assert(handler(nullptr,"",0,{"wire"}));assert(LLFloaterReg::shown=="guidebook"&&LLFloaterReg::params.url=="https://example.test/guide"&&LLFloaterReg::params.trusted_content);assert(LLFloaterReg::params.preferred_media_size.value.top==600&&LLFloaterReg::params.preferred_media_size.value.right==799&&!LLFloaterReg::floater.canClose&&LLFloaterReg::floater.title=="Actual Guide");message={};message[KEY_ACTION].text=VALUE_OPEN_URL;auto&data=message[KEY_DATA];data[KEY_FLOATER].text=FLOATER_WEB_CONTENT;data[KEY_URL].text="https://example.test/content";data[KEY_PARAMS][KEY_TRUSTED_CONTENT].boolean=false;data[KEY_PARAMS][KEY_SHOW_PAGE_TITLE].boolean=false;data[KEY_PARAMS][KEY_ALLOW_ADRESS_ENTRY].boolean=false;assert(handler(nullptr,"",0,{"wire"}));assert(LLFloaterReg::shown=="web_content"&&LLFloaterReg::params.url=="https://example.test/content"&&!LLFloaterReg::params.trusted_content&&!LLFloaterReg::params.show_page_title&&!LLFloaterReg::params.allow_address_entry);message={};assert(!handler(nullptr,"",0,{"invalid"}));}
