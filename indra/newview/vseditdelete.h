// Original delete controller shared by real selection peers. LGPL-2.1.
#pragma once
#include "llnotifications.h"
#include "lluictrlfactory.h"
#include "llfloatertools.h"
#include "rlvhandler.h"
template<class Manager,class Confirm>void vs_delete_selection(Manager& manager,Confirm confirm)
{
// [RLVa:KB] - Checked: 2010-03-23 (RLVa-1.2.0e) | Added: RLVa-1.2.0a
    if ( (rlv_handler_t::isEnabled()) && (!manager.canDeleteOrReturn()) )
    {
        make_ui_sound("UISndInvalidOp");
        if (!gFloaterTools || !gFloaterTools->getVisible())
            manager.deselectAll();
        return;
    }
// [/RLVa:KB]

    S32 deleteable_count = 0;

    bool locked_but_deleteable_object = false;
    bool no_copy_but_deleteable_object = false;
    bool all_owned_by_you = true;

    for (auto iter = manager.getSelection()->begin();
         iter != manager.getSelection()->end(); iter++)
    {
        auto* obj = (*iter)->getObject();

        if( obj->isAttachment() )
        {
            continue;
        }

        deleteable_count++;

        // Check to see if you can delete objects which are locked.
        if(!obj->permMove())
        {
            locked_but_deleteable_object = true;
        }
        if(!obj->permCopy())
        {
            no_copy_but_deleteable_object = true;
        }
        if(!obj->permYouOwner())
        {
            all_owned_by_you = false;
        }
    }

    if( 0 == deleteable_count )
    {
        make_ui_sound("UISndInvalidOp");
        return;
    }

    LLNotification::Params params("ConfirmObjectDeleteLock");
    params.functor.function(confirm);

    if(locked_but_deleteable_object ||
       no_copy_but_deleteable_object ||
       !all_owned_by_you)
    {
        // convert any transient pie-menu selections to full selection so this operation
        // has some context
        // NOTE: if user cancels delete operation, this will potentially leave objects selected outside of build mode
        // but this is ok, if not ideal
        manager.convertTransient();

        //This is messy, but needed to get all english our of the UI.
        if(locked_but_deleteable_object && !no_copy_but_deleteable_object && all_owned_by_you)
        {
            //Locked only
            params.name("ConfirmObjectDeleteLock");
        }
        else if(!locked_but_deleteable_object && no_copy_but_deleteable_object && all_owned_by_you)
        {
            //No Copy only
            params.name("ConfirmObjectDeleteNoCopy");
        }
        else if(!locked_but_deleteable_object && !no_copy_but_deleteable_object && !all_owned_by_you)
        {
            //not owned only
            params.name("ConfirmObjectDeleteNoOwn");
        }
        else if(locked_but_deleteable_object && no_copy_but_deleteable_object && all_owned_by_you)
        {
            //locked and no copy
            params.name("ConfirmObjectDeleteLockNoCopy");
        }
        else if(locked_but_deleteable_object && !no_copy_but_deleteable_object && !all_owned_by_you)
        {
            //locked and not owned
            params.name("ConfirmObjectDeleteLockNoOwn");
        }
        else if(!locked_but_deleteable_object && no_copy_but_deleteable_object && !all_owned_by_you)
        {
            //no copy and not owned
            params.name("ConfirmObjectDeleteNoCopyNoOwn");
        }
        else
        {
            //locked, no copy and not owned
            params.name("ConfirmObjectDeleteLockNoCopyNoOwn");
        }

        LLNotifications::instance().add(params);
    }
    else
    {
        LLNotifications::instance().forceResponse(params, 0);
    }
}
