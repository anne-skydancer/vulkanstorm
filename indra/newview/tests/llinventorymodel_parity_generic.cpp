/**
 * @file llinventorymodel_parity_generic.cpp
 * @brief Frozen copies of the GenericMessage handlers (tests only).
 *
 * The GenericMessage and LargeGenericMessage UDP handlers of
 * llviewergenericmessage.cpp hand BulkUpdateInventory to gGenericDispatcher,
 * where LLInventoryModel registers its LLDispatchBulkUpdateInventory. That
 * file cannot be linked into the test (it brings the GLTF material list),
 * so the two handlers are copied here verbatim into a test-only namespace;
 * gAgent and gGenericDispatcher are the stubs' (llinventorymodel_parity_stubs.cpp).
 * tests/check_r2_copies.py checks the copies against the live source.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "../llviewerprecompiledheaders.h"

#include "../llagent.h"
#include "../llviewergenericmessage.h"
#include "lldispatcher.h"
#include "lluuid.h"
#include "message.h"

namespace vks_inv_frozen
{
// FROZEN COPY begin: indra/newview/llviewergenericmessage.cpp, process_generic_message, lines 74-95, blob f3a0f026b8f7233672158cb091b24f4d2c3b1af8
void process_generic_message(LLMessageSystem* msg, void**)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    LLUUID agent_id;
    msg->getUUID("AgentData", "AgentID", agent_id);
    if (agent_id != gAgent.getID())
    {
        LL_WARNS() << "GenericMessage for wrong agent" << LL_ENDL;
        return;
    }

    std::string request;
    LLUUID invoice;
    LLDispatcher::sparam_t strings;
    LLDispatcher::unpackMessage(msg, request, invoice, strings);

    if(!gGenericDispatcher.dispatch(request, invoice, strings))
    {
        LL_WARNS() << "GenericMessage " << request << " failed to dispatch"
            << LL_ENDL;
    }
}
// FROZEN COPY end

// FROZEN COPY begin: indra/newview/llviewergenericmessage.cpp, process_large_generic_message, lines 113-134, blob f3a0f026b8f7233672158cb091b24f4d2c3b1af8
void process_large_generic_message(LLMessageSystem* msg, void**)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    LLUUID agent_id;
    msg->getUUID("AgentData", "AgentID", agent_id);
    if (agent_id != gAgent.getID())
    {
        LL_WARNS() << "GenericMessage for wrong agent" << LL_ENDL;
        return;
    }

    std::string request;
    LLUUID invoice;
    LLDispatcher::sparam_t strings;
    LLDispatcher::unpackLargeMessage(msg, request, invoice, strings);

    if (!gGenericDispatcher.dispatch(request, invoice, strings))
    {
        LL_WARNS() << "GenericMessage " << request << " failed to dispatch"
            << LL_ENDL;
    }
}
// FROZEN COPY end
}

#include "../llinventorymodel.h"

namespace vks_inv_frozen
{
// The inventory part of LLAppViewer::disconnectViewer() (the cache written
// at logout); substitution: the member mSecondInstance is the parameter.
void save_inventory_at_logout(bool mSecondInstance)
{
// FROZEN COPY begin: indra/newview/llappviewer.cpp, LLAppViewer::disconnectViewer, lines 6658-6671, blob 2e3911a737b36b4b4c997576f866fea3d5d5f720
    // save inventory if appropriate
    if (gInventory.isInventoryUsable()
        && gAgent.getID().notNull()) // Shouldn't be null at this stage
    {
        gInventory.cache(gInventory.getRootFolderID(), gAgent.getID());
        if (gInventory.getLibraryRootFolderID().notNull()
            && gInventory.getLibraryOwnerID().notNull()
            && !mSecondInstance) // agent is unique, library isn't
        {
            gInventory.cache(
                gInventory.getLibraryRootFolderID(),
                gInventory.getLibraryOwnerID());
        }
    }
// FROZEN COPY end
}
}
