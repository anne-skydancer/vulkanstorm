// Scoped ownership for the shared estate covenant request/content pipeline. LGPL-2.1.
#pragma once
#include "vsregioncontext.h"
#include <functional>
class LLView;
class LLTextBase;
class LLMessageSystem;
struct VSCovenantCallbacks
{
    std::function<void(const std::string&)> estateName,ownerName,lastModified;
    std::function<void(const LLTextBase*,const LLUUID&)> content;
    std::function<void(const std::string&,const LLUUID&)> text;
};
void vs_request_covenant(const VSRegionContext&,LLView&,VSCovenantCallbacks,std::function<bool()> targetCurrent={});
void vs_receive_covenant(LLMessageSystem*);
void vs_reset_covenants();
