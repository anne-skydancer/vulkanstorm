// SPDX-License-Identifier: LGPL-2.1-or-later
/*[EXTRA_CODE_HERE]*/
layout(binding=1,r32f) uniform readonly image2D particle_depth;
void main() { gl_FragDepth=imageLoad(particle_depth,ivec2(gl_FragCoord.xy)).r; }
