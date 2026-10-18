// Object-scoped native ownership of shared media policy and plugin instances. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsobjectmedia.h"
#include "vsprimmediatarget.h"
#include "vsmediaevent.h"
#include "llmediadataclient.h"
#include "llvovolume.h"
#include "llviewermediafocus.h"

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
struct VSObjectMedia::Impl : LLViewerMediaObserver,std::enable_shared_from_this<Impl>
{
    struct ClientObject;
    void handleMediaEvent(LLPluginClassMedia*,EMediaEvent) override;
    void navigated(LLViewerMediaImpl*,const std::string&);
    void bounceBack(U8);
    bool publish(const LLSD&,const std::string&,const std::string&,
                 std::shared_ptr<const VSResourceBudget::Reservation>);

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
        for(auto& instance:media)
            if(instance){instance->remObserver(this);instance->removeNativeSurface(object);}
        media.clear();entries.clear();
    }
};

bool VSObjectMedia::Impl::publish(const LLSD& data,const std::string& mediaVersion,
                                const std::string& version,
                                std::shared_ptr<const VSResourceBudget::Reservation> residency)
{
    const auto session=VSNativeSession::active();
    if(retired || !valid() || !session || !residency)return false;
    if(!data.isArray() || data.size()>45) return false;
    const auto fetched=LLTextureEntry::getVersionFromMediaVersionString(mediaVersion);
    if(S32(fetched)<fetchedVersion) return false;
    // Validate every row before changing any admitted entry. Keep each
    // LLMediaEntry at its original address: copying it generates a new
    // media ID and would restart the plugin on every URL/version update.
    for(const auto& item:llsd::inArray(data))
        if(!item.isUndefined() && !item.isMap()) return false;
    std::vector<std::unique_ptr<LLTextureEntry>> entries;
    std::vector<viewer_media_t> media;
    entries.reserve(data.size());media.reserve(data.size());
    const bool fromSelf=LLTextureEntry::getAgentIDFromMediaVersionString(mediaVersion)==session->account().agent;
    for(unsigned face=0;face<unsigned(data.size());++face)
    {
        auto entry=face<this->entries.size()?std::move(this->entries[face]):std::make_unique<LLTextureEntry>();
        const auto* previous=entry->getMediaData();
        const std::string previousURL=previous?previous->getCurrentURL():"";
        entry->updateMediaData(data[face]);
        auto* metadata=entry->getMediaData();
        media.push_back(metadata?LLViewerMedia::getInstance()->updateMediaImpl(metadata,previousURL,fromSelf):viewer_media_t{});
        entries.push_back(std::move(entry));
    }
    for(auto& previous:this->media)
        if(previous && std::none_of(media.begin(),media.end(),[&](const auto& current){return current==previous;}))
        {previous->remObserver(this);previous->removeNativeSurface(object);}
    this->entries=std::move(entries);this->media=std::move(media);
    fetchedVersion=S32(fetched);known=true;requestedVersion=version;
    for(auto& instance:this->media)if(instance)instance->addObserver(this);
    this->residency=std::move(residency);
    return true;
}

