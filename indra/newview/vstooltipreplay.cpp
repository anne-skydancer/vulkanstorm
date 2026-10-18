// Actual shared tooltip widget/timer/native-pointer replay. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vstooltipreplay.h"
#include "llbutton.h"
#include "llrootview.h"
#include "lluictrlfactory.h"
#include "lltooltip.h"
#include "llviewerwindow.h"
#include "llwindow.h"
#include "llui.h"
#include "llfocusmgr.h"
#include "llframetimer.h"
#include <memory>
#include <stdexcept>
namespace {
struct TooltipReplay {
    LLHandle<LLView> button;
    F64 deadline=0;
    unsigned step=0;
    LLCoordGL point;
};
std::unique_ptr<TooltipReplay> state;
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
constexpr const char* first="Actual native tooltip: first control";
constexpr const char* second="Actual native tooltip: replacement control";
void move(const LLCoordGL& scaled)
{
    LLCoordGL physical;
    LLUI::getInstance()->screenPointToGL(scaled.mX,scaled.mY,&physical.mX,&physical.mY);
    gViewerWindow->handleMouseMove(gViewerWindow->getWindow(),physical,MASK_NONE);
}
void click(const LLCoordGL& scaled)
{
    LLCoordGL physical;
    LLUI::getInstance()->screenPointToGL(scaled.mX,scaled.mY,&physical.mX,&physical.mY);
    gViewerWindow->handleMouseDown(gViewerWindow->getWindow(),physical,MASK_NONE);
    gViewerWindow->handleMouseUp(gViewerWindow->getWindow(),physical,MASK_NONE);
}
}
bool vs_tooltip_replay_tick(LLSD& evidence)
{
    if(evidence["connected_native_tooltip_controls"].asBoolean())return true;
    if(!state)
    {
        require(gToolTipView && gToolTipView->getParent()==gViewerWindow->getRootView(),"Native tooltip has no actual root owner");
        require(!gFocusMgr.getMouseCapture(),"Tooltip qualification requires uncaptured real window input");
        state=std::make_unique<TooltipReplay>();state->deadline=LLFrameTimer::getTotalSeconds()+30;
        auto* root=gViewerWindow->getRootView();
        LLButton::Params p;p.name="native_tooltip_qualification";p.label="Tooltip qualification";
        p.rect=LLRect(30,root->getLocalRect().getHeight()/2+40,230,root->getLocalRect().getHeight()/2);
        p.tool_tip=first;
        auto* button=LLUICtrlFactory::create<LLButton>(p,root);
        require(button!=nullptr,"Real tooltip qualification button was not admitted");root->sendChildToFront(button);root->sendChildToFront(gToolTipView);
        state->button=button->getHandle();const auto rect=button->calcScreenRect();state->point=LLCoordGL(rect.getCenterX(),rect.getCenterY());
        // Actual changed-position input resets shared idle delay. No direct
        // manager show/unblock or overridden tooltip-delay setting is used.
        move(LLCoordGL(state->point.mX+7,state->point.mY+7));move(state->point);
        return false;
    }
    require(LLFrameTimer::getTotalSeconds()<state->deadline,"Actual native tooltip qualification timed out");
    auto* button=state->button.get();require(button!=nullptr,"Tooltip source control retired too early");
    auto& mgr=LLToolTipMgr::instance();
    if(state->step==0)
    {
        if(!mgr.toolTipVisible())return false;
        std::string text;mgr.getToolTipMessage(text);require(text==first,"Tooltip did not come from genuine hovered control");
        click(state->point);require(!mgr.toolTipVisible(),"Actual pointer click did not block existing tooltip");
        button->setToolTip(LLStringExplicit(second));move(LLCoordGL(state->point.mX+11,state->point.mY+8));move(state->point);
        state->step=1;return false;
    }
    if(!mgr.toolTipVisible())return false;
    std::string text;mgr.getToolTipMessage(text);require(text==second,"Tooltip did not replace retired Params using actual idle timer");
    click(state->point);require(!mgr.toolTipVisible(),"Replacement tooltip survived actual pointer click");
    delete button;state.reset();evidence["connected_native_tooltip_controls"]=true;return true;
}
