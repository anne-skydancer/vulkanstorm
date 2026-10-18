// Transpose LLAgentCamera + LLHUDEffect LookAt/PointAt CPU and wire semantics.
// Targets are real scene handles, never synthetic legacy objects. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsobjectattention.h"
#include "vsattentionpolicy.h"
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#include "llviewerwindow.h"
#include "vsworldavatar.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llviewercontrol.h"
#include "llhudeffectlookat.h"
#include "llhudeffectpointat.h"
#include "llhudobject.h"
#include "llcharacter.h"
#include "lljoint.h"
#include "llanimationstates.h"
#include "vsavatarmotions.h"
#include "llprimitive.h"
#include "llframetimer.h"
#include "llregionhandle.h"
#include "message.h"
#include <cmath>
namespace
{
VSWorldScene* attentionScene()
{
    return gViewerWindow && gViewerWindow->isNativeVulkan() && gViewerWindow->nativeContext()?
        &gViewerWindow->nativeContext()->world():nullptr;
}
bool sameTarget(const std::optional<VSWorldScene::ObjectHandle>& left,const std::optional<VSWorldScene::ObjectHandle>& right)
{
    if(bool(left)!=bool(right))return false;
    if(!left)return true;
    return left->account==right->account && left->accountGeneration==right->accountGeneration && left->region==right->region &&
        left->host==right->host && left->id==right->id && left->local==right->local && left->generation==right->generation && left->kind==right->kind;
}
}
struct VSObjectAttention::Impl
{
    struct Effect
    {
        LLUUID id;
        U8 type=0;
        std::optional<VSWorldScene::ObjectHandle> target;
        LLVector3d offset;
        LLVector3 lastSent;
        F32 duration=1.f,killTime=0,lastSend=0;
        U64 sourceGeneration=0;
        bool needsSend=true,dead=false,active=false;
        Effect(){id.generate();}
    };
    std::weak_ptr<VSNativeSession> session;
    VSNativeSession::AccountHandle account;
    std::function<bool()> current;
    Effect look,point;
    F64 epoch=LLFrameTimer::getTotalSeconds();
    bool male=false,retired=false;
    explicit Impl(std::weak_ptr<VSNativeSession> owner):session(std::move(owner))
    {
        if(const auto locked=session.lock()){account=locked->account();current=locked->accountGuard();}
    }
    bool alive()const{return !retired && current && current();}
    F32 time()const{return F32(LLFrameTimer::getTotalSeconds()-epoch);}
    std::optional<VSWorldScene::ObjectHandle> source()const
    {
        auto* world=attentionScene();
        return alive() && world?world->object(account.agent):std::nullopt;
    }
    VSWorldAvatar* avatar()const
    {
        auto* world=attentionScene();const auto self=source();
        return world && self?world->avatarForObject(self->id):nullptr;
    }
    static void clear(Effect& effect)
    {
        effect.target.reset();effect.offset.clearVec();effect.type=0;
    }
    std::optional<LLVector3d> targetPosition(bool isPoint)
    {
        if(!alive())return {};
        auto* world=attentionScene();
        auto& effect=isPoint?point:look;
        if(!world || !effect.type || effect.dead)return {};
        if(!effect.target)return effect.offset.isFinite()?std::optional<LLVector3d>(effect.offset):std::nullopt;
        if(!world->current(*effect.target))return {};
        const auto origin=gAgent.getPosGlobalFromAgent(LLVector3::zero);
        const auto focus=world->focusObject(*effect.target,origin);
        if(!focus || !focus->current())return {};
        LLVector3 position=focus->position();
        LLQuaternion rotation=focus->rotation();
        LLVector3 offset(effect.offset);
        if(focus->avatar())
        {
            const auto targetAvatar=world->avatarForObject(effect.target->id);
            const auto owner=session.lock();
            const auto region=owner?owner->regionSnapshot(effect.target->host,false):std::nullopt;
            if(!targetAvatar || !region || !owner->current(*region))return {};
            position=targetAvatar->headPosition()+LLVector3(from_region_handle(region->handle)-origin);
            auto& character=targetAvatar->character();
            if(!isPoint && effect.target->id==account.agent && effect.offset.magVecSquared()<.0001f)
            {
                effect.offset.setVec(1.,0.,0.);offset.setVec(effect.offset);
            }
            if(!isPoint && (effect.type==LOOKAT_TARGET_MOUSELOOK || effect.type==LOOKAT_TARGET_FREELOOK))rotation=LLQuaternion::DEFAULT;
            else
            {
                const bool pelvis=isPoint || (effect.target->id==account.agent && gAgentCamera.cameraCustomizeAvatar());
                const auto joint=pelvis?character.getJoint("mPelvis"):character.getRootJoint();
                if(!joint)return {};
                rotation=joint->getWorldRotation();
            }
        }
        const auto result=gAgent.getPosGlobalFromAgent(position+offset*rotation);
        return result.isFinite()?std::optional<LLVector3d>(result):std::nullopt;
    }
    void bind(bool isPoint,const std::shared_ptr<Impl>& self)
    {
        const auto sourceAvatar=avatar();
        if(!sourceAvatar)return;
        auto& effect=isPoint?point:look;
        const auto weak=std::weak_ptr<Impl>(self);
        sourceAvatar->attention(isPoint,effect.type?VSWorldAvatar::Attention([weak,isPoint]() -> std::optional<LLVector3d>
            {const auto state=weak.lock();return state?state->targetPosition(isPoint):std::nullopt;}):VSWorldAvatar::Attention(),effect.duration);
        effect.sourceGeneration=sourceAvatar->ownerGeneration();
    }
    void send(Effect& effect,bool isPoint)
    {
        const auto owner=session.lock();
        if(!owner || !alive() || !source() || !gMessageSystem || !effect.active || !effect.needsSend || effect.dead)return;
        U8 type=effect.type;
        LLVector3d offset=effect.offset;
        auto target=effect.target;
        if(!isPoint)
        {
            const bool privacy=gSavedSettings.getBOOL("PrivateLookAtTarget");
            if(privacy && gSavedSettings.getBOOL("PrivateLocalLookAtTarget"))
            {
                clear(effect);effect.dead=true;effect.needsSend=false;
                if(const auto source=avatar())
                {
                    source->attention(false,{},0);
                    source->character().removeAnimationData("LookAtPoint");
                }
                return;
            }
            if(privacy && type!=LOOKAT_TARGET_AUTO_LISTEN)
            {
                type=LOOKAT_TARGET_AUTO_LISTEN;offset.setVec(2.5,0.,0.);target=source();
            }
        }
        // Original privacy pack tests presence of the ORIGINAL target even
        // after substituting target_object. Preserve that exact wire contract.
        const auto wire=vs_attention_wire(account.agent,effect.target && target?target->id:LLUUID::null,offset,type);
        gMessageSystem->newMessage("ViewerEffect");
        gMessageSystem->nextBlock("AgentData");
        gMessageSystem->addUUID("AgentID",account.agent);gMessageSystem->addUUID("SessionID",account.session);
        gMessageSystem->nextBlock("Effect");
        gMessageSystem->addUUID("ID",effect.id);gMessageSystem->addUUID("AgentID",account.agent);
        gMessageSystem->addU8("Type",isPoint?LLHUDObject::LL_HUD_EFFECT_POINTAT:LLHUDObject::LL_HUD_EFFECT_LOOKAT);
        gMessageSystem->addF32("Duration",effect.duration);
        const LLColor4U color;gMessageSystem->addBinaryData("Color",color.mV,4);
        gMessageSystem->addBinaryData("TypeData",wire.data(),S32(wire.size()));
        effect.lastSend=time();effect.needsSend=false;
        // Same unreliable delivery as LLHUDManager::sendEffects/gAgent.sendMessage.
        gMessageSystem->sendMessage(owner->host());
    }
};
VSObjectAttention::VSObjectAttention(std::weak_ptr<VSNativeSession> owner):mImpl(std::make_shared<Impl>(std::move(owner))){}
VSObjectAttention::~VSObjectAttention(){reset();}
bool VSObjectAttention::point(U8 type,const std::optional<VSWorldScene::ObjectHandle>& target,LLVector3 position)
{
    const auto state=mImpl;auto* world=attentionScene();
    if(!world || !state->avatar() || !state->alive())return false;
    auto& effect=state->point;
    if(gSavedSettings.getBOOL("PrivatePointAtTarget"))
    {
        Impl::clear(effect);effect.dead=true;state->bind(true,state);return false;
    }
    if(target)
    {
        if(!world->current(*target))return false;
        const auto row=world->objectAdmission(*target);
        if(!row || row->attachment || row->pcode==LL_PCODE_LEGACY_AVATAR)return false;
    }
    if(effect.dead)effect=Impl::Effect();
    effect.active=true;
    if(type>=POINTAT_NUM_TARGETS || vs_point_attention_policy(type).priority<vs_point_attention_policy(effect.type).priority)return false;
    const auto now=state->time();
    if(type!=effect.type || !sameTarget(target,effect.target) || vs_attention_update_due(position,effect.lastSent,now,effect.lastSend))
    {
        effect.lastSent=position;effect.duration=vs_point_attention_policy(type).timeout;effect.needsSend=true;
    }
    if(type==POINTAT_TARGET_CLEAR)Impl::clear(effect);
    else
    {
        effect.type=type;effect.target=target;effect.offset=target?LLVector3d(position):gAgent.getPosGlobalFromAgent(position);
        effect.killTime=state->time()+effect.duration;
    }
    state->bind(true,state);
    return state->alive();
}
bool VSObjectAttention::look(U8 type,const std::optional<VSWorldScene::ObjectHandle>& requestedTarget,LLVector3 position)
{
    const auto state=mImpl;auto* world=attentionScene();const auto self=state->source();
    if(!world || !self || !state->avatar() || !state->alive())return false;
    auto target=requestedTarget;
    if(target && !world->current(*target))return false;
    // Preserve the reference camera wrapper before priority/limiter policies.
    if(gSavedSettings.getBOOL("PrivateLocalLookAtTarget"))
    {position.clearVec();type=LOOKAT_TARGET_NONE;target=self;}
    else if(target)
    {
        const auto row=world->objectAdmission(*target);
        if(row && row->attachment)
        {
            const auto root=world->rootObject(*target,false);
            if(root && root->id==self->id){target=self;position.clearVec();}
        }
    }
    auto next=state->look;
    auto& effect=next;
    if(effect.dead){effect=Impl::Effect();state->male=false;}
    effect.active=true;
    if(type>=LOOKAT_NUM_TARGETS || vs_look_attention_policy(state->male,type).priority<vs_look_attention_policy(state->male,effect.type).priority)return false;
    const auto now=state->time();
    const bool clamp=vs_attention_limited(vs_look_attention_policy(state->male,effect.type),gSavedSettings.getBOOL("FSLookAtTargetLimitDistance"));
    if(!clamp && (type!=effect.type || !sameTarget(target,effect.target) || vs_attention_update_due(position,effect.lastSent,now,effect.lastSend)))
    {effect.lastSent=position;effect.duration=vs_look_attention_policy(state->male,type).timeout;effect.needsSend=true;}
    if(type==LOOKAT_TARGET_CLEAR)Impl::clear(effect);
    else
    {
        effect.type=type;effect.target=target;
        if(target && clamp && target->id!=self->id)
        {
            const auto origin=gAgent.getPosGlobalFromAgent(LLVector3::zero);
            const auto focus=world->focusObject(*target,origin);
            if(!focus || !focus->current())return false;
            if(focus->avatar())
            {
                const auto targetAvatar=world->avatarForObject(target->id);
                const auto owner=state->session.lock();
                const auto region=owner?owner->regionSnapshot(target->host,false):std::nullopt;
                if(!targetAvatar || !region || !owner->current(*region))return false;
                effect.offset=LLVector3d(targetAvatar->headPosition())+from_region_handle(region->handle);
            }
            else effect.offset=gAgent.getPosGlobalFromAgent(focus->position()+position*focus->rotation());
            effect.target.reset();
        }
        else effect.offset=target?LLVector3d(position):gAgent.getPosGlobalFromAgent(position);
        if(clamp && !effect.target)
        {
            const auto owner=state->session.lock();const auto region=owner?owner->regionSnapshot(self->host,false):std::nullopt;
            if(!region || !owner->current(*region))return false;
            const LLVector3d head=LLVector3d(state->avatar()->headPosition())+from_region_handle(region->handle);
            const auto distance=F32(dist_vec(effect.offset,head));
            const auto radius=gSavedSettings.getF32("FSLookAtTargetMaxDistance");
            if(distance>radius)effect.offset=head+(effect.offset-head)*(radius/distance);
            // Original compares type after assigning it, so only positional
            // delta can trigger the limited-distance update here.
            const auto limitedPosition=gAgent.getPosAgentFromGlobal(effect.offset);
            if(vs_attention_update_due(limitedPosition,effect.lastSent,now,effect.lastSend))
            {effect.lastSent=limitedPosition;effect.duration=vs_look_attention_policy(state->male,type).timeout;effect.needsSend=true;}
        }
        effect.killTime=state->time()+effect.duration;
    }
    state->look=std::move(next);
    state->bind(false,state);
    // Original update switches the attention table only after setting/updating.
    if(type!=LOOKAT_TARGET_CLEAR)state->male=state->avatar()->character().getSex()==SEX_MALE;
    return state->alive();
}
void VSObjectAttention::tick()
{
    const auto state=mImpl;auto* world=attentionScene();
    if(!world || !state->alive())return;
    const auto avatar=state->avatar();
    if(!avatar)return;
    if(state->look.active && !state->look.dead)state->male=avatar->character().getSex()==SEX_MALE;
    const auto now=state->time();
    for(bool isPoint:{false,true})
    {
        auto& effect=isPoint?state->point:state->look;
        if(!effect.active)continue;
        bool changed=effect.sourceGeneration!=avatar->ownerGeneration();
        if(effect.target && !world->current(*effect.target)){Impl::clear(effect);changed=true;}
        if(effect.killTime!=0.f && now>effect.killTime && effect.type)
        {effect.type=0;changed=true;if(!isPoint){Impl::clear(effect);effect.needsSend=true;}}
        if(changed)state->bind(isPoint,state);
        state->send(effect,isPoint);
        if(!state->alive())return;
    }
}
void VSObjectAttention::reset()
{
    const auto state=mImpl;
    if(state->retired)return;
    if(const auto source=state->avatar())
    {
        source->attention(true,{},0);source->attention(false,{},0);
        source->character().removeAnimationData("PointAtPoint");source->character().removeAnimationData("LookAtPoint");
    }
    state->retired=true;
    Impl::clear(state->look);Impl::clear(state->point);
}

