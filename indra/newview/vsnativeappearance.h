// Connected native appearance transactions and attachment queues. LGPL-2.1.
#pragma once
#include "lluuid.h"
#include "llpointer.h"
#include <functional>
#include <memory>
#include <vector>
class VSNativeSession;
class LLSD;
class LLInventoryCallback;
class VSNativeAppearance
{
public:
    explicit VSNativeAppearance(std::shared_ptr<VSNativeSession>);
    ~VSNativeAppearance();
    void tick();
    bool startupReady() const;
    void fallback(const LLSD& wearables);
    void refresh();
    bool hoverEnabled() const;
    bool setHoverHeight(F32, bool send);
    void syncHoverHeight();
    bool canWear(const std::vector<LLUUID>&, bool replace = false, bool replaceOutfit = false) const;
    bool wear(const std::vector<LLUUID>&, bool replace, LLPointer<LLInventoryCallback> callback = {});
    bool remove(const std::vector<LLUUID>&, LLPointer<LLInventoryCallback> callback = {});
    bool attach(const LLUUID&, U8 point, bool add);
    bool detach(const LLUUID&);
    bool wearCategory(const LLUUID&, bool append);
    bool isWorn(const LLUUID&) const;
    bool moveWearable(const LLUUID&, bool closerToBody);
    bool removeTop(unsigned type);
    LLSD evidence() const;

private:
    bool wearItems(const std::vector<LLUUID>&, bool replace, LLPointer<LLInventoryCallback>, bool replaceOutfit);
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
