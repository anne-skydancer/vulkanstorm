// Shared original object-media event and bounce-back policy. LGPL-2.1.
#pragma once
#include "llviewermedia.h"
#include "llpluginclassmedia.h"
#include "llmediaentry.h"
#include "llnotificationsutil.h"
#include <vector>
template<class Navigate>
void vs_media_event(LLViewerMediaImpl* impl,LLPluginClassMedia* plugin,LLViewerMediaObserver::EMediaEvent event,Navigate navigate)
{
    switch(event)
    {

        case LLViewerMediaObserver::MEDIA_EVENT_LOCATION_CHANGED:
        {
            switch(impl->getNavState())
            {
                case LLViewerMediaImpl::MEDIANAVSTATE_FIRST_LOCATION_CHANGED:
                {
                    // This is the first location changed event after the start of a non-server-directed nav.  It may need to be broadcast or bounced back.
                    navigate(impl, plugin, plugin->getLocation());
                }
                break;

                case LLViewerMediaImpl::MEDIANAVSTATE_FIRST_LOCATION_CHANGED_SPURIOUS:
                    // This navigate didn't change the current URL.
                    LL_DEBUGS("MediaOnAPrim") << "  NOT broadcasting navigate (spurious)" << LL_ENDL;
                break;

                case LLViewerMediaImpl::MEDIANAVSTATE_SERVER_FIRST_LOCATION_CHANGED:
                    // This is the first location changed event after the start of a server-directed nav.  Don't broadcast it.
                    LL_INFOS("MediaOnAPrim") << "   NOT broadcasting navigate (server-directed)" << LL_ENDL;
                break;

                default:
                    // This is a subsequent location-changed due to a redirect.  Don't broadcast.
                    LL_INFOS("MediaOnAPrim") << "   NOT broadcasting navigate (redirect)" << LL_ENDL;
                break;
            }
        }
        break;

        case LLViewerMediaObserver::MEDIA_EVENT_NAVIGATE_COMPLETE:
        {
            switch(impl->getNavState())
            {
                case LLViewerMediaImpl::MEDIANAVSTATE_COMPLETE_BEFORE_LOCATION_CHANGED:
                {
                    // This is the first location changed event after the start of a non-server-directed nav.  It may need to be broadcast or bounced back.
                    navigate(impl, plugin, plugin->getNavigateURI());
                }
                break;

                case LLViewerMediaImpl::MEDIANAVSTATE_COMPLETE_BEFORE_LOCATION_CHANGED_SPURIOUS:
                    // This navigate didn't change the current URL.
                    LL_DEBUGS("MediaOnAPrim") << "  NOT broadcasting navigate (spurious)" << LL_ENDL;
                break;

                case LLViewerMediaImpl::MEDIANAVSTATE_SERVER_COMPLETE_BEFORE_LOCATION_CHANGED:
                    // This is the the navigate complete event from a server-directed nav.  Don't broadcast it.
                    LL_INFOS("MediaOnAPrim") << "   NOT broadcasting navigate (server-directed)" << LL_ENDL;
                break;

                default:
                    // For all other states, the navigate should have been handled by LOCATION_CHANGED events already.
                break;
            }
        }
        break;

        case LLViewerMediaObserver::MEDIA_EVENT_FILE_DOWNLOAD:
        {
            // Media might be blocked, waiting for a file,
            // send an empty response to unblock it
            const std::vector<std::string> empty_response;
            plugin->sendPickFileResponse(empty_response);

            LLNotificationsUtil::add("MediaFileDownloadUnsupported");
        }
        break;

        default:
        break;
    }

}
inline void vs_media_bounce_back(const LLMediaEntry* mep,viewer_media_t impl)
{
    if (mep && impl)
    {
        std::string url = mep->getCurrentURL();
        // Look for a ":", if not there, assume "https://"
        if (!url.empty() && std::string::npos == url.find(':'))
        {
            url = "https://" + url;
        }
        // If the url we're trying to "bounce back" to is either empty or not
        // allowed by the whitelist, try the home url.  If *that* doesn't work,
        // set the media as failed and unload it
        if (url.empty() || !mep->checkCandidateUrl(url))
        {
            url = mep->getHomeURL();
            // Look for a ":", if not there, assume "https://"
            if (!url.empty() && std::string::npos == url.find(':'))
            {
                url = "https://" + url;
            }
        }
        if (url.empty() || !mep->checkCandidateUrl(url))
        {
            // The url to navigate back to is not good, and we have nowhere else
            // to go.
            LL_WARNS("MediaOnAPrim") << "FAILED to bounce back URL \"" << url << "\" -- unloading impl" << LL_ENDL;
            impl->setMediaFailed(true);
        }
        // Make sure we are not bouncing to url we came from
        else if (impl->getCurrentMediaURL() != url)
        {
            // Okay, navigate now
            LL_INFOS("MediaOnAPrim") << "bouncing back to URL: " << url << LL_ENDL;
            impl->navigateTo(url, "", false, true);
        }
    }
}
