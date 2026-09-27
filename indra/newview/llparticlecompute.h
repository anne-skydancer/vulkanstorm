// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <vector>

class LLFace;
class LLVertexBuffer;

namespace LLParticleCompute
{
// Required shader: compile during viewer shader loading, including reloads.
bool initGL();
// Queue particle-partition faces in existing sorted order. The first buffer
// binding flushes all queued groups. No CPU geometry or position readback.
bool generate(LLVertexBuffer& buffer, const std::vector<LLFace*>& faces);
bool flush();
void destroyGL();
}
