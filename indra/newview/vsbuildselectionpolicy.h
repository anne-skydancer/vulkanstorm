// Shared original Build selection policy over genuine renderer peers. LGPL-2.1.
#pragma once
#include "llpermissions.h"
#include "llsaleinfo.h"
#include "llviewercontrol.h"
#include "rlvhandler.h"

template<class Selection>
bool vs_build_selectGetModify(Selection& selection)
{
    for (auto iter = selection.getSelection()->begin(); iter != selection.getSelection()->end(); iter++)
    {
        auto* node   = *iter;
        auto* object = node->getObject();
        if (!object || !node->mValid)
        {
            return false;
        }
        if (!object->permModify())
        {
            return false;
        }
    }
    return true;
}

template<class Selection>
bool vs_build_selectGetRootsModify(Selection& selection)
{
    for (auto iter = selection.getSelection()->root_begin(); iter != selection.getSelection()->root_end(); iter++)
    {
        auto* node   = *iter;
        auto* object = node->getObject();
        if (!node->mValid)
        {
            return false;
        }
        if (!object->permModify())
        {
            return false;
        }
    }

    return true;
}

template<class Selection>
bool vs_build_selectGetNonPermanentEnforced(Selection& selection)
{
    for (auto iter = selection.getSelection()->begin(); iter != selection.getSelection()->end(); iter++)
    {
        auto* node   = *iter;
        auto* object = node->getObject();
        if (!object || !node->mValid)
        {
            return false;
        }
        if (object->isPermanentEnforced())
        {
            return false;
        }
    }
    return true;
}

template<class Selection>
bool vs_build_selectGetRootsNonPermanentEnforced(Selection& selection)
{
    for (auto iter = selection.getSelection()->root_begin(); iter != selection.getSelection()->root_end(); iter++)
    {
        auto* node   = *iter;
        auto* object = node->getObject();
        if (!node->mValid)
        {
            return false;
        }
        if (object->isPermanentEnforced())
        {
            return false;
        }
    }

    return true;
}

template<class Selection>
bool vs_build_selectGetNonPathfinding(Selection& selection)
{
    for (auto iter = selection.getSelection()->begin(); iter != selection.getSelection()->end(); iter++)
    {
        auto* node   = *iter;
        auto* object = node->getObject();
        if (!object || !node->mValid)
        {
            return false;
        }
        if (object->flagObjectPermanent() || object->flagCharacter())
        {
            return false;
        }
    }
    return true;
}

template<class Selection>
bool vs_build_selectGetRootsNonPathfinding(Selection& selection)
{
    for (auto iter = selection.getSelection()->root_begin(); iter != selection.getSelection()->root_end(); iter++)
    {
        auto* node   = *iter;
        auto* object = node->getObject();
        if (!node->mValid)
        {
            return false;
        }
        if (object->flagObjectPermanent() || object->flagCharacter())
        {
            return false;
        }
    }

    return true;
}

template<class Selection>
bool vs_build_selectGetPermanent(Selection& selection)
{
    for (auto iter = selection.getSelection()->begin(); iter != selection.getSelection()->end(); iter++)
    {
        auto* node   = *iter;
        auto* object = node->getObject();
        if (!object || !node->mValid)
        {
            return false;
        }
        if (!object->flagObjectPermanent())
        {
            return false;
        }
    }
    return true;
}

template<class Selection>
bool vs_build_selectGetRootsPermanent(Selection& selection)
{
    for (auto iter = selection.getSelection()->root_begin(); iter != selection.getSelection()->root_end(); iter++)
    {
        auto* node   = *iter;
        auto* object = node->getObject();
        if (!node->mValid)
        {
            return false;
        }
        if (!object->flagObjectPermanent())
        {
            return false;
        }
    }

    return true;
}

