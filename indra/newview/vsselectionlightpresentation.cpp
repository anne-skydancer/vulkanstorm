// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsselectionlightpresentation.h"
#include "vsselectionlightgeometry.h"
#include "vsworldscene.h"
#include "vsnativesession.h"
#include "vsobjectselection.h"
#include "vseditselection.h"
#include "llselectmgr.h"
#include "lltoolmgr.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llrender.h"
VSSelectionLightPresentation::VSSelectionLightPresentation(VSWorldRenderer&renderer,VSWorldScene&world):
    mWorld(world),mPaint(renderer,[&world](std::size_t bytes){return world.reserveSpatialBytes(bytes);},
        [this]{return mCurrent && mCurrent();}){}
void VSSelectionLightPresentation::append(VSNativeSession&session,bool showHUD,std::vector<VSWorldRenderer::Packet>&packets)
{
    if(!LLSelectMgr::sRenderLightRadius || !LLToolMgr::instanceExists() || !LLToolMgr::instance().inEdit())return;
    const auto selection=session.retainObjectSelection();if(!selection || !selection->objectCount())return;
    const auto account=session.accountGuard(),selected=selection->selectionGuard();
    mCurrent=[account,selected]{return account() && selected();};
    const auto type=selection->selectionType();
    const bool hud=type.type==std::optional<VSObjectSelection::SelectionType>(VSObjectSelection::SelectionType::HUD);
    if(hud && !showHUD)return;
    auto& paint=mPaint.begin();
    {
        auto blend=paint.blend(true);auto cull=paint.cull(true);auto depth=paint.depth(true,false);
        paint.matrixMode(LLRender::MM_MODELVIEW);paint.pushMatrix();
        paint.hud(hud);
        if(hud)paint.scalef(gAgentCamera.mHUDCurZoom,gAgentCamera.mHUDCurZoom,gAgentCamera.mHUDCurZoom);
        const auto origin=gAgent.getPosGlobalFromAgent(LLVector3::zero);
        // The same selected node owner supplies actual current simulator light
        // parameters; no display flag is used as a substitute for light data.
        const bool complete=selection->visitSelected([&](const auto& token,const auto&)
        {
            if(!mCurrent())return false;
            VSEditObject object(token);if(!object || !object.getIsLight())return true;
            const auto frame=mWorld.focusObject(token,origin);if(!frame || !frame->current())return true;
            const LLColor4 color(object.getLightSRGBColor(),.5f);
            vs_selected_light_geometry(paint,frame->renderPosition.mV,object.getLightRadius(),color.mV);
            return mCurrent();
        });
        if(!complete)return;
        paint.popMatrix();
    }
    if(mCurrent())mPaint.finish(packets);
}
void VSSelectionLightPresentation::reset(){mCurrent={};mPaint.retire();}
