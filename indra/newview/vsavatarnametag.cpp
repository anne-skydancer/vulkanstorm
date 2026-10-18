// Native avatar presentation. Shared tag policy, real character joints. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsavatarnametag.h"
#include "llvoavatar.h"
#include "llavatarappearance.h"
#include "llhudnametag.h"
#include "llnamevalue.h"
#include "llchat.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewerwindow.h"
#include "llviewernetwork.h"
#include "llavatarnamecache.h"
#include "llcacheName.h"
#include "llcallingcard.h"
#include "llmutelist.h"
#include "lltrans.h"
#include "llresmgr.h"
#include "llcriticaldamp.h"
#include "llanimationstates.h"
#include "lggcontactsets.h"
#include "lfsimfeaturehandler.h"
#include "llnetmap.h"
#include "fsdata.h"
#include "fscommon.h"
#include "rlvactions.h"
#include "rlvhandler.h"
#include "rlvmodifiers.h"
#include <sstream>
#include <limits>
#include "vsavatarnametagpolicy.h"

struct VSAvatarNameTag::State : std::enable_shared_from_this<State>
{
    Frame frame;
    struct Residency {std::shared_ptr<const void> owner,names;};
    std::shared_ptr<Residency> residency;
    Reserve reserve;
    LLPointer<LLHUDNameTag> mNameText;
    LLFrameTimer mChatTimer,mTimeVisible;
    LLFrameTimer& mTypingTimer;
    bool& mTyping;
    std::deque<LLChat> mChats;
    bool mVisibleChat=false,mVisibleTyping=false;
    bool mNameIsSet=false,mNameAway=false,mNameDoNotDisturb=false,mNameAutoResponse=false,
        mNameIsTyping=false,mNameMute=false,mNameAppearance=false,mNameFriend=false,
        mNameCloud=false,mRenderGroupTitles=true;
    F32 mAppAngle=0,mAttachmentSurfaceArea=0,mPelvisToFoot=0,mWaterHeight=0;
    U32 mVisualComplexity=0,mNameArc=0;
    S32 mLastRezzedStatus=0;
    LLSD mClientTagData;
    LLColor4 mNameColor,mNameArcColor;
    std::string mNameFirstname,mNameLastname,mDistanceString,mTitle,rawNames;
    LLSD rawClientTag;
    LLVector3 voicePosition;
    LLVector3 mAvatarOffset,mBodySize,mCurRootToHeadOffset,mTargetRootToHeadOffset,mAgentOffset;
    LLJoint* mRoot=nullptr;
    LLJoint* mHeadp=nullptr;
    const std::map<LLUUID,S32>& mSignaledAnimations;
    State(const LLUUID& id,const std::map<LLUUID,S32>& animations,bool& typing,LLFrameTimer& timer,std::shared_ptr<const void> admitted,Reserve budget):
        residency(std::make_shared<Residency>(Residency{std::move(admitted),{}})),reserve(std::move(budget)),mTypingTimer(timer),mTyping(typing),mSignaledAnimations(animations){frame.id=id;}
    std::map<std::string,std::unique_ptr<LLNameValue>> values;
    bool isSelf() const{return frame.self;}
    const LLUUID& getID() const{return frame.id;}
    const LLVector3d& getPositionGlobal() const{return frame.globalPosition;}
    bool isInMuteList() const{return LLMuteList::instance().isMuted(frame.id);}
    bool isBuddy() const{return LLAvatarTracker::instance().isBuddy(frame.id);}
    bool getHasMissingParts() const{return frame.missingParts;}
    bool isTooComplex() const{return frame.tooComplex;}
    LLNameValue* getNVPair(const char* name){const auto i=values.find(name);return i==values.end()?nullptr:i->second.get();}
    void debugAvatarRezTime(const char* notification,const char* reason)
    {LL_DEBUGS("AvatarRez")<<notification<<" "<<frame.id<<" "<<reason<<LL_ENDL;}
    LLColor4 getNameTagColor(){return VSAvatarNameTagPolicy::color(*this);}
    void clearNameTag(){VSAvatarNameTagPolicy::clear(*this);}
    void idleUpdateNameTagText(bool changed){VSAvatarNameTagPolicy::text(*this,changed);}
    void addNameTagLine(const std::string& line,const LLColor4& color,S32 style,const LLFontGL* font,
        bool ellipses=false,bool name=false)
    {VSAvatarNameTagPolicy::addLine(*this,line,color,style,font,ellipses,name);}
    bool canShowNameTag() const
    {
        if(!gRlvHandler.hasBehaviour(RLV_BHVR_SHOWNAMETAGS)||
            gRlvHandler.isException(RLV_BHVR_SHOWNAMETAGS,frame.id)||frame.id==gAgentID)return true;
        const F32 distance=RlvBehaviourDictionary::instance().getModifier(RLV_MODIFIER_SHOWNAMETAGSDIST)->getValue<F32>();
        return distance!=0.f&&dist_vec_squared(frame.globalPosition,gAgent.getPositionGlobal())<distance*distance;
    }
    static std::unique_ptr<LLNameValue> parseNameValue(const std::string& line)
    {
        std::istringstream fields(line);
        std::string key,type,access,send;
        if(!(fields>>key>>type>>access>>send))return {};
        if((key!="Title"&&key!="FirstName"&&key!="LastName")||type!="STRING")return {};
        if(access!="R"&&access!="RW"&&access!="READ_ONLY"&&access!="READ_WRITE")return {};
        // Only bounded known tokens may reach LLNameValue's global intern table.
        if(send!="S"&&send!="DS"&&send!="SV"&&send!="DSV"&&send!="SIM"&&send!="SIM_SPACE"&&send!="SIM_VIEWER"&&send!="SIM_SPACE_VIEWER")return {};
        auto value=std::make_unique<LLNameValue>(line.c_str());
        return value->getTypeEnum()==NVT_STRING&&value->getString()?std::move(value):nullptr;
    }
    void bindNameTag()
    {
        const std::weak_ptr<State> weak=shared_from_this();
        mNameText->setNativeSource([weak]()->std::optional<LLHUDNameTag::NativeSource>
        {
            const auto state=weak.lock();
            if(!state||!state->frame.current||!(*state->frame.current)())return {};
            return LLHUDNameTag::NativeSource{state->frame.id,state->frame.position,state->frame.radius};
        },residency);
    }
    void retire()
    {
        if(mNameText){mNameText->markDead();mNameText=nullptr;--LLVOAvatar::sNumVisibleChatBubbles;}
        frame.current={};
    }
    ~State(){retire();}
};
VSAvatarNameTag::VSAvatarNameTag(const LLUUID& id,const std::map<LLUUID,S32>& animations,bool& typing,LLFrameTimer& timer,std::shared_ptr<const void> residency,Reserve reserve):
    mState(std::make_shared<State>(id,animations,typing,timer,std::move(residency),std::move(reserve))){}
