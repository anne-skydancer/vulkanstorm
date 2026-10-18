// The collection scope never outlives its exact synchronous frame/account.
#include "llviewerprecompiledheaders.h"
#include "vstrackerdraw.h"
#include <stdexcept>
namespace VSTrackerDraw
{
namespace{thread_local Context* active=nullptr;}
Scope::Scope(Context& value):previous(active){active=&value;}
Scope::~Scope(){active=previous;}
void beam(const LLVector3& position,const std::string& direction,const LLColor4& color,F32 distance)
{
    if(!active || !active->beam)throw std::logic_error("Native tracker draw without its current frame owner");
    active->beam(position,direction,color,distance);
}
LLColor4 fogColor()
{
    if(!active)throw std::logic_error("Native tracker fog without its current frame owner");
    return active->fogColor;
}
}
