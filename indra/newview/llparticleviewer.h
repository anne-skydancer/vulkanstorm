// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "lluuid.h"
#include "v3math.h"
#include <vector>
class LLViewerPart;
class LLViewerRegion;
class LLGLSLShader;
namespace LLParticleViewer
{
bool active();
bool acceptsBirths();
bool beginFrame();
void add(LLViewerPart* part); // consumes a birth record, never retains CPU state
void finishFrame(float dt);
void shift(const LLVector3& offset);
void kill(U32 source);
void killOwner(const LLUUID& owner);
void cleanupRegion(LLViewerRegion* region);
void destroyGL();
bool beginView(const std::vector<float>& boundaries, bool hud);
bool draw(U32 interval, LLGLSLShader& shader, bool depthWrite, bool depthOnly, bool glow);
bool pick(const LLVector3& start, const LLVector3& end, LLUUID& owner, LLUUID& object);
}
