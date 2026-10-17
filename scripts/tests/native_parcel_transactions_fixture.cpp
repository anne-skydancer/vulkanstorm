#include <cassert>
#include <string>
#include <map>
#include <vector>
#include <optional>
#include <functional>
using S32=int;using F32=float;using LLHost=int;
struct LLUUID{int value=0;bool operator==(LLUUID b)const{return value==b.value;}bool operator!=(LLUUID b)const{return !(*this==b);}bool isNull()const{return !value;}};
struct LLVector3d{};
bool available=true,current=true,allowed=true;
struct VSRegionContext{LLHost getHost()const{return 99;}};
std::optional<VSRegionContext>vs_region_context(const LLVector3d&){return available&&current?std::optional<VSRegionContext>(VSRegionContext{}):std::nullopt;}
struct Agent{LLUUID id{1},session{2},group{3};LLUUID getID()const{return id;}LLUUID getSessionID()const{return session;}LLUUID getGroupID()const{return group;}}gAgent;
struct LLParcel{int local=5,price=200,area=512;int getLocalID()const{return local;}int getSalePrice()const{return price;}int getArea()const{return area;}};
struct LLNotificationsUtil{static void add(const char*){}};
struct Wire{std::string name,block;std::map<std::string,double>numbers;int host=0;};
struct LLMessageSystem{std::vector<Wire>sent;Wire draft;
 void newMessage(const char*name){draft={};draft.name=name;}void nextBlock(const char*block){draft.block=block;}
 void addUUID(const char*name,LLUUID value){draft.numbers[draft.block+"/"+name]=value.value;}
 void addBOOL(const char*name,bool value){draft.numbers[draft.block+"/"+name]=value;}
 void addS32(const char*name,int value){draft.numbers[draft.block+"/"+name]=value;}
 void addF32(const char*name,float value){draft.numbers[draft.block+"/"+name]=value;}
 void sendReliable(int host){draft.host=host;sent.push_back(draft);}
}message,*gMessageSystem=&message;
struct LLViewerParcelMgr{
 class ParcelBuyInfo;bool mSelected=true;LLParcel* mCurrentParcel=nullptr;LLVector3d mWestSouth;int deselects=0;
 bool canAgentBuyParcel(LLParcel*,bool)const{return allowed;}void deselectLand(){mSelected=false;++deselects;}
 void sendParcelBuy(ParcelBuyInfo*);static void deleteParcelBuy(ParcelBuyInfo**);void sendParcelDeed(const LLUUID&);void sendParcelRelease();
};
METHODS
int main(){
 LLViewerParcelMgr manager;LLParcel parcel;manager.mCurrentParcel=&parcel;
 auto*info=new LLViewerParcelMgr::ParcelBuyInfo;info->mAgent={1};info->mSession={2};info->mGroup={3};info->mIsGroupOwned=false;info->mRemoveContribution=true;info->mIsClaim=false;info->mCurrent=[] {return current;};info->mHost=99;info->mParcelID=5;info->mPrice=200;info->mArea=512;
 manager.sendParcelBuy(info);assert(message.sent.size()==1);auto wire=message.sent.back();assert(wire.name=="ParcelBuy"&&wire.host==99);
 const std::map<std::string,double>buy={{"AgentData/AgentID",1},{"AgentData/SessionID",2},{"Data/GroupID",3},{"Data/IsGroupOwned",0},{"Data/RemoveContribution",1},{"Data/LocalID",5},{"Data/Final",1},{"ParcelData/Price",200},{"ParcelData/Area",512}};assert(wire.numbers==buy);
 auto refused=[&]{manager.sendParcelBuy(info);assert(message.sent.size()==1);};
 current=false;refused();current=true;gAgent.session={4};refused();gAgent.session={2};gAgent.id={4};refused();gAgent.id={1};
 parcel.price=201;refused();parcel.price=200;parcel.area=516;refused();parcel.area=512;parcel.local=6;refused();parcel.local=5;
 allowed=false;refused();allowed=true;info->mIsGroupOwned=true;gAgent.group={4};refused();gAgent.group={3};
 info->mIsClaim=true;info->mWest=0;info->mSouth=4;info->mEast=16;info->mNorth=32;manager.sendParcelBuy(info);assert(message.sent.size()==2);wire=message.sent.back();assert(wire.name=="ParcelClaim"&&wire.host==99);
 assert(wire.numbers.at("ParcelData/West")==0&&wire.numbers.at("ParcelData/South")==4&&wire.numbers.at("ParcelData/East")==16&&wire.numbers.at("ParcelData/North")==32&&wire.numbers.at("Data/Final")==1);
 assert(!wire.numbers.count("ParcelData/Price")&&!wire.numbers.count("Data/LocalID")&&!wire.numbers.count("Data/RemoveContribution"));
 manager.deleteParcelBuy(&info);assert(!info);manager.deleteParcelBuy(&info);
 manager.sendParcelDeed({3});assert(message.sent.size()==3&&message.sent.back().name=="ParcelDeedToGroup"&&message.sent.back().host==99&&message.sent.back().numbers.at("Data/LocalID")==5);
 manager.sendParcelDeed({0});assert(message.sent.size()==3);available=false;manager.sendParcelDeed({3});manager.sendParcelRelease();assert(message.sent.size()==3);available=true;
 manager.sendParcelRelease();assert(message.sent.size()==4&&message.sent.back().name=="ParcelRelease"&&message.sent.back().numbers.at("Data/LocalID")==5&&manager.deselects==1&&!manager.mSelected);
 manager.sendParcelRelease();assert(message.sent.size()==4);
}
