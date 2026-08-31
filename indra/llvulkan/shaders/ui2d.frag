#version 450
// 2D UI quad fragment shader — solid-color path (milestone 1). The textured
// variant (sampler at set 0 binding 0) lands with the descriptor-set plumbing
// in milestone 3; this untextured path lets the pipeline validate and the
// diff harness verify a known rect first.

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vColor;
}
