// Native avatar presentation owned by the actual admitted avatar. LGPL-2.1.
#pragma once
#include "lluuid.h"
#include "v3math.h"
#include "v3dmath.h"
#include "llsd.h"
#include <functional>
#include <optional>
#include <memory>
#include <map>
#include <string_view>
class LLAvatarAppearance;
class LLJoint;
class LLFrameTimer;
class LLChat;
class LLVector4a;
class VSAvatarNameTag
{
public:
    struct Frame
    {
        LLUUID id;
        LLAvatarAppearance* appearance=nullptr;
        LLJoint* head=nullptr;
        LLVector3 position,agentOffset;
        LLVector3d globalPosition;
        float waterHeight=0,radius=0,angularExtent=0;
        U32 complexity=0;
        float attachmentArea=0;
        bool self=false,missingParts=true,tooComplex=false,visible=false;
        S32 rezStatus=0;
        LLSD clientTag;
        std::shared_ptr<const std::function<bool()>> current;
    };
    using Reserve=std::function<std::shared_ptr<const void>(std::size_t)>;
    VSAvatarNameTag(const LLUUID&,const std::map<LLUUID,S32>&,bool& typing,LLFrameTimer&,std::shared_ptr<const void> residency,Reserve);
    static std::size_t allocationBytes();
    static void refreshPreferences(S32 simulatorLimit);
    ~VSAvatarNameTag();
    void update(Frame,std::string_view nameValues);
    void chat(const LLChat&);
    void typing(bool);
    void invalidate();
    void suspend();
    std::optional<LLVector3> voicePosition() const;
    bool intersect(const LLVector4a&,const LLVector4a&,LLVector4a&) const;
private:
    struct State;
    std::shared_ptr<State> mState;
};
