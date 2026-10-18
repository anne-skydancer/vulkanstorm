// Original selected-object permission policy with lazy renderer peers. LGPL-2.1.
#pragma once
#include "llpermissions.h"
template<class Object,class Agent>
bool vs_selected_allow_operation(const Object* mObject,const LLPermissions* mPermissions,
                                 PermissionBit op,U64 group_proxy_power,Agent& agent)
{
    // Extract ownership.
    bool object_is_group_owned = false;
    LLUUID object_owner_id;
    mPermissions->getOwnership(object_owner_id, object_is_group_owned);

    // Operations on invalid or public objects is not allowed.
    if (!mObject || (mObject->isDead()) || !mPermissions->isOwned())
    {
        return false;
    }

    // The transfer permissions can never be given through proxy.
    if (PERM_TRANSFER == op)
    {
        // The owner of an agent-owned object can transfer to themselves.
        if ( !object_is_group_owned
            && (agent.getID() == object_owner_id) )
        {
            return true;
        }
        else
        {
            // Otherwise check aggregate permissions.
            return mObject->permTransfer();
        }
    }

    if (PERM_MOVE == op
        || PERM_MODIFY == op)
    {
        // only owners can move or modify their attachments
        // no proxy allowed.
        if (mObject->isAttachment() && object_owner_id != agent.getID())
        {
            return false;
        }
    }

    // Calculate proxy_agent_id and group_id to use for permissions checks.
    // proxy_agent_id may be set to the object owner through group powers.
    // group_id can only be set to the object's group, if the agent is in that group.
    LLUUID group_id = LLUUID::null;
    LLUUID proxy_agent_id = agent.getID();

    // Gods can always operate.
    if (agent.isGodlike())
    {
        return true;
    }

    // Check if the agent is in the same group as the object.
    LLUUID object_group_id = mPermissions->getGroup();
    if (object_group_id.notNull() &&
        agent.isInGroup(object_group_id))
    {
        // Assume the object's group during this operation.
        group_id = object_group_id;
    }

    // Only allow proxy powers for PERM_COPY if the actual agent can
    // receive the item (ie has PERM_TRANSFER permissions).
    // NOTE: op == PERM_TRANSFER has already been handled, but if
    // that ever changes we need to BLOCK proxy powers for PERM_TRANSFER.  DK 03/28/06
    if (PERM_COPY != op || mPermissions->allowTransferTo(agent.getID()))
    {
        // Check if the agent can assume ownership through group proxy or agent-granted proxy.
        if (   ( object_is_group_owned
                && agent.hasPowerInGroup(object_owner_id, group_proxy_power))
                // Only allow proxy for move, modify, and copy.
                || ( (PERM_MOVE == op || PERM_MODIFY == op || PERM_COPY == op)
                    && (!object_is_group_owned
                        && agent.isGrantedProxy(*mPermissions))))
        {
            // This agent is able to assume the ownership role for this operation.
            proxy_agent_id = object_owner_id;
        }
    }

    // We now have max ownership information.
    if (PERM_OWNER == op)
    {
        // This this was just a check for ownership, we can now return the answer.
        return proxy_agent_id == object_owner_id;
    }

    // check permissions to see if the agent can operate
    return (mPermissions->allowOperationBy(op, proxy_agent_id, group_id));
}
