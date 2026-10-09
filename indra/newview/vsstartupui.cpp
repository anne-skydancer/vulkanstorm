// Required native login/progress/alert controls; optional viewer UI remains gated. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsstartupui.h"
#include "vsuiadmission.h"
#include "llrootview.h"
#include "llpopupview.h"
#include "llpanel.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "lliconctrl.h"
#include "lllineeditor.h"
#include "lltexteditor.h"
#include "llviewertexteditor.h"
#include "lltextbox.h"
#include "lllayoutstack.h"
#include "llprogressbar.h"
#include "llscrollbar.h"
#include "llscrollcontainer.h"
#include "llscrolllistctrl.h"
#include "llviewborder.h"
#include "llfloater.h"
#include "llmediactrl.h"
#include "llradiogroup.h"
#include "llwindowshade.h"
#include "llmodaldialog.h"
#include "llfloatertos.h"
#include "lltoast.h"
#include "lltoastalertpanel.h"
#include "llscreenchannel.h"
#include "llresizebar.h"
#include "llresizehandle.h"
#include "lldraghandle.h"
std::unique_ptr<VSUIAdmission> vs_startup_ui_admission()
{
    return std::make_unique<VSUIAdmission>(
        [](const std::type_info& t)
        {
            return t == typeid(LLView) || t == typeid(LLPopupView) || t == typeid(LLRootView) || t == typeid(LLUICtrl) || t == typeid(LLPanel) ||
                   t == typeid(LLButton) || t == typeid(LLCheckBoxCtrl) || t == typeid(LLComboBox) || t == typeid(LLIconCtrl) ||
                   t == typeid(LLLineEditor) || t == typeid(LLTextEditor) || t == typeid(LLViewerTextEditor) || t == typeid(LLTextBox) || t == typeid(LLLayoutStack) ||
                   t == typeid(LLLayoutPanel) || t == typeid(LLProgressBar) || t == typeid(LLScrollbar) || t == typeid(LLScrollContainer) ||
                   t == typeid(LLScrollListCtrl) || t == typeid(LLViewBorder) || t == typeid(LLFloaterView) || t == typeid(LLFloater) ||
                   t == typeid(LLModalDialog) || t == typeid(LLFloaterTOS) ||
                   t == typeid(LLNotificationsUI::LLToast) || t == typeid(LLToastAlertPanel) ||
                   t == typeid(LLNotificationsUI::LLScreenChannel) || t == typeid(LLNotificationsUI::LLScreenChannelBase) ||
                   t == typeid(LLWindowShade) || t == typeid(LLResizeBar) || t == typeid(LLResizeHandle) ||
                   t == typeid(LLDragHandleTop) || t == typeid(LLDragHandleLeft) || t == typeid(LLMediaCtrl) || t == typeid(LLRadioGroup) || t == LLRadioGroup::itemType();
        },
        [](std::string_view name) { return name == "message_critical" || name == "message_tos"; },
        [](std::string_view name) { return name == "progress_view" || name == "progress_view_mini" || name == "popup_holder"; });
}
