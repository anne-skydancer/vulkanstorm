namespace
{
struct ReferenceMenuSit
{
    LLViewerObject* value;
    bool valid()const{return value!=nullptr;}
    bool current()const{return true;}
    U8 pcode()const{return value->getPCode();}
    bool canSit(const LLVector3& offset)const{return RlvActions::canSit(value,offset);}
    bool avatarValid()const{return isAgentAvatarValid();}
    bool sitting()const{return gAgentAvatarp->isSitting();}
    const LLUUID& id()const{return value->mID;}
    void send()const{value->getRegion()->sendReliableMessage();}
};
struct NativeMenuSit
{
    const VSObjectContext& value;
    bool valid()const{return current();}
    bool current()const{return value.current && value.current();}
    U8 pcode()const{return value.pcode;}
    bool canSit(const LLVector3& offset)const{return RlvActions::canSit(value,offset);}
    bool avatarValid()const{return vs_avatar_character()!=nullptr;}
    bool sitting()const{return gAgent.isSitting();}
    const LLUUID& id()const{return value.id;}
    void send()const{if(current())gMessageSystem->sendReliable(value.host);}
};
}
