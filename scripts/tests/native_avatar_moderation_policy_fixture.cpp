// Frozen original authority policy compared with the shared peer controller.
#include <cassert>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
struct LLUUID{int value=0;bool operator==(LLUUID b)const{return value==b.value;}bool operator!=(LLUUID b)const{return value!=b.value;}};using uuid_vec_t=std::vector<LLUUID>;constexpr int GP_LAND_ADMIN=1;
struct LLVector3d{double x=0;};struct LLVector3{};
std::vector<std::string>calls;
struct LLParcel{LLUUID owner{8};LLUUID getOwnerID()const{calls.push_back("parcel-owner");return owner;}};
struct LLViewerRegion{int id=0;bool owner=false,manager=false,self=false,group=false;LLUUID getOwner()const{calls.push_back("owner"+std::to_string(id));return{owner?7:9};}bool isEstateManager()const{calls.push_back("estate"+std::to_string(id));return manager;}LLVector3 getPosRegionFromGlobal(LLVector3d)const{return{};}bool isOwnedSelf(LLVector3)const{calls.push_back("self"+std::to_string(id));return self;}bool isOwnedGroup(LLVector3)const{calls.push_back("group"+std::to_string(id));return group;}};
std::map<int,LLViewerRegion>regions;bool parcelExists=true,groupPower=false;
bool getRegionAndPosGlobalFromAgentID(LLUUID id,const LLViewerRegion**region,LLVector3d*position){calls.push_back("resolve"+std::to_string(id.value));auto i=regions.find(id.value);if(i==regions.end())return false;if(region)*region=&i->second;if(position)position->x=id.value;return true;}
bool getRegionFromAgentID(LLUUID id,const LLViewerRegion**region){return getRegionAndPosGlobalFromAgentID(id,region,nullptr);}
struct VSAvatarContext{const LLViewerRegion*region=nullptr;LLVector3d point;static std::optional<VSAvatarContext>resolve(LLUUID id){const LLViewerRegion*r=nullptr;LLVector3d p;if(!getRegionAndPosGlobalFromAgentID(id,&r,&p))return{};return VSAvatarContext{r,p};}bool current()const{return region;}LLVector3d position()const{return point;}LLUUID getOwner()const{return region->getOwner();}bool isEstateManager()const{return region->isEstateManager();}bool isOwnedSelf()const{return region->isOwnedSelf({});}bool isOwnedGroup()const{return region->isOwnedGroup({});}};
struct Selection{LLParcel*getParcel(){static LLParcel p;return parcelExists?&p:nullptr;}};
struct LLViewerParcelMgr{static LLViewerParcelMgr*getInstance(){static LLViewerParcelMgr p;return&p;}std::shared_ptr<Selection>selectParcelAt(LLVector3d p){calls.push_back("select"+std::to_string(int(p.x)));return std::make_shared<Selection>();}};
struct Agent{bool god=false;LLUUID getID(){return{7};}bool isGodlikeWithoutAdminMenuFakery(){return god;}bool hasPowerInGroup(LLUUID id,int power){calls.push_back("group-power");assert(id.value==8&&power==GP_LAND_ADMIN);return groupPower;}}gAgent;
struct LLAvatarActions{static bool canLandFreezeOrEjectMultiple(uuid_vec_t&,bool);static bool canEstateKickOrTeleportHomeMultiple(uuid_vec_t&,bool);};struct GoldAvatarActions{static bool canLandFreezeOrEjectMultiple(uuid_vec_t&,bool);static bool canEstateKickOrTeleportHomeMultiple(uuid_vec_t&,bool);};
bool GoldAvatarActions::canLandFreezeOrEjectMultiple(uuid_vec_t& idAgents, bool fFilter /*=false*/)
{
    if (gAgent.isGodlikeWithoutAdminMenuFakery())
        return true;                    // Gods can always freeze

    uuid_vec_t::iterator itAgent = idAgents.begin(); bool fCanFreeze = false;
    while ( (itAgent != idAgents.end()) && ((fFilter) || (!fCanFreeze)) )
    {
        const LLViewerRegion* pRegion = NULL; LLVector3d posGlobal;
        if (getRegionAndPosGlobalFromAgentID(*itAgent, &pRegion, &posGlobal))
        {
            // NOTE: we actually don't always need the parcel, but attempting to get it now will help with setting fBanEnabled when ejecting
            const LLParcel* pParcel = LLViewerParcelMgr::getInstance()->selectParcelAt(posGlobal)->getParcel();
            const LLVector3 posRegion = pRegion->getPosRegionFromGlobal(posGlobal);
            if ( (pRegion->getOwner() == gAgent.getID()) || (pRegion->isEstateManager()) || (pRegion->isOwnedSelf(posRegion)) ||
                 ((pRegion->isOwnedGroup(posRegion)) && (pParcel) && (gAgent.hasPowerInGroup(pParcel->getOwnerID(), GP_LAND_ADMIN))) )
            {
                fCanFreeze = true;
                ++itAgent;
                continue;
            }
        }
        if (fFilter)
            itAgent = idAgents.erase(itAgent);
        else
            ++itAgent;
    }
    return fCanFreeze;
}
bool GoldAvatarActions::canEstateKickOrTeleportHomeMultiple(uuid_vec_t& idAgents, bool fFilter /*=false*/)
{
    if (gAgent.isGodlikeWithoutAdminMenuFakery())
        return true;        // Gods can always kick

    uuid_vec_t::iterator itAgent = idAgents.begin(); bool fCanKick = false;
    while ( (itAgent != idAgents.end()) && ((fFilter) || (!fCanKick)) )
    {
        const LLViewerRegion* pRegion = NULL;
        if ( (getRegionFromAgentID(*itAgent, &pRegion)) && ((pRegion->getOwner() == gAgent.getID()) || (pRegion->isEstateManager())) )
        {
            fCanKick = true;
            ++itAgent;      // Estate owners/managers can kick
            continue;
        }

        if (fFilter)
            itAgent = idAgents.erase(itAgent);
        else
            ++itAgent;
    }
    return fCanKick;
}
METHODS
int main(){for(unsigned mask=0;mask<256;++mask)for(bool god:{false,true})for(bool filter:{false,true}){
gAgent.god=god;groupPower=mask&16;parcelExists=mask&32;regions.clear();for(int id=1;id<=3;++id){unsigned bits=mask>>(id-1);regions[id]={id,bool(bits&1),bool(bits&2),bool(bits&4),bool(bits&8)};}
for(bool estate:{false,true}){uuid_vec_t oldIDs={{1},{99},{2},{3}},newIDs=oldIDs;calls.clear();bool original=estate?GoldAvatarActions::canEstateKickOrTeleportHomeMultiple(oldIDs,filter):GoldAvatarActions::canLandFreezeOrEjectMultiple(oldIDs,filter);auto expected=calls;calls.clear();bool actual=estate?LLAvatarActions::canEstateKickOrTeleportHomeMultiple(newIDs,filter):LLAvatarActions::canLandFreezeOrEjectMultiple(newIDs,filter);assert(actual==original&&newIDs==oldIDs&&calls==expected);}
}}
