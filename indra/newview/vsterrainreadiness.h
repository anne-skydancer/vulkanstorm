// Shared terrain texture readiness policy from LLTerrainMaterials. LGPL-2.1.
#pragma once
#include <cstdint>
enum class VSTerrainTextureState { Unloaded, Insufficient, Empty, Ready };
inline VSTerrainTextureState vs_terrain_texture_state(int discard,int width,int height,int components)
{
    if(discard<0)return VSTerrainTextureState::Unloaded;
    if(discard!=0 && (width<128 || height<128))return VSTerrainTextureState::Insufficient;
    return components==0?VSTerrainTextureState::Empty:VSTerrainTextureState::Ready;
}
