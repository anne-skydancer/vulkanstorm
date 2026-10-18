#include <algorithm>
#include <cassert>
#include <cctype>
#include <functional>
#include <string>
#include <vector>
using LLUUID=int;
constexpr char RLV_CMD_PREFIX='@';
constexpr int CHAT_TYPE_OWNER=8;
enum ERlvCmdRet{RLV_RET_UNKNOWN=0,RLV_RET_RETAINED=1,RLV_RET_SUCCESS=0x100,RLV_RET_SUCCESS_UNSET,RLV_RET_SUCCESS_DUPLICATE,RLV_RET_FAILED=0x200};
enum class VSOwnerChatResult{NotCommand,Consumed,Display};
#define RLV_ASSERT(v) assert(v)
unsigned flags;
std::vector<std::string> trace;
bool bit(unsigned n){return flags&(1u<<n);}
struct LLChat{int mFromID=7,mChatType=CHAT_TYPE_OWNER;std::string mText;};
struct rlv_handler_t{static bool isEnabled(){trace.push_back("enabled");return bit(0);}};
struct RlvSettings{
 static bool getEnableTemporaryAttachments(){trace.push_back("temporary");return bit(1);}
 static bool getDebug(){trace.push_back("debug");return bit(2);}
 static bool getDebugHideUnsetDup(){trace.push_back("hide");return bit(3);}
};
struct LLViewerObject{
 bool isAttachment(){trace.push_back("attachment");return bit(4);}
 bool isTempAttachment(){trace.push_back("temp attachment");return bit(5);}
};
struct LLStringUtil{static void toLower(std::string&s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return std::tolower(c);});}};
namespace boost{enum{drop_empty_tokens};template<class T>struct char_separator{char_separator(const char*,const char*,int){}};}
struct boost_tokenizer{
 std::vector<std::string>tokens;using iterator=std::vector<std::string>::iterator;
 boost_tokenizer(const std::string&s,boost::char_separator<char>){size_t a=0;while(a<s.size()){const auto b=s.find(',',a);auto t=s.substr(a,b-a);if(!t.empty())tokens.push_back(t);if(b==std::string::npos)break;a=b+1;}}
 iterator begin(){return tokens.begin();}iterator end(){return tokens.end();}
};
struct Handler{
 ERlvCmdRet processCommand(int id,const std::string&s,bool){trace.push_back(std::to_string(id)+":"+s);if(s[0]=='f')return RLV_RET_FAILED;if(s[0]=='r')return RLV_RET_RETAINED;if(s[0]=='u')return RLV_RET_SUCCESS_UNSET;if(s[0]=='d')return RLV_RET_SUCCESS_DUPLICATE;return RLV_RET_SUCCESS;}
}gRlvHandler;
struct RlvForceWear{static bool instanceExists(){trace.push_back("wear exists");return bit(6);}static RlvForceWear&instance(){static RlvForceWear w;return w;}void done(){trace.push_back("wear done");}};
struct RlvStrings{static const char*getStringFromReturnCode(ERlvCmdRet r){trace.push_back("suffix:"+std::to_string(r));return bit(7)?"suffix":nullptr;}};
REFERENCE_POLICY
CURRENT_POLICY
int main(){
 LLViewerObject object;
 const std::vector<std::string>texts{"", "hi", "@a", "normal", "@SUCCEED", "@SUCCEED,FAIL,RETAIN", "@UNSET,DUPLICATE", "@,,FAIL,,", "@RETAIN", "@FAIL", "@SUCCEED,SUCCEED", "@,,,"};
 for(flags=0;flags<256;++flags)for(const auto&input:texts)for(bool exists:{false,true})for(int type:{1,CHAT_TYPE_OWNER}){
  LLViewerObject*chatter=exists?&object:nullptr;
  LLChat expected;expected.mChatType=type;auto actual=expected;auto original=input,text=input;
  trace.clear();const auto result=reference(expected,original,chatter);const auto events=trace;
  trace.clear();const auto current=vs_process_owner_chat(actual,text,[chatter]{return !chatter||!chatter->isAttachment()||!chatter->isTempAttachment()||RlvSettings::getEnableTemporaryAttachments();}, {});
  assert(current==result&&text==original&&actual.mText==expected.mText&&trace==events);
 }
 flags=1;LLChat chat;std::string text="@SUCCEED,FAIL";trace.clear();int checks=0;
 assert(vs_process_owner_chat(chat,text,[]{return true;},[&]{++checks;return false;})==VSOwnerChatResult::Consumed);
 assert(checks==1&&std::count(trace.begin(),trace.end(),"7:succeed")==1);
 assert(std::find(trace.begin(),trace.end(),"7:fail")==trace.end());
 assert(std::find(trace.begin(),trace.end(),"wear exists")==trace.end());
}
