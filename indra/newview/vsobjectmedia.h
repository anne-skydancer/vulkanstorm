// Object-scoped native ownership of shared media policy and plugin instances. LGPL-2.1.
#pragma once
#include "llhost.h"
#include "lluuid.h"
#include <functional>
#include <memory>
#include <string>
class LLMediaEntry;
class VSObjectMedia
{
public:
    VSObjectMedia(const LLHost&, const LLUUID&, std::function<bool()>);
    ~VSObjectMedia();
    // Refresh metadata through ObjectMedia; preserve the admitted data on failure.
    void update(const std::string& version, double pixels, double distance);
    LLUUID texture(unsigned face) const;
    // Synchronous borrow; the media owner retains the real entry and its ID.
    const LLMediaEntry* entry(unsigned face) const;
private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
