// Frozen OpenGL/reference implementation from f2ddbb2aa7. LGPL-2.1.
bool LLSelectMgr::canSelectObject(LLViewerObject* object, bool ignore_select_owned)
{
    // Never select dead objects
    if (!object || object->isDead())
    {
        return false;
    }

    if (mForceSelection)
    {
        return true;
    }

    if(!ignore_select_owned)
    {
        if ((gSavedSettings.getBOOL("SelectOwnedOnly") && !object->permYouOwner()) ||
                (gSavedSettings.getBOOL("SelectMovableOnly") && (!object->permMove() ||  object->isPermanentEnforced())))
        {
            // only select my own objects
            return false;
        }

        // <FS:Ansariel> FIRE-14593: Option to select only copyable objects
        if (!object->permCopy() && gSavedSettings.getBOOL("FSSelectCopyableOnly"))
        {
            return false;
        }
        // </FS:Ansariel>
        // <FS:Ansariel> FIRE-17696: Option to select only locked objects
        if (gSavedSettings.getBOOL("FSSelectLockedOnly") && object->permMove() && !object->isPermanentEnforced())
        {
            return false;
        }
        // </FS:Ansariel>
    }

    // Can't select orphans
    if (object->isOrphaned()) return false;

    // Can't select avatars
    if (object->isAvatar()) return false;

    // Can't select land
    if (object->getPCode() == LLViewerObject::LL_VO_SURFACE_PATCH) return false;

    ESelectType selection_type = getSelectTypeForObject(object);
    if (mSelectedObjects->getObjectCount() > 0 && mSelectedObjects->mSelectType != selection_type) return false;

    return true;
}