template<class Selection>
bool vs_build_selectGetCharacter(Selection& selection)
{
    for (auto iter = selection.getSelection()->begin(); iter != selection.getSelection()->end(); iter++)
    {
        auto* node   = *iter;
        auto* object = node->getObject();
        if (!object || !node->mValid)
        {
            return false;
        }
        if (!object->flagCharacter())
        {
            return false;
        }
    }
    return true;
}

template<class Selection>
bool vs_build_selectGetRootsCharacter(Selection& selection)
{
    for (auto iter = selection.getSelection()->root_begin(); iter != selection.getSelection()->root_end(); iter++)
    {
        auto* node   = *iter;
        auto* object = node->getObject();
        if (!node->mValid)
        {
            return false;
        }
        if (!object->flagCharacter())
        {
            return false;
        }
    }

    return true;
}

template<class Selection>
bool vs_build_selectGetRootsCopy(Selection& selection)
{
    for (auto iter = selection.getSelection()->root_begin(); iter != selection.getSelection()->root_end(); iter++)
    {
        auto* node   = *iter;
        auto* object = node->getObject();
        if (!node->mValid)
        {
            return false;
        }
        if (!object->permCopy())
        {
            return false;
        }
    }
    return true;
}

template<class Selection>
bool vs_build_selectGetRootsTransfer(Selection& selection)
{
    for (auto iter = selection.getSelection()->root_begin(); iter != selection.getSelection()->root_end(); iter++)
    {
        auto* node   = *iter;
        auto* object = node->getObject();
        if (!node->mValid)
        {
            return false;
        }
        if (!object->permTransfer())
        {
            return false;
        }
    }
    return true;
}

template<class Selection>
bool vs_build_selectGetPerm(Selection& selection, U8 which_perm, U32* mask_on, U32* mask_off)
{
    U32  mask;
    U32  mask_and  = 0xffffffff;
    U32  mask_or   = 0x00000000;
    bool all_valid = false;

    for (auto iter = selection.getSelection()->root_begin(); iter != selection.getSelection()->root_end(); iter++)
    {
        auto* node = *iter;

        if (!node->mValid)
        {
            all_valid = false;
            break;
        }

        all_valid = true;

        switch (which_perm)
        {
            case PERM_BASE:
                mask = node->mPermissions->getMaskBase();
                break;
            case PERM_OWNER:
                mask = node->mPermissions->getMaskOwner();
                break;
            case PERM_GROUP:
                mask = node->mPermissions->getMaskGroup();
                break;
            case PERM_EVERYONE:
                mask = node->mPermissions->getMaskEveryone();
                break;
            case PERM_NEXT_OWNER:
                mask = node->mPermissions->getMaskNextOwner();
                break;
            default:
                mask = 0x0;
                break;
        }
        mask_and &= mask;
        mask_or |= mask;
    }

    if (all_valid)
    {
        // ...true through all ANDs means all true
        *mask_on = mask_and;

        // ...false through all ORs means all false
        *mask_off = ~mask_or;
        return true;
    }
    else
    {
        *mask_on  = 0;
        *mask_off = 0;
        return false;
    }
}

template<class Selection>
bool vs_build_selectGetSaleInfo(Selection& selection, LLSaleInfo& result_sale_info)
{
    bool       first = true;
    LLSaleInfo sale_info;
    for (auto iter = selection.getSelection()->root_begin(); iter != selection.getSelection()->root_end(); iter++)
    {
        auto* node = *iter;
        if (!node->mValid)
        {
            return false;
        }

        if (first)
        {
            sale_info = node->mSaleInfo;
            first     = false;
        }
        else
        {
            sale_info.accumulate(node->mSaleInfo);
        }
    }

    result_sale_info = sale_info;

    return true;
}

