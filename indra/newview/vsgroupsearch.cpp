// Native text UI host for the production skinned group directory panel. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsgroupsearch.h"
#include "llfloaterreg.h"
#include "llpaneldirgroups.h"
#include "llscrolllistctrl.h"
#include "llbutton.h"

bool VSGroupSearch::postBuild()
{
    auto* panel = findChild<LLPanelDirGroups>("panel_dir_groups");
    if (!panel)
    {
        LL_WARNS("NativeUI") << "Required production group directory panel is missing" << LL_ENDL;
        return false;
    }
    auto* profile = getChild<LLButton>("open_profile_btn");
    profile->setEnabled(false);
    profile->setCommitCallback([panel](LLUICtrl*, const LLSD&) { panel->openProfile(); });
    auto* results = panel->getChild<LLScrollListCtrl>("results");
    results->setCommitCallback([panel, profile](LLUICtrl* control, const LLSD&)
    {
        LLPanelDirBrowser::onCommitList(control, panel);
        profile->setEnabled(panel->getChild<LLScrollListCtrl>("results")->getFirstSelected() != nullptr);
    });
    results->setDoubleClickCallback([panel] { panel->openProfile(); });
    return true;
}

void vs_show_group_search()
{
    static const bool registered = []
    {
        LLFloaterReg::add("vs_group_search", "floater_vs_group_search.xml", &LLFloaterReg::build<VSGroupSearch>);
        return true;
    }();
    (void)registered;
    LLFloaterReg::showInstance("vs_group_search");
}
