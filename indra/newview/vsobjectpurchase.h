// Retained genuine simulator selection for the shared Buy/Open controllers.
// LGPL-2.1, like the viewer.
#pragma once
#include "vsobjectselection.h"
#include "vstaskinventoryobject.h"
#include <boost/signals2/connection.hpp>

struct VSObjectPurchase
{
    static std::shared_ptr<VSObjectPurchase> capture(bool singleRoot = true);
    std::shared_ptr<VSObjectSelection>       selection;
    std::shared_ptr<const void>              lease;
    VSTaskInventoryObject                    object;
    VSObjectSelection::Publication           publication;
    std::function<bool()>                    current;
    boost::signals2::scoped_connection       inventoryChanged;
    boost::signals2::scoped_connection       selectionChanged;
    bool                                     owner(LLUUID&, std::string&) const;
    bool                                     buy(const LLUUID& category, const LLSaleInfo&) const;
};
