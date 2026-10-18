// Original LLSelectMgr admission with lazy renderer-peer inputs. LGPL-2.1.
#pragma once
// Object getters and setting reads retain original ordering and short circuiting.
template<class Object,class Settings,class SelectType,class PCode>
bool vs_can_select_object(const Object& object,bool forceSelection,bool ignore_select_owned,
                          const SelectType& selectedType,PCode surfacePatch,const Settings& settings)
{
    // Never select dead objects
    if (!object.present() || object.dead())
    {
        return false;
    }

    if (forceSelection)
    {
        return true;
    }

    if(!ignore_select_owned)
    {
        if ((settings("SelectOwnedOnly") && !object.youOwner()) ||
                (settings("SelectMovableOnly") && (!object.move() ||  object.permanent())))
        {
            // only select my own objects
            return false;
        }

        // <FS:Ansariel> FIRE-14593: Option to select only copyable objects
        if (!object.copy() && settings("FSSelectCopyableOnly"))
        {
            return false;
        }
        // </FS:Ansariel>
        // <FS:Ansariel> FIRE-17696: Option to select only locked objects
        if (settings("FSSelectLockedOnly") && object.move() && !object.permanent())
        {
            return false;
        }
        // </FS:Ansariel>
    }

    // Can't select orphans
    if (object.orphaned()) return false;

    // Can't select avatars
    if (object.avatar()) return false;

    // Can't select land
    if (object.pcode() == surfacePatch) return false;

    const auto selection_type = object.selectionType();
    if (selectedType.hasSelection() && selectedType.type() != selection_type) return false;

    return true;
}
