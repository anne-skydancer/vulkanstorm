// Native Build Face selection actions over the retained actual selected owners.
// LGPL-2.1, matching LLSelectMgr.
#include "llviewerprecompiledheaders.h"
#include "vseditselection.h"
#include "vspbrfacepolicy.h"
#include "llagent.h"
#include "lltool.h"
#include "lltooldraganddrop.h"
#include "llnotificationsutil.h"
#include "llgltfmateriallist.h"
#include "llhudeffect.h"
#include "vsobjectcontext.h"

void VSEditSelection::selectionSetAlphaOnly(F32 alpha)
{
    const auto current=guard();
    for(const auto& entry:mStorage)
    {
        if(!current())return;
        const auto& token=entry->source;
        auto object=VSMaterialObject::find(token);
        if(!object || !mSelection->node(token))continue;
        const auto count=llmin(object.getNumTEs(),object.getNumFaces());
        for(S32 side=0;side<count;++side)
        {
            if(!current())return;
            if(mSelection->contains(token,side) && object.permModify())
                if(const auto* te=object.getTE(side))
                {
                    LLColor4 color=te->getColor();color.mV[VALPHA]=alpha;
                    object.setTEColor(U8(side),color);
                }
        }
    }
    // Original LLSelectMgrSendFunctor sends once for every selected object,
    // including unchanged non-modifiable objects.
    for(const auto& entry:mStorage)
    {
        if(!current())return;
        if(mSelection->node(entry->source))
            VSMaterialObject::find(entry->source).sendTEUpdate();
    }
}

bool VSEditSelection::selectionSetGLTFMaterial(const LLUUID& material)
{
    const auto selectedCurrent=guard();if(!mSelection || !selectedCurrent())return false;
    // Retain the genuine item across no-copy source removal and inventory callbacks.
    LLPointer<LLViewerInventoryItem> item=gInventory.getItem(material);
    const LLUUID itemAsset=item?item->getAssetUUID():LLUUID::null;
    const auto current=[selectedCurrent,item,itemAsset]{return selectedCurrent() && (!item || item->getAssetUUID()==itemAsset);};
    if(item && !item->getPermissions().allowOperationBy(PERM_COPY,gAgent.getID()) && getNumNodes()>1)
    {
        LLNotificationsUtil::add("FailedToApplyGLTFNoCopyToMultiple");return false;
    }
    const bool restricted=item && item->getPermissions().getOwner()!=ALEXANDRIA_LINDEN_ID &&
        (!item->getPermissions().allowOperationBy(PERM_COPY,gAgent.getID()) ||
         !item->getPermissions().allowOperationBy(PERM_TRANSFER,gAgent.getID()) ||
         !item->getPermissions().allowOperationBy(PERM_MODIFY,gAgent.getID()));
    // Refuse unavailable protocol admission before any no-copy transfer or
    // predictive material mutation.
    for(const auto& entry:mStorage)
    {
        const auto object=VSMaterialObject::find(entry->source);
        if(!current() || !object)return false;
        if(object.permModify() && object.task().getCapability("ModifyMaterialParams").empty())return false;
    }
    bool success=true;
    for(const auto& entry:mStorage)
    {
        if(!current())return false;
        const auto token=entry->source;auto object=VSMaterialObject::find(token);
        if(!object || !mSelection->node(token))continue;
        if(!object.permModify()){success=false;continue;}
        const auto targetCurrent=[current,object]{return current() && !object.task().getCapability("ModifyMaterialParams").empty();};
        bool copied=false;
        const S32 count=llmin(object.getNumTEs(),object.getNumFaces());
        for(S32 side=0;side<count;++side)
        {
            if(!current())return false;
            if(!mSelection->contains(token,side))continue;
            if(restricted)
            {
                LLUUID asset=item->getAssetUUID();if(asset.isNull())asset=BLANK_MATERIAL_ASSET_ID;
                if(!copied && asset.notNull())copied=LLToolDragAndDrop::handleDropMaterialProtections(object.task(),item,LLToolDragAndDrop::SOURCE_AGENT,LLUUID::null);
                if(!targetCurrent())return false;
                if(!copied){success=false;break;}
                if(!object.setRenderMaterialID(side,asset)){success=false;break;}
            }
            else
                success=vs_apply_pbr_face(&object,side,item,material,
                    [](VSMaterialObject* source,LLViewerInventoryItem* value)
                    {return LLToolDragAndDrop::handleDropMaterialProtections(source->task(),value,LLToolDragAndDrop::SOURCE_AGENT,LLUUID::null);},targetCurrent) && success;
        }
    }
    if(restricted)LLGLTFMaterialList::flushUpdates();
    // Match the original success short circuit: no send loop after a failed face.
    if(success)
        for(const auto& entry:mStorage)
        {
            if(!current())return false;
            auto object=VSMaterialObject::find(entry->source);
            if(!object || !mSelection->node(entry->source))continue;
            if(!object.permModify()){success=false;continue;}
            if(!item)vs_native_beam_effect(gAgent.getID(),object.getID(),LLColor4U(gAgent.getEffectColor()),LL_HUD_DUR_SHORT);
            dialog_refresh_all();if(!current())return false;object.sendTEUpdate();
        }
    LLGLTFMaterialList::flushUpdates();return success && current();
}
