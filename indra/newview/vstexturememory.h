// Native transposition of LLViewerTexture::updateClass memory policy. LGPL-2.1.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace VSTextureMemory
{
struct Settings
{
    double detectedMiB=1024,overrideMiB=0,divisor=2,manualMiB=0;
    bool manual=false;
    double minimumTargetMiB=768;
    double lowIncrement=.1,highDecrement=.1,minimumFreeSystemMiB=512;
    double minimizedSeconds=1,backgroundSeconds=60;
};
struct Input
{
    std::uint64_t gpuBytes=0,geometryBytes=0;
    double now=0,seconds=0,freeSystemMiB=0,systemFactor=1,admissionOver=-1;
    bool systemLow=false,background=false,minimized=false;
};
class Policy
{
    double lowSince=0,backgroundSince=0,savedBias=1;
    bool wasLow=false,wasBackground=false;
public:
    double budgetMiB=1024,targetMiB=768,usedMiB=0,freeMiB=768,bias=1;
    void update(const Settings& settings,const Input& in)
    {
        const double detected=settings.overrideMiB>0?settings.overrideMiB:settings.detectedMiB;
        budgetMiB=settings.manual?settings.manualMiB:std::max(1024.0,detected/(settings.divisor>0?settings.divisor:1));
        targetMiB=std::max(std::min(budgetMiB-512,budgetMiB*.8),settings.minimumTargetMiB);
        // Preserve the reference's conservative estimate; CPU snapshots are
        // accounted separately and must never be passed here as GPU bytes.
        usedMiB=std::round((double(in.gpuBytes)+double(in.geometryBytes))/(1024*512));
        freeMiB=std::max(targetMiB-usedMiB,0.0);
        const double over=std::max((usedMiB-targetMiB)/targetMiB,in.admissionOver);
        const bool low=in.systemLow || over>0;
        if(low && !wasLow) {
            bias=std::max(bias,1.5*(in.systemLow?in.systemFactor:1));
        }
        wasLow=low;
        if(low) {
            if(in.now-lowSince>1) bias+=(settings.lowIncrement+std::max(over,0.0))*in.seconds;
        } else {
            lowSince=in.now;
            if(bias>1 && over<-.1 && in.freeSystemMiB>settings.minimumFreeSystemMiB+100)
                bias-=(settings.highDecrement-std::min(over+.1,0.0))*in.seconds;
        }
        if(in.background) {
            const double timeout=in.minimized?settings.minimizedSeconds:settings.backgroundSeconds;
            if(timeout>0 && in.now-backgroundSince>timeout) {
                if(!wasBackground) {savedBias=bias;wasBackground=true;}
                bias=5;
            }
        } else {
            backgroundSince=in.now;
            if(wasBackground) {wasBackground=false;bias=savedBias;}
        }
        bias=std::clamp(bias,1.0,4.0);
    }
    // LLViewerTextureList scales projected area by round(4^(bias-1)). Native
    // subscribers use a projected dimension, so apply the square-root scaler.
    unsigned dimension(unsigned requested,unsigned maxDiscard,double importance=0) const
    {
        if(bias<=1.9+importance*.5) return requested;
        const double adjusted=std::clamp(double(maxDiscard)-2,1.0,bias);
        const double area=std::round(std::pow(4.0,adjusted-1));
        return std::max(1u,unsigned(std::ceil(requested/std::sqrt(area))));
    }
};
}
