// Synchronous native sink for the actual shared LLTracker controller. LGPL-2.1.
#pragma once
#include "v3math.h"
#include "v4color.h"
#include <functional>
#include <string>
namespace VSTrackerDraw
{
struct Context
{
    LLColor4 fogColor;
    std::function<void(const LLVector3&,const std::string&,const LLColor4&,F32)> beam;
};
class Scope
{
public:
    explicit Scope(Context&);
    ~Scope();
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
private:
    Context* previous;
};
void beam(const LLVector3&,const std::string&,const LLColor4&,F32);
LLColor4 fogColor();
}
