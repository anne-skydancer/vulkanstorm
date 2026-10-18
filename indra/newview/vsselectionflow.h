// Renderer-neutral LLToolSelect branch flow. Peer adapters supply actual owners.
// LGPL-2.1, like the viewer. Original ordering is protected by reference traces.
#pragma once
#include "llkeyboard.h"
#include <array>
#include <optional>

template<class Peer,class Pick>
auto vs_handle_object_selection(Peer& peer,const Pick& pick,bool ignoreGroup,bool temporary,bool selectRoot)
{
    auto object=peer.object(pick);
    if(selectRoot)object=peer.rootEdit(object);
    if(object && peer.rlvEnabled())
    {
        if(!peer.canEdit(object))
        {
            if(!temporary)return peer.selection();
            if(peer.inBuildMode())peer.leaveBuildMode();
        }
        if(peer.fartouch() && (!peer.attachment(object) || !peer.youOwner(object)))
        {
            const auto limit=peer.fartouchDistanceSquared();
            if(peer.distanceToObjectSquared(object)>limit)
            {
                if(peer.distanceToSurfaceSquared(pick)>1.5f*1.5f)
                {
                    if(peer.buildVisible() && peer.mask(pick)!=MASK_SHIFT && peer.mask(pick)!=MASK_CONTROL)peer.deselectAll();
                    return peer.selection();
                }
                if(peer.inBuildMode())peer.leaveBuildMode();
            }
        }
    }
    struct Options
    {
        Peer& peer;
        const std::array<const char*,4> names{{"SelectOwnedOnly","SelectMovableOnly","FSSelectCopyableOnly","FSSelectLockedOnly"}};
        std::array<bool,4> previous;
        bool active;
        Options(Peer& value,bool temporary):peer(value),previous{{value.setting(names[0]),value.setting(names[1]),value.setting(names[2]),value.setting(names[3])}},active(temporary || value.allowSelectAvatar())
        {if(active){for(auto name:names)peer.setting(name,false);peer.forceSelection(true);}}
        void restore(){if(active){active=false;for(unsigned i=0;i<names.size();++i)peer.setting(names[i],previous[i]);peer.forceSelection(false);}}
        ~Options(){restore();}
    }options(peer,temporary);
    auto finish=[&]{options.restore();return peer.selection();};
    if(!peer.current())return finish();
    bool extend=peer.mask(pick)==MASK_SHIFT || peer.mask(pick)==MASK_CONTROL;
    if(!object)
    {
        if(peer.scriptIcon(pick))peer.showScriptDebug(pick);
        else if(!extend)peer.deselectAll();
    }
    else
    {
        bool selected=peer.selected(object);
        if(selected && peer.faceCount(object)>0 && !peer.containsAllFaces(object))
        {
            const auto selectedFace=peer.faceSelection(object,pick);
            if(selectedFace && !*selectedFace && peer.hasMediaFocus())
            {peer.clearMediaFocus();selected=false;extend=true;}
        }
        if(!peer.current())return finish();
        if(extend)
        {
            if(selected)
            {
                if(ignoreGroup)peer.deselectOnly(object);
                else peer.deselectFamily(object);
            }
            else
            {
                if(ignoreGroup)peer.selectOnly(object,-1,-1,-1);
                else peer.selectFamily(object);
            }
        }
        else
        {
            auto zoom=peer.hudZoom();
            if(!selected || ignoreGroup)peer.deselectAll();
            if(!peer.current())return finish();
            if(ignoreGroup)peer.selectOnly(object,-1,peer.gltfNode(pick),peer.gltfPrimitive(pick));
            else peer.selectFamily(object);
            peer.hudZoom(zoom);
        }
        if(!peer.current())return finish();
        if(!peer.focusOnAvatar() && !peer.selfAttachment(object) && !peer.self(object) && peer.setting("FSTurnAvatarToSelectedObject"))
            peer.turnToSelection(object);
        if(temporary && !selected && peer.current())peer.transientRootAndChildren(object);
    }
    return finish();
}