std::size_t VSAvatarNameTag::allocationBytes()
{
    // Bound raw NV values, display/title/status labels, UTF-32 lines, the
    // original 12/DB_CHAT_MSG_STR_LEN bubble history and font-width maps.
    return sizeof(State)+sizeof(LLHUDNameTag)+96*1024;
}
VSAvatarNameTag::~VSAvatarNameTag(){mState->retire();}
void VSAvatarNameTag::invalidate(){mState->clearNameTag();}
void VSAvatarNameTag::suspend(){mState->retire();}
void VSAvatarNameTag::update(Frame frame,std::string_view nameValues)
{
    auto& state=*mState;
    if(!frame.current||!(*frame.current)()||!frame.appearance||!frame.head||frame.id.isNull())
    {state.retire();return;}
    const bool different=state.frame.id!=frame.id;
    if(different){state.retire();state.mChats.clear();state.mTyping=false;state.values.clear();state.rawNames.clear();}
    state.frame=std::move(frame);
    const auto& f=state.frame;
    if(!f.visible)state.mTimeVisible.reset();
    if(state.rawNames!=nameValues)
    {
        // A name value can span the whole wire field. Charge its owned UTF-8,
        // UTF-32 and parsed/formatted copies before replacing displayed text.
        if(nameValues.size()>(std::numeric_limits<std::size_t>::max()-512)/16){state.retire();return;}
        auto nameReservation=state.reserve(nameValues.size()*16+512);
        if(!nameReservation){state.retire();return;}
        state.values.clear();state.rawNames=nameValues;state.residency->names=std::move(nameReservation);
        std::istringstream names(state.rawNames);std::string line;
        while(std::getline(names,line))
        {
            auto value=State::parseNameValue(line);
            if(value&&value->mName)state.values[value->mName]=std::move(value);
        }
        state.clearNameTag();
    }
    state.mRoot=f.appearance->mRoot;state.mHeadp=f.head;
    state.mBodySize=f.appearance->mBodySize;state.mAvatarOffset=f.appearance->mAvatarOffset;
    state.mPelvisToFoot=f.appearance->getPelvisToFoot();state.mWaterHeight=f.waterHeight;
    state.mAgentOffset=f.agentOffset;
    state.mAppAngle=f.angularExtent;state.mVisualComplexity=f.complexity;
    state.mAttachmentSurfaceArea=f.attachmentArea;state.mLastRezzedStatus=f.rezStatus;
    if(state.rawClientTag!=f.clientTag){state.rawClientTag=f.clientTag;state.mClientTagData=f.clientTag;state.clearNameTag();}
    // Exactly the same smoothed head/root/legacy/underwater policy as OpenGL.
    state.voicePosition=VSAvatarNameTagPolicy::position(state,state.mRoot->getLastWorldPosition()+state.mAgentOffset);
    VSAvatarNameTagPolicy::update(state,state.voicePosition);
}
void VSAvatarNameTag::chat(const LLChat& chat)
{
    auto& state=*mState;if(state.frame.current&&!(*state.frame.current)())return;
    state.mChats.push_back(chat);std::size_t length=0;
    for(const auto& item:state.mChats)length+=item.mText.size();
    while((length>DB_CHAT_MSG_STR_LEN||state.mChats.size()>12)&&!state.mChats.empty())
    {length-=state.mChats.front().mText.size();state.mChats.pop_front();}
    state.mChatTimer.reset();
}
void VSAvatarNameTag::typing(bool active)
{if(active)mState->mTypingTimer.reset();mState->mTyping=active;}
bool VSAvatarNameTag::intersect(const LLVector4a& start,const LLVector4a& end,LLVector4a& hit) const
{return mState->mNameText&&mState->frame.current&&(*mState->frame.current)()&&mState->mNameText->lineSegmentIntersect(start,end,hit);}

void VSAvatarNameTag::refreshPreferences(S32 simulatorLimit)
{
    // Same CPU preference/simulator clamp as display(), without creating its
    // GL world. Both renderer peers consume the actual shared tag policy.
    static LLCachedControl<S32> mode(gSavedSettings,"AvatarNameTagMode",1);
    static LLCachedControl<bool> titles(gSavedSettings,"NameTagShowGroupTitles",true);
    LLVOAvatar::sRenderName=mode>simulatorLimit?simulatorLimit:S32(mode);
    LLVOAvatar::sRenderGroupTitles=titles&&LLVOAvatar::sRenderName>0;
}

std::optional<LLVector3> VSAvatarNameTag::voicePosition() const
{const auto& s=*mState;return s.frame.current&&(*s.frame.current)()?std::optional<LLVector3>(s.voicePosition):std::nullopt;}
