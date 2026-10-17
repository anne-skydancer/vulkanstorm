"""Embed the shared reference terrain sampler with native Vulkan bindings."""
from pathlib import Path
import argparse
root=Path(__file__).resolve().parents[1]
reference=(root/'indra/newview/app_settings/shaders/class1/deferred/pbrterrainUtilF.glsl').read_text()
# Preserve the reference functions and their complete LGPL/MIT attribution.
reference=reference.replace('in vec3 vary_vertex_normal;', '#define vary_vertex_normal worldNormal')
for name in ('tex_col','tex_orm','tex_vNt','tex_emissive'):
    reference=reference.replace('sampler2D '+name,'texture2D '+name)
    reference=reference.replace('texture('+name+',','texture(sampler2D('+name+',terrainSampler),')
prolog=r"""
layout(location=0) in vec2 texUV;
layout(location=1) in vec3 light;
layout(location=2) in vec3 worldNormal;
layout(location=3) in vec3 worldPosition;
layout(location=4) in vec2 texUV1;
layout(location=5) in vec4 worldTangent;
layout(location=6) in vec4 faceColor;
layout(location=0) out vec4 result;
layout(std140) uniform WorldView { mat4 projection; vec4 sun; vec4 ambient; vec4 tint; vec4 material; vec4 regionOffset; vec4 surfaceFlags; vec4 surfaceParams; vec4 emissive; vec4 specular; vec4 channelSets; vec4 normalOcclusion; vec4 normalTangent; vec4 uvTransform[10]; };
layout(std140) uniform TerrainView {
 vec4 terrainBase[4]; vec4 terrainFactors[4]; vec4 terrainEmission[4];
 vec4 terrainTransform[8]; vec4 terrainTangent[4]; vec4 terrainSettings; vec4 terrainPaint;
};
uniform texture2D terrainColor[4];
uniform texture2D terrainNormal[4];
uniform texture2D terrainORM[4];
uniform texture2D terrainEmissive[4];
uniform texture2D terrainBlend;
uniform sampler terrainSampler;
uniform sampler terrainClamp;
#define TERRAIN_PLANAR_TEXTURE_SAMPLE_COUNT 3
#define TERRAIN_TRIPLANAR_BLEND_FACTOR terrainSettings.z
vec3 srgb_to_linear(vec3 c) {return mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c));}
vec3 linear_to_srgb(vec3 c) {return mix(c*12.92,1.055*pow(max(c,vec3(0)),vec3(1.0/2.4))-.055,step(vec3(.0031308),c));}
"""
wrapper=r"""
vec2 terrainUV(int layer,vec2 p) {
 vec4 m=terrainTransform[layer*2]; vec2 o=terrainTransform[layer*2+1].xy;
 vec2 uv=vec2(m.x*p.x+m.z*p.y,m.y*p.x+m.w*p.y)+o;
 return vec2(uv.x,1.0-uv.y);
}
PBRMix terrainLayer(int layer,texture2D colorTex,texture2D normalTex,texture2D ormTex,texture2D emissiveTex) {
 TerrainCoord coord;
 vec3 position=worldPosition-regionOffset.xyz;
 coord[0]=vec4(terrainUV(layer,position.xy),terrainUV(layer,position.yz));
 coord[1]=vec4(terrainUV(layer,position.xz*vec2(-1,1)),terrainUV(layer,position.yz*vec2(-1,1)));
 coord[2]=vec4(terrainUV(layer,position.xz),0,0);
 TerrainTriplanar tw=_t_triplanar();
 if(terrainSettings.y<2) {tw.weight=vec3(0,0,1);tw.type=SAMPLE_Z;}
 PBRMix sampled=terrain_sample_pbr(coord,tw,colorTex,ormTex
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL
 ,normalTex,terrainTangent[layer].z
#endif
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE
 ,emissiveTex
#endif
 );
 sampled=multiply_factors_pbr(sampled,terrainBase[layer]
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION
 ,terrainFactors[layer].xyz
#else
 ,terrainFactors[layer].yz
#endif
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE
 ,terrainEmission[layer].rgb
#endif
 );
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL
 vec3 n=normalize(worldNormal);
 vec3 t=abs(n.z)>.00001?normalize(vec3(1,0,-n.x/n.z)):normalize(cross(vec3(0,1,0),n));
 vec3 b=cross(n,t);
 vec4 rotation=terrainTangent[layer];
 vec3 tangent=normalize(rotation.x*t+rotation.y*b);
 sampled.vNt=normalize(mat3(tangent,rotation.z*cross(n,tangent),n)*sampled.vNt);
#endif
 return sampled;
}
void main() {
 vec4 weights;
 if(terrainPaint.x>.5) {
  vec2 uv=vec2(texUV.x,1.0-texUV.y)/terrainPaint.y;
  uv.y=1.0-uv.y;
  weights=get_terrain_usage_from_weight3(texture(sampler2D(terrainBlend,terrainClamp),uv).rgb).weight;
 } else {
  float a1=texture(sampler2D(terrainBlend,terrainClamp),vec2(texUV1.x,1.0-texUV1.y)).a;
  float a2=texture(sampler2D(terrainBlend,terrainClamp),vec2(texUV1.x-2,1.0-texUV1.y)).a;
  float af=texture(sampler2D(terrainBlend,terrainClamp),vec2(texUV1.x-1,1.0-texUV1.y)).a;
  weights=get_terrain_mix_weights(a1,a2,af).weight;
 }
 PBRMix mixed=init_pbr_mix();
#define LAYER(I) if(weights[I]>0) mixed=mix_pbr(mixed,terrainLayer(I,terrainColor[I],terrainNormal[I],terrainORM[I],terrainEmissive[I]),weights[I]);
 LAYER(0) LAYER(1) LAYER(2) LAYER(3)
#undef LAYER
 float cutoff=dot(weights,vec4(terrainFactors[0].w,terrainFactors[1].w,terrainFactors[2].w,terrainFactors[3].w));
 if(mixed.col.a<cutoff) discard;
 vec3 n=normalize(
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_NORMAL
 mixed.vNt
#else
 worldNormal
#endif
 )*(gl_FrontFacing?1.0:-1.0);
 vec3 illumination=material.x>.5?vec3(1):ambient.rgb+(1-ambient.rgb)*max(dot(n,normalize(sun.xyz)),0);
 // As in the object foundation, retain roughness/metallic inputs for R4/R5's
 // G-buffer/BRDF. Do not invent a replacement lighting model at R3.
 vec3 color=mixed.col.rgb*illumination;
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_OCCLUSION
 color*=mixed.orm.x;
#endif
#if TERRAIN_PBR_DETAIL >= TERRAIN_PBR_DETAIL_EMISSIVE
 color+=mixed.emissive;
#endif
 result=vec4(linear_to_srgb(color),1);
}
"""
header='// Native terrain material adapter around the shared reference sampler. LGPL-2.1.\n#pragma once\n#include <string>\nnamespace VSTerrainShader {\n'
source=prolog+reference+wrapper
chunks=[source[i:i+8000] for i in range(0,len(source),8000)]
header+='inline std::string source(int detail) { return "#define TERRAIN_PBR_DETAIL "+std::to_string(detail)+"\\n" + '+' + '.join('std::string(R"VS_TERRAIN('+part+')VS_TERRAIN")' for part in chunks)+'; }\n}\n'
destination=root/'indra/newview/vsterrainshader.h'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check',action='store_true')
args=parser.parse_args()
if args.check:
    if not destination.exists() or destination.read_text()!=header:
        raise SystemExit('Native terrain shader is stale; run scripts/generate_vulkan_terrain_shader.py')
    print('PASS: native terrain uses the current reference sampler')
else:
    destination.write_text(header)
