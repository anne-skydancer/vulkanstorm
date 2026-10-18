#include <cassert>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>
using LLUUID=int;
constexpr int CHAT_TYPE_START=4,CHAT_TYPE_STOP=5,CHAT_TYPE_OWNER=8,CHAT_TYPE_DIRECT=9,CHAT_TYPE_RADAR=12;
constexpr int CHAT_SOURCE_AGENT=1,CHAT_SOURCE_OBJECT=2;
constexpr int RLV_BHVR_RECVCHAT=0,RLV_BHVR_RECVCHATFROM=1,RLV_BHVR_RECVEMOTE=2,RLV_BHVR_RECVEMOTEFROM=3;
std::vector<int> trace;
std::uint32_t flags;
bool bit(unsigned b){return bool(flags&(1u<<b));}
struct LLVector3{int value=3;};
struct LLChat{int mFromID=2,mOwnerID=0,mSourceType=1,mChatType=1;std::string mFromName="name",mURL;bool mRlvNamesFiltered=false;LLVector3 mPosAgent;};
struct LLAvatarName{std::string name="cached";};
struct LLAvatarNameCache{static bool get(int,LLAvatarName*){trace.push_back(40);return bit(14);}};
struct rlv_handler_t{static bool isEnabled(){trace.push_back(1);return bit(0);}};
struct Agent{int getID(){trace.push_back(2);return 1;}}gAgent;
struct RlvUtil{
 static bool isEmote(const std::string&s){trace.push_back(3);return s.rfind("/me ",0)==0;}
 static void filterNames(std::string&s){trace.push_back(4);s="filtered "+s;}
};
struct Settings{}gSavedSettings;
template<class T>struct LLCachedControl{LLCachedControl(Settings&,const char*){}operator bool()const{trace.push_back(5);return bit(3);}};
struct Handler{
 bool hasBehaviour(int b){trace.push_back(10+b);return bit(4+b);}
 bool isException(int b,int){trace.push_back(20+b);return bit(8+b);}
 bool filterChat(std::string&s,bool){trace.push_back(30);s="...";return true;}
}gRlvHandler;
struct RlvActions{
 static constexpr int SNC_DEFAULT=0;
 static bool canShowName(int){trace.push_back(31);return bit(12);}
 static bool canShowName(int,int id){trace.push_back(32+id);return id==2?bit(13):bit(15);}
 static bool canShowLocation(){trace.push_back(39);return bit(16);}
};
struct RlvStrings{static std::string getAnonym(const LLAvatarName&v){trace.push_back(41);return "anon "+v.name;}static std::string getAnonym(const std::string&s){trace.push_back(42);return "anon "+s;}};
struct Value{std::string text;Value&operator=(const std::string&v){text=v;return *this;}Value&operator=(int v){text=std::to_string(v);return *this;}Value&operator=(bool v){text=v?"true":"false";return *this;}};
struct LLSD{std::map<std::string,Value>data;Value&operator[](const char*k){return data[k];}};
struct LLURI{static std::string mapToQueryString(const LLSD&s){std::string out;for(const auto&[k,v]:s.data)out+=k+"="+v.text+"&";return out;}};
struct LLSLURL{
 std::string value;
 LLSLURL(const std::string&name,const LLVector3&p):value(name+"/"+std::to_string(p.value)){}
 LLSLURL(const char*name,int id,const std::string&q):value(std::string(name)+"/"+std::to_string(id)+"?"+q){}
 std::string getLocationString()const{return value;}
 std::string getSLURLString()const{return value;}
};
struct LLViewerObject{
 bool isAttachment(){trace.push_back(43);return bit(1);}
 bool permYouOwner(){trace.push_back(44);return bit(2);}
};
struct LLViewerRegion{std::string getName()const{trace.push_back(45);return "region";}};
struct LLWorld{
 static LLWorld*getInstance(){static LLWorld w;return &w;}
 const LLViewerRegion*getRegionFromPosAgent(LLVector3){trace.push_back(46);static LLViewerRegion r;return bit(17)?&r:nullptr;}
};
struct VSReceivedChatContext{
 struct Source{bool attachment=false,owned=false;};
 std::function<Source()>source;
 std::function<std::optional<std::string>()>location;
};
REFERENCE_POLICY
CURRENT_POLICY
int main(){
 LLViewerObject object;
 for(std::uint32_t mask=0;mask<(1u<<18);++mask){
  flags=mask;
  for(int source=0;source<6;++source)for(int self=0;self<2;++self){
   LLChat expected;expected.mSourceType=source;expected.mFromID=self?1:2;
   expected.mChatType=int((mask>>3)%13);
   LLChat actual=expected;
   std::string text=(mask&128)?"/me waves":"hello",referenceText=text;
   LLViewerObject*chatter=(mask&256)?&object:nullptr;
   trace.clear();const bool accepted=reference(expected,referenceText,chatter,3);
   const auto originalTrace=trace;
   trace.clear();
   VSReceivedChatContext context{
    [chatter]{return VSReceivedChatContext::Source{chatter&&chatter->isAttachment(),chatter&&chatter->permYouOwner()};},
    [&actual]()->std::optional<std::string>{const auto*r=LLWorld::getInstance()->getRegionFromPosAgent(actual.mPosAgent);if(!r)return {};return LLSLURL(r->getName(),actual.mPosAgent).getLocationString();}};
   const bool admitted=vs_filter_received_chat(actual,text,3,context);
   assert(admitted==accepted&&text==referenceText);
   assert(actual.mFromName==expected.mFromName&&actual.mURL==expected.mURL&&actual.mRlvNamesFiltered==expected.mRlvNamesFiltered);
   assert(trace==originalTrace);
  }
 }
}
