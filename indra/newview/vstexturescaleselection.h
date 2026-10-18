// Actual native selected-node texture scale snapshot ownership. LGPL-2.1.
#pragma once
#include "vsobjectselection.h"
bool vs_capture_selected_texture_ratios(VSObjectSelection&,const VSWorldScene::ObjectHandle&);
bool vs_adjust_selected_textures_by_scale(VSObjectSelection&,bool sendToSimulator,bool stretch);

#include <map>
struct VSPreparedTextureAdjustment;
// Compute all original UV/material edits against the proposed local scales;
// no scene edits or network operations occur until publish succeeds.
std::shared_ptr<VSPreparedTextureAdjustment> vs_prepare_selected_texture_adjustment(
    VSObjectSelection&,const std::map<LLUUID,LLVector3>& proposedScales,bool sendToSimulator,bool stretch);
bool vs_publish_prepared_texture_adjustment(const std::shared_ptr<VSPreparedTextureAdjustment>&);
