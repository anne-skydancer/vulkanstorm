// Dispatch the original Build controller over genuine renderer-owned selections.
// LGPL-2.1, like the viewer.
#pragma once
#include "llselectmgr.h"
#include "llviewerwindow.h"
#if VS_NATIVE_VULKAN
#include "vseditselection.h"
#endif
template<class F>
void vs_with_edit_selection(F&& action)
{
#if VS_NATIVE_VULKAN
    if (gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        VSEditSelection selection;
        action(&selection);
        return;
    }
#endif
    action(LLSelectMgr::getInstance());
}
template<class Selection, class Predicate>
bool vs_build_apply_objects(Selection* selection, Predicate&& predicate)
{
    bool result = true;
    for (auto iter = selection->getSelection()->begin(); iter != selection->getSelection()->end(); ++iter)
        if (auto* object = (*iter)->getObject())
        {
            const bool value = predicate(object);
            result           = result && value;
        }
    return result;
}
