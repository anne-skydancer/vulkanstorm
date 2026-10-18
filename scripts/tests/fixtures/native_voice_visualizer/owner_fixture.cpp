#include <memory>
#include <functional>
#include <vector>
#include <cassert>
struct LLVector3d{};
struct VSWorldRenderer{struct Packet{};};
struct Visualizer{unsigned calls=0;void appendNative(VSWorldRenderer&,const LLVector3d&,std::vector<VSWorldRenderer::Packet>&p){++calls;p.emplace_back();}};
struct VSWorldAvatar{
 struct Impl{std::shared_ptr<const std::function<bool()>>voiceCurrent;std::shared_ptr<Visualizer>mVoiceVisualizer;VSWorldRenderer renderer;unsigned retirements=0;void retireVoice(){++retirements;mVoiceVisualizer={};voiceCurrent={};}};
 std::unique_ptr<Impl>mImpl=std::make_unique<Impl>();void appendVoice(std::vector<VSWorldRenderer::Packet>&,const LLVector3d&);
};
METHOD
int main(){bool current=true;VSWorldAvatar owner;auto visualizer=std::make_shared<Visualizer>();owner.mImpl->mVoiceVisualizer=visualizer;owner.mImpl->voiceCurrent=std::make_shared<const std::function<bool()>>([&]{return current;});std::vector<VSWorldRenderer::Packet>packets;owner.appendVoice(packets,{});assert(packets.size()==1&&visualizer->calls==1);current=false;packets.clear();owner.appendVoice(packets,{});assert(packets.empty()&&visualizer->calls==1&&!owner.mImpl->mVoiceVisualizer&&!owner.mImpl->voiceCurrent);owner.appendVoice(packets,{});assert(packets.empty()&&visualizer->calls==1);}
