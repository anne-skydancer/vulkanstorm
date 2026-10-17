// Native scene operations for the shared Firestorm bridge controller. LGPL-2.1.
#pragma once
#include "vsworldscene.h"
#include "vstaskinventory.h"
#include <functional>
#include <map>
#include <memory>
class VSNativeSession;
class LLViewerInventoryItem;
class VSBridgeAdapter
{
public:
    using Attach = std::function<void(const VSWorldScene::Attachment&)>;
    using Detach = std::function<void(const LLUUID&, U8)>;
    VSBridgeAdapter(std::weak_ptr<VSNativeSession>, Attach, Detach);
    void tick();
    void reset();
    std::optional<VSWorldScene::Attachment> attachment(const LLUUID&) const;
    bool configure(const LLUUID&);
    bool inspect(const LLUUID&, VSTaskInventory::Callback);
    bool saveScript(const LLUUID& attachment, const LLViewerInventoryItem& script);
    std::function<bool()> guard(const LLUUID&) const;
private:
    std::weak_ptr<VSNativeSession> mSession;
    Attach mAttach;
    Detach mDetach;
    std::map<LLUUID, VSWorldScene::Attachment> mAttachments;
};
