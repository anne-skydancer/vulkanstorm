#include <cassert>
#include <memory>
#include <sstream>
#include <string>
constexpr int NVT_STRING=1;
struct LLNameValue {static inline unsigned interned=0;std::string data;explicit LLNameValue(const char* line):data(line){++interned;}int getTypeEnum()const{return NVT_STRING;}const char* getString()const{return data.c_str();}};
struct State {CURRENT};
int main(){
 for(const auto* valid:{"FirstName STRING RW SV Native","  LastName\tSTRING R SIM_VIEWER Tag","Title STRING READ_WRITE DSV A long group title","Title STRING READ_ONLY SIM_SPACE_VIEWER "})assert(State::parseNameValue(valid));
 assert(LLNameValue::interned==4);
 for(const auto* invalid:{"unknown STRING RW SV text","FirstName S32 RW SV 10","LastName STRING unknown SV text","Title STRING RW a_new_interned_token text","Title STRING","","   "})assert(!State::parseNameValue(invalid));
 assert(LLNameValue::interned==4); // no arbitrary-key/type/class intern side effects
}