template<class Selection>
void vs_build_selectGetAggregateSaleInfo(Selection& selection, U32& num_for_sale, bool& is_for_sale_mixed, bool& is_sale_price_mixed,
                                         S32& total_sale_price, S32& individual_sale_price)
{
    num_for_sale          = 0;
    is_for_sale_mixed     = false;
    is_sale_price_mixed   = false;
    total_sale_price      = 0;
    individual_sale_price = 0;

    // Empty set.
    if (selection.getSelection()->root_begin() == selection.getSelection()->root_end())
        return;

    auto*      node                  = *(selection.getSelection()->root_begin());
    const bool first_node_for_sale   = node->mSaleInfo.isForSale();
    const S32  first_node_sale_price = node->mSaleInfo.getSalePrice();

    for (auto iter = selection.getSelection()->root_begin(); iter != selection.getSelection()->root_end(); iter++)
    {
        auto*      node            = *iter;
        const bool node_for_sale   = node->mSaleInfo.isForSale();
        const S32  node_sale_price = node->mSaleInfo.getSalePrice();

        // Set mixed if the fields don't match the first node's fields.
        if (node_for_sale != first_node_for_sale)
            is_for_sale_mixed = true;
        if (node_sale_price != first_node_sale_price)
            is_sale_price_mixed = true;

        if (node_for_sale)
        {
            total_sale_price += node_sale_price;
            num_for_sale++;
        }
    }

    individual_sale_price = first_node_sale_price;
    if (is_for_sale_mixed)
    {
        is_sale_price_mixed   = true;
        individual_sale_price = 0;
    }
}

template<class Selection, class Sitting>
bool vs_build_selectGetEditMoveLinksetPermissions(Selection& selection, bool& move, bool& modify, Sitting&& sittingOnObjectRoot)
{
    move   = true;
    modify = true;
    // <FS:Ansariel> gSavedSettings replacement
    // bool selecting_linked_set = !gSavedSettings.getBOOL("EditLinkedParts");
    static LLCachedControl<bool> editLinkedParts(gSavedSettings, "EditLinkedParts");
    bool                         selecting_linked_set = !editLinkedParts();
    // </FS:Ansariel>

    for (auto iter = selection.getSelection()->begin(); iter != selection.getSelection()->end(); iter++)
    {
        auto* nodep  = *iter;
        auto* object = nodep->getObject();
        if (!object || !nodep->mValid)
        {
            move   = false;
            modify = false;
            return false;
        }

        auto* root_object         = object->getRootEdit();
        bool  this_object_movable = false;
        if (object->permMove() && !object->isPermanentEnforced() && ((root_object == NULL) || !root_object->isPermanentEnforced()) &&
            (object->permModify() || selecting_linked_set))
        {
            this_object_movable = true;

            // [RLVa:KB] - Checked: 2010-03-31 (RLVa-1.2.0c) | Modified: RLVa-1.0.0g
            if ((rlv_handler_t::isEnabled()) && ((gRlvHandler.hasBehaviour(RLV_BHVR_UNSIT)) || (gRlvHandler.hasBehaviour(RLV_BHVR_SITTP))))
            {
                if (sittingOnObjectRoot(object))
                    move = modify = false;
            }
            // [/RLVa:KB]
        }
        move   = move && this_object_movable;
        modify = modify && object->permModify();

        // <FS:Ansariel> Optimize: Once move and modify are both false, there is no reason to continue checking - neither will become true
        // again
        if (!move && !modify)
        {
            return true;
        }
        // </FS:Ansariel>
    }

    return true;
}

template<class Selection>
bool vs_build_selectGetAggregateTexturePermissions(Selection& selection, LLAggregatePermissions& result_perm)
{
    bool                   first = true;
    LLAggregatePermissions perm;
    for (auto iter = selection.getSelection()->root_begin(); iter != selection.getSelection()->root_end(); iter++)
    {
        auto* node = *iter;
        if (!node->mValid)
        {
            return false;
        }

        LLAggregatePermissions t_perm = node->getObject()->permYouOwner() ? node->mAggregateTexturePermOwner : node->mAggregateTexturePerm;
        if (first)
        {
            perm  = t_perm;
            first = false;
        }
        else
        {
            perm.aggregate(t_perm);
        }
    }

    result_perm = perm;

    return true;
}