struct VSObjectMedia::Impl::ClientObject final : LLMediaDataClientObject
{
    std::weak_ptr<Impl> state;
    LLUUID object;
    LLHost host;
    std::weak_ptr<VSNativeSession> session;
    std::function<bool()> account;
    U64 circuit=0;
    explicit ClientObject(const std::shared_ptr<Impl>& owner)
        :state(owner),object(owner->object),host(owner->host)
    {
        if(const auto active=VSNativeSession::active())
        {session=active;account=active->accountGuard();circuit=active->circuitGeneration(host);}
    }
    std::shared_ptr<Impl> current()const
    {
        const auto owner=state.lock();const auto active=session.lock();
        return owner && !owner->retired && owner->valid() && active && account && account() &&
            active->circuitGeneration(host)==circuit?owner:nullptr;
    }
    U8 getMediaDataCount()const override
    {const auto owner=current();return owner?U8(owner->entries.size()):0;}
    LLSD getMediaDataLLSD(U8 face)const override
    {
        const auto owner=current();
        const auto* entry=owner && face<owner->entries.size()?owner->entries[face]->getMediaData():nullptr;
        if(!entry)return {};
        auto value=entry->asLLSD();
        if(!value.has(LLMediaEntry::WHITELIST_KEY))value[LLMediaEntry::WHITELIST_KEY]=LLSD::emptyArray();
        return value;
    }
    bool isCurrentMediaUrl(U8 face,const std::string& url)const override
    {
        const auto owner=current();
        const auto* entry=owner && face<owner->entries.size()?owner->entries[face]->getMediaData():nullptr;
        return entry?entry->getCurrentURL()==url:url.empty();
    }
    LLUUID getID()const override{return object;}
    void mediaNavigateBounceBack(U8 face)override
    {if(const auto owner=current())owner->bounceBack(face);}
    bool hasMedia()const override
    {
        const auto owner=current();if(!owner)return false;
        return std::any_of(owner->entries.begin(),owner->entries.end(),[](const auto& entry){return entry->hasMedia();});
    }
    void updateObjectMediaData(const LLSD& data,const std::string& version)override
    {if(const auto owner=current())owner->publish(data,version,owner->desiredVersion,owner->residency);}
    F64 getMediaInterest()const override
    {
        const auto owner=current();F64 total=0;
        if(owner)for(const auto& impl:owner->media)if(impl)total+=impl->getInterest();
        return total;
    }
    std::string getCapabilityUrl(const std::string& name)const override
    {const auto active=session.lock();return current() && active?active->capability(host,name):std::string();}
    bool isDead()const override{return !current();}
    U32 getMediaVersion()const override
    {const auto owner=current();return owner?LLTextureEntry::getVersionFromMediaVersionString(owner->desiredVersion):0;}
    bool isInterestingEnough()const override
    {return current() && LLViewerMedia::instance().isNativeInterestingEnough(object,getMediaInterest());}
    bool isNew()const override
    {const auto owner=current();return owner && !owner->known;}
};

void VSObjectMedia::Impl::bounceBack(U8 face)
{
    if(retired || !valid() || face>=entries.size() || face>=media.size())return;
    vs_media_bounce_back(entries[face]->getMediaData(),media[face]);
}
void VSObjectMedia::Impl::navigated(LLViewerMediaImpl* impl,const std::string& location)
{
    if(retired || !valid())return;
    const auto found=std::find_if(media.begin(),media.end(),[impl](const auto& value){return value.get()==impl;});
    if(found==media.end())return;
    const U8 face=U8(found-media.begin());
    const auto target=VSPrimMediaTarget::resolve(object,face);
    if(!target || target->media()!=impl)return;
    const auto* entry=target->metadata();
    if(entry && (!entry->checkCandidateUrl(location) || !target->permission(true)))
        bounceBack(face);
    else if(LLVOVolume::sObjectMediaNavigateClient)
        LLVOVolume::sObjectMediaNavigateClient->navigate(new ClientObject(shared_from_this()),face,location);
}
void VSObjectMedia::Impl::handleMediaEvent(LLPluginClassMedia* plugin,EMediaEvent event)
{
    const auto lifetime=shared_from_this();
    if(retired || !valid())return;
    const auto found=std::find_if(media.begin(),media.end(),[plugin](auto& value){return value && value->getMediaPlugin()==plugin;});
    if(found==media.end())return;
    const viewer_media_t impl=*found;
    vs_media_event(impl,plugin,event,[this](auto* impl,auto*,const std::string& location)
    {navigated(impl,location);});
}

VSObjectMedia::VSObjectMedia(const LLHost& host,const LLUUID& object,std::function<bool()> valid)
    :mImpl(std::make_shared<Impl>())
{
    mImpl->host=host;mImpl->object=object;mImpl->valid=std::move(valid);
}
VSObjectMedia::~VSObjectMedia()
{
    if(LLVOVolume::sObjectMediaNavigateClient)
    {
        LLMediaDataClientObject::ptr_t object=new Impl::ClientObject(mImpl);
        LLVOVolume::sObjectMediaNavigateClient->removeFromQueue(object);
    }
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
const LLMediaEntry* VSObjectMedia::entry(unsigned face) const
{
    if(!mImpl->valid() || face>=mImpl->entries.size() || !mImpl->entries[face])return nullptr;
    return mImpl->entries[face]->getMediaData();
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
            state->publish(result[LLTextureEntry::OBJECT_MEDIA_DATA_KEY],
                result[LLTextureEntry::MEDIA_VERSION_KEY].asString(),version,residency);
        });
    });
}
