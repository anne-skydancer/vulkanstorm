// SPDX-License-Identifier: LGPL-2.1-or-later
/*[EXTRA_CODE_HERE]*/
struct Vertex { vec4 position, normalMaterial, color, uvGlow; };
struct Entry { uvec4 key; uvec4 value; };
layout(std430,binding=0) readonly buffer ParticleVertices { Vertex vertices[]; };
layout(std430,binding=1) readonly buffer ParticleOrder { Entry entries[]; };
layout(std430,binding=2) readonly buffer ParticleIntervals { uvec4 intervals[]; };
uniform int particle_interval, particle_glow_interleaved;
uniform mat4 modelview_matrix, modelview_projection_matrix;
uniform mat3 normal_matrix;
uniform float near_clip;
out vec3 vary_fragcoord, vary_position, vary_norm;
// Shared atmospheric helper objects declare these inputs even though this
// deferred path computes atmosphere per fragment. Satisfy strict stage linking.
out vec3 vary_AdditiveColor, vary_AtmosAttenuation;
out vec4 vertex_color;
out vec2 vary_texcoord0;
out float particle_glow;
struct ParticleMaterial { sampler2D diffuse; uint blend; uint flags; };
layout(std430,binding=3) readonly buffer ParticleMaterials { ParticleMaterial particle_materials[]; };
flat out sampler2D particle_texture;
flat out uint particle_blend, particle_flags, particle_glow_pass;
void main()
{
    uint draw=uint(gl_DrawIDARB);
    uint rank=intervals[particle_interval].x+draw/(particle_glow_interleaved!=0?2u:1u);
    particle_glow_pass=particle_glow_interleaved!=0 ? (draw&1u) : 0u;
    uint slot=entries[rank].value.x;
    const uint corners[6]=uint[6](0u,1u,2u,2u,1u,3u);
    Vertex v=vertices[slot*4u+corners[gl_VertexID]];
    // Material identity depends only on DrawID, never on vertex/instance ID.
    // ARB_bindless_texture requires dynamically uniform sampler handles.
    uint material=floatBitsToUint(vertices[slot*4u].normalMaterial.w);
    particle_texture=particle_materials[material].diffuse;
    particle_blend=particle_materials[material].blend;
    particle_flags=particle_materials[material].flags;
    vary_AdditiveColor=vec3(0);
    vary_AtmosAttenuation=vec3(1);
    vec4 pos=modelview_matrix*v.position;
    gl_Position=modelview_projection_matrix*v.position;
    vary_position=pos.xyz;
    vary_fragcoord=gl_Position.xyz+vec3(0,0,near_clip);
    vary_norm=normalize(normal_matrix*v.normalMaterial.xyz);
    vary_texcoord0=v.uvGlow.xy;
    vertex_color=v.color;
    particle_glow=v.uvGlow.z;
}
