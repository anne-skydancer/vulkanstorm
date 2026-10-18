// Original LLDebugBeacon geometry gold; LGPL-2.1, like the viewer.
#include <cassert>
#include <array>
#include <vector>
using F32=float;constexpr int VX=0,VY=1,VZ=2;
struct LLVector3{float mV[3];};
struct Sink{std::vector<std::array<float,3>>v;void vertex3f(float x,float y,float z){v.push_back({x,y,z});}}gGL;
void draw_cross_lines(const LLVector3& center, F32 dx, F32 dy, F32 dz)
{
    gGL.vertex3f(center.mV[VX] - dx, center.mV[VY], center.mV[VZ]);
    gGL.vertex3f(center.mV[VX] + dx, center.mV[VY], center.mV[VZ]);
    gGL.vertex3f(center.mV[VX], center.mV[VY] - dy, center.mV[VZ]);
    gGL.vertex3f(center.mV[VX], center.mV[VY] + dy, center.mV[VZ]);
    gGL.vertex3f(center.mV[VX], center.mV[VY], center.mV[VZ] - dz);
    gGL.vertex3f(center.mV[VX], center.mV[VY], center.mV[VZ] + dz);
}
void draw_line_cube(F32 width, const LLVector3& center)
{
    width = 0.5f * width;
    gGL.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] + width);
    gGL.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] + width);
    gGL.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] + width);
    gGL.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] + width);
    gGL.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] + width);
    gGL.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] + width);
    gGL.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] + width);
    gGL.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] + width);

    gGL.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] - width);
    gGL.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] - width);
    gGL.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] - width);
    gGL.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] - width);
    gGL.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] - width);
    gGL.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] - width);
    gGL.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] - width);
    gGL.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] - width);

    gGL.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] + width);
    gGL.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] - width);
    gGL.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] + width);
    gGL.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] - width);
    gGL.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] + width);
    gGL.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] - width);
    gGL.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] + width);
    gGL.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] - width);
}
KERNEL
int main(){for(auto center:{LLVector3{{0,0,0}},LLVector3{{-50,32,-13}},LLVector3{{1023,2047,57}}})for(float delta:{0.f,.5f,2.f,50.f}){
Sink actual;gGL.v.clear();draw_cross_lines(center,delta,delta,delta);vs_debug_beacon_cross(actual,center,delta,delta,delta);assert(actual.v==gGL.v&&actual.v.size()==6);
actual.v.clear();gGL.v.clear();draw_line_cube(.10f,center);vs_debug_beacon_cube(actual,.10f,center);assert(actual.v==gGL.v&&actual.v.size()==24);
}}
