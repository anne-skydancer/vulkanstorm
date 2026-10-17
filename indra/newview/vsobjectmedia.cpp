// Object-scoped native ownership of shared media policy and plugin instances. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsobjectmedia.h"
#include "vsnativesession.h"
#include "vsresourcebudget.h"
#include "llviewermedia.h"
#include "lltextureentry.h"
#include "llmediaentry.h"
#include "llcorehttputil.h"
#include "llcoros.h"
#include "llframetimer.h"
#include "workqueue.h"
#include "llsdutil.h"
#include <vector>
namespace
{
VSResourceBudget requests(8);
// Charge the response's admitted maximum, including inactive object metadata.
// A transport slot alone would not bound retained media entries after a tour.
VSResourceBudget metadata(16*1024*1024);
}
struct VSObjectMedia::Impl
{
    LLHost host;
    LLUUID object;
    std::function<bool()> valid;
    std::string requestedVersion;
    std::string desiredVersion;
    bool known=false, retired=false;
    double retryAt=0;
    S32 fetchedVersion=-1;
    std::vector<std::unique_ptr<LLTextureEntry>> entries;
    std::vector<viewer_media_t> media;
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t request;
    std::shared_ptr<const VSResourceBudget::Reservation> residency;
    void release()
    {
        for(auto& instance:media) if(instance) instance->removeNativeSurface(object);
        media.clear();entries.clear();
    }
};
VSObjectMedia::VSObjectMedia(const LLHost& host,const LLUUID& object,std::function<bool()> valid)
    :mImpl(std::make_shared<Impl>())
{
    mImpl->host=host;mImpl->object=object;mImpl->valid=std::move(valid);
}
VSObjectMedia::~VSObjectMedia()
{
    mImpl->retired=true;
    auto request=std::exchange(mImpl->request,{});
    if(request) request->cancelSuspendedOperation();
    mImpl->release();
}
LLUUID VSObjectMedia::texture(unsigned face) const
{
    if(face>=mImpl->media.size() || !mImpl->media[face]) return {};
    return mImpl->media[face]->getMediaTextureID();
}
void VSObjectMedia::update(const std::string& version,double pixels,double distance)
{
    if(!mImpl->valid()) return;
    mImpl->desiredVersion=version;
    for(auto& instance:mImpl->media)
        if(instance) instance->setNativeSurface(mImpl->object,pixels,distance);
    const auto session=VSNativeSession::active();
    if(!session || mImpl->request || (mImpl->known && mImpl->requestedVersion==version) ||
        LLFrameTimer::getTotalSeconds()<mImpl->retryAt) return;
    const auto url=session->capability(mImpl->host,"ObjectMedia");
    if(url.empty()) return;
    auto slot=requests.reserve(1);
    if(!slot) return;
    auto residency=mImpl->residency?mImpl->residency:metadata.reserve(256*1024);
    if(!residency) return;
    auto request=std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("nativeObjectMedia",LLCore::HttpRequest::DEFAULT_POLICY_ID);
    mImpl->request=request;mImpl->requestedVersion=version;mImpl->retryAt=LLFrameTimer::getTotalSeconds()+5;
    const auto guard=session->accountGuard();
    const auto circuit=session->circuitGeneration(mImpl->host);
    const auto host=mImpl->host;const auto object=mImpl->object;
    const std::weak_ptr<Impl> weak=mImpl;const std::weak_ptr<VSNativeSession> owner=session;
    LLCoros::instance().launch("nativeObjectMedia",[weak,owner,guard,circuit,host,object,request,url,version,slot,residency]()
    {
        auto http=std::make_shared<LLCore::HttpRequest>();
        auto options=std::make_shared<LLCore::HttpOptions>();
        options->setTransferTimeout(15);options->setRetries(0);options->setMaximumResponseBodyBytes(256*1024);
        LLSD body;body["verb"]="GET";body[LLTextureEntry::OBJECT_ID_KEY]=object;
        const auto result=request->postAndSuspend(http,url,body,options);
        LL::WorkQueue::getInstance("mainloop")->post([weak,owner,guard,circuit,host,object,request,result,version,slot,residency]()
        {
            const auto state=weak.lock();const auto session=owner.lock();
            if(!state || state->retired || state->request!=request) return;
            state->request.reset();
            if(!guard() || !state->valid() || state->desiredVersion!=version || !session || session->circuitGeneration(host)!=circuit ||
                !LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(result["http_result"]) ||
                result.has("error") || result[LLTextureEntry::OBJECT_ID_KEY].asUUID()!=object) return;
            const auto& data=result[LLTextureEntry::OBJECT_MEDIA_DATA_KEY];
            if(!data.isArray() || data.size()>45) return;
            const auto mediaVersion=result[LLTextureEntry::MEDIA_VERSION_KEY].asString();
            const auto fetched=LLTextureEntry::getVersionFromMediaVersionString(mediaVersion);
            if(S32(fetched)<state->fetchedVersion) return;
            // Validate every row before changing any admitted entry. Keep each
            // LLMediaEntry at its original address: copying it generates a new
            // media ID and would restart the plugin on every URL/version update.
            for(const auto& item:llsd::inArray(data))
                if(!item.isUndefined() && !item.isMap()) return;
            std::vector<std::unique_ptr<LLTextureEntry>> entries;
            std::vector<viewer_media_t> media;
            entries.reserve(data.size());media.reserve(data.size());
            const bool fromSelf=LLTextureEntry::getAgentIDFromMediaVersionString(mediaVersion)==session->account().agent;
            for(unsigned face=0;face<unsigned(data.size());++face)
            {
                auto entry=face<state->entries.size()?std::move(state->entries[face]):std::make_unique<LLTextureEntry>();
                const auto* previous=entry->getMediaData();
                const std::string previousURL=previous?previous->getCurrentURL():"";
                entry->updateMediaData(data[face]);
                auto* metadata=entry->getMediaData();
                media.push_back(metadata?LLViewerMedia::getInstance()->updateMediaImpl(metadata,previousURL,fromSelf):viewer_media_t{});
                entries.push_back(std::move(entry));
            }
            state->release();state->entries=std::move(entries);state->media=std::move(media);
            state->fetchedVersion=S32(fetched);state->known=true;state->requestedVersion=version;
            state->residency=residency;
        });
    });
}
