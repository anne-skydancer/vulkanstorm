#include <map>
#include <memory>
#include <cassert>
#include <iostream>
#define LL_WARNS(x) std::cerr
#define LL_ENDL std::endl
using LLUUID=int;
struct LLVoiceChannel{LLUUID mSessionID;std::shared_ptr<const void>mRequestOwner=std::make_shared<char>(0);inline static std::map<LLUUID,LLVoiceChannel*>sVoiceChannelMap;explicit LLVoiceChannel(int id):mSessionID(id){}void updateSessionID(const LLUUID&);};
METHOD
int main(){LLVoiceChannel source(1);LLVoiceChannel::sVoiceChannelMap[1]=&source;std::weak_ptr<const void>request=source.mRequestOwner;source.updateSessionID(2);assert(request.expired()&&source.mSessionID==2&&!LLVoiceChannel::sVoiceChannelMap.contains(1)&&LLVoiceChannel::sVoiceChannelMap[2]==&source);request=source.mRequestOwner;source.updateSessionID(2);assert(!request.expired()&&LLVoiceChannel::sVoiceChannelMap[2]==&source);LLVoiceChannel successor(3);LLVoiceChannel::sVoiceChannelMap[3]=&successor;source.updateSessionID(3);assert(source.mSessionID==2&&LLVoiceChannel::sVoiceChannelMap[2]==&source&&LLVoiceChannel::sVoiceChannelMap[3]==&successor&&!request.expired());LLVoiceChannel orphan(2);orphan.updateSessionID(4);assert(LLVoiceChannel::sVoiceChannelMap[2]==&source&&!LLVoiceChannel::sVoiceChannelMap.contains(4));LLVoiceChannel absent(999);absent.updateSessionID(5);assert(!LLVoiceChannel::sVoiceChannelMap.contains(5)&&absent.mSessionID==999);}
