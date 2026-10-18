// Real shared EstateExperience decoder and controller publication.
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using S32=int;constexpr int UUID_BYTES=16;
struct LLUUID{char mData[16]={};};struct LLSD{std::vector<LLUUID>array;std::map<std::string,LLSD>map;static LLSD emptyArray(){return{};}static LLSD emptyMap(){return{};}void append(LLUUID id){array.push_back(id);}LLSD&with(const std::string&key,LLSD value){map[key]=value;return*this;}};
using sparam_t=std::vector<std::string>;struct LLDispatcher{};struct LLPanelRegionExperiences{int calls=0;LLSD last;void processResponse(LLSD value){++calls;last=value;}}panel;
struct LLFloaterRegionInfo{static LLPanelRegionExperiences*getPanelExperiences(){return &panel;}};
struct LLDispatchSetEstateExperience{static LLSD getIDs(sparam_t::const_iterator,sparam_t::const_iterator,S32);bool operator()(const LLDispatcher*,const std::string&,const LLUUID&,const sparam_t&);};
METHODS
int main(){LLDispatchSetEstateExperience decoder;sparam_t rows={"50","1","1","1","1",std::string(16,'a'),std::string(16,'b'),std::string(16,'c')};decoder(nullptr,"setexperience",{},rows);assert(panel.calls==1&&panel.last.map["blocked"].array[0].mData[0]=='a'&&panel.last.map["trusted"].array[0].mData[0]=='b'&&panel.last.map["allowed"].array[0].mData[0]=='c');
for(const std::string bad:{"-1","999999999999999999999999999","bad","","4"})for(int i=2;i<5;++i){auto broken=rows;broken[i]=bad;decoder(nullptr,"setexperience",{},broken);assert(panel.calls==1);}
auto broken=rows;broken[7]="short";decoder(nullptr,"setexperience",{},broken);assert(panel.calls==1);broken=rows;broken[4]="2";decoder(nullptr,"setexperience",{},broken);assert(panel.calls==1);broken.resize(4);decoder(nullptr,"setexperience",{},broken);assert(panel.calls==1);
sparam_t empty={"50","1","0","0","0"};decoder(nullptr,"setexperience",{},empty);assert(panel.calls==2&&panel.last.map["blocked"].array.empty()&&panel.last.map["trusted"].array.empty()&&panel.last.map["allowed"].array.empty());
assert(LLDispatchSetEstateExperience::getIDs(rows.begin()+5,rows.end(),-1).array.empty());assert(LLDispatchSetEstateExperience::getIDs(rows.begin()+5,rows.end(),4).array.empty());rows[7]="short";assert(LLDispatchSetEstateExperience::getIDs(rows.begin()+5,rows.end(),3).array.empty());
}
