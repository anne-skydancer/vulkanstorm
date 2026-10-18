// Original texture-drop channel and permission policy. LGPL-2.1.
#pragma once
enum class VSFaceTextureDrop { Refuse, Diffuse, Normal, Specular, PBROverride };
inline VSFaceTextureDrop vs_face_texture_drop(bool pbr,bool removePBR,S32 channel,
    bool toolsVisible,S32 toolsChannel,S32 channelCount,bool copy,bool transfer)
{
    // Firestorm -2 bypasses PBR before being normalized to the ordinary channel.
    if(pbr && !removePBR && channel>=-1)
        return copy && transfer ? VSFaceTextureDrop::PBROverride : VSFaceTextureDrop::Refuse;
    if(channel<-1)channel=-1;
    if(!toolsVisible || toolsChannel==channelCount)return VSFaceTextureDrop::Diffuse;
    channel=channel>-1?channel:toolsChannel;
    return channel==1?VSFaceTextureDrop::Normal:channel==2?VSFaceTextureDrop::Specular:VSFaceTextureDrop::Diffuse;
}
