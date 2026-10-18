// Retained genuine simulator selection for the shared Buy/Open controllers.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsobjectpurchase.h"
#include "vsnativesession.h"
#include "llagent.h"
#include "llviewercontrol.h"
#include "llslurl.h"
#include "lltrans.h"
#include "message.h"

std::shared_ptr<VSObjectPurchase> VSObjectPurchase::capture(bool singleRoot)
{
    const auto account   = VSNativeSession::active();
    const auto selection = account ? account->retainObjectSelection() : nullptr;
    if (!selection || (singleRoot && selection->rootObjectCount() != 1) || !selection->primaryObject())
        return {};
    const auto root     = selection->firstRootObject();
    const auto property = root ? selection->properties(*root) : std::nullopt;
    if (!property)
        return {};
    const auto object = VSTaskInventoryObject::find(root->id);
    if (!object)
        return {};
    auto result         = std::make_shared<VSObjectPurchase>();
    result->selection   = selection;
    result->lease       = selection->retainSelection();
    result->object      = object;
    result->publication = *property;
    const auto selected = selection->selectionGuard();
    const auto source   = object.objectGuard();
    result->current     = [selected, source]
    {
        return selected() && source();
    };
    return result;
}

bool VSObjectPurchase::owner(LLUUID& id, std::string& name) const
{
    if (!current())
        return false;
    bool first = true, groupOwned = false;
    for (const auto& token : selection->selected())
    {
        if (!gSavedSettings.getBOOL("EditLinkedParts") && token.id != publication.object.id)
            continue;
        const auto value = selection->properties(token);
        if (!value)
            continue; // The reference query iterates valid nodes only.
        const auto permissions = value->properties->permissions();
        LLUUID     owner;
        bool       group = false;
        permissions.getOwnership(owner, group);
        if (first)
        {
            id         = owner;
            groupOwned = group;
            first      = false;
        }
        else if (id != owner)
        {
            name = LLTrans::getString("AvatarNameMultiple");
            return false;
        }
    }
    if (first || id.isNull())
        return false;
    name = LLSLURL(groupOwned ? "group" : "agent", id, "inspect").getSLURLString();
    return true;
}

bool VSObjectPurchase::buy(const LLUUID& category, const LLSaleInfo& sale) const
{
    if (!current() || !gMessageSystem)
        return false;
    // Send the price/type the user confirmed; the simulator verifies the sale.
    // The retained source prevents a later selection or same-UUID replacement
    // from changing the object bought by this dialog.
    gMessageSystem->newMessageFast(_PREHASH_ObjectBuy);
    gMessageSystem->nextBlockFast(_PREHASH_AgentData);
    gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    gMessageSystem->addUUIDFast(_PREHASH_GroupID, gAgent.getGroupID());
    gMessageSystem->addUUIDFast(_PREHASH_CategoryID, category);
    gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
    gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID, object.getLocalID());
    gMessageSystem->addU8Fast(_PREHASH_SaleType, sale.getSaleType());
    gMessageSystem->addS32Fast(_PREHASH_SalePrice, sale.getSalePrice());
    return gMessageSystem->sendReliable(object.getHost()) > 0;
}
