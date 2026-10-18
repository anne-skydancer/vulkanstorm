// Original LLSelectMgrApplyFlags policy over real object peers. LGPL-2.1.
#pragma once
template<class Object, class Root>
bool vs_build_apply_flags(Object* object, U32 flags, bool state, Root&& rootOf)
{
    if (object->permModify())
    {
        if (object->isRoot())
            object->setFlags(flags, state);
        else if ((FLAGS_WORLD & flags) && rootOf(object)->isSelected())
            object->setFlagsWithoutUpdate(FLAGS_WORLD & flags, state);
    }
    return true;
}
