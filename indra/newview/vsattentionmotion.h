// Exact LLHUDEffect animation-controller side effects shared by both peers.
// LGPL-2.1, like the viewer.
#pragma once
template<class Character,class MotionID>
void vs_attention_motion(Character& avatar,bool point,bool targetPresent,bool disableLook,const MotionID& head,const MotionID& editing)
{
    if(point)
    {
        if(targetPresent)avatar.startMotion(editing);
        return;
    }
    if(!targetPresent){avatar.stopMotion(head);return;}
    auto* motion=avatar.findMotion(head);
    if(disableLook)
    {
        if(motion)avatar.stopMotion(head);
    }
    else if(!motion || motion->isStopped())avatar.startMotion(head);
}
