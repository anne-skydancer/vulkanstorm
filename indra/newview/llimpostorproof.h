// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// MEASURING BUILD ONLY (branch codex/impostor-pbr-proof, never merged): the in-world proof of the impostor material
// and blending fix. While RenderImpostorProofLog is on, every impostor generation logs one record per impostored
// item (the avatar, the item, its material path and how often it was blended) to
// <logs>/impostor_proof/items.jsonl and saves the finished impostor as a PNG under
// <logs>/impostor_proof/<before|after>/. "before" is RenderImpostorFullPBR = FALSE (the old behaviour), "after" is TRUE.

#ifndef LL_LLIMPOSTORPROOF_H
#define LL_LLIMPOSTORPROOF_H

#include "stdtypes.h"

class LLDrawInfo;
class LLVOAvatar;

namespace LLImpostorProof
{
    // RenderImpostorFullPBR (TRUE: the fix; FALSE: the behaviour before it)
    bool fullPBR();
    // true once after RenderImpostorFullPBR changed, so every impostor must regenerate
    bool settingChanged();

    extern bool sRecording; // between beginGeneration and endGeneration of a logged impostor

    void beginGeneration(LLVOAvatar* avatar);
    void noteDrawImpl(const LLDrawInfo& info);
    // reads the impostor target (still bound) back, writes the PNG and the records
    void endGeneration(LLVOAvatar* avatar, S32 width, S32 height);

    inline void noteDraw(const LLDrawInfo& info)
    {
        if (sRecording)
        {
            noteDrawImpl(info);
        }
    }
}

#define VKS_IMPOSTOR_PROOF_DRAW(info) LLImpostorProof::noteDraw(info)

#endif // LL_LLIMPOSTORPROOF_H
