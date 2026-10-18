// Frozen original LLTracker CPU geometry; LGPL-2.1, like the viewer.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <string>
#include <stdexcept>
#include <vector>
using U8=unsigned char;using F32=float;using U32=unsigned;using S32=int;constexpr F32 F_PI=3.14159265358979323846f,F_TWO_PI=2*F_PI;
template<class T>T llmin(T a,T b){return std::min(a,b);}template<class T>T llmax(T a,T b){return std::max(a,b);}
struct LLVector3{F32 mV[3]={};LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{};LLVector3&operator+=(const LLVector3&v){for(int i=0;i<3;++i)mV[i]+=v.mV[i];return *this;}};
struct LLColor4{F32 mV[4]={};LLColor4()=default;LLColor4(F32 r,F32 g,F32 b,F32 a):mV{r,g,b,a}{};LLColor4 operator+(const LLColor4&v)const{return{mV[0]+v.mV[0],mV[1]+v.mV[1],mV[2]+v.mV[2],mV[3]+v.mV[3]};}LLColor4 operator*(const LLColor4&v)const{return{mV[0]*v.mV[0],mV[1]*v.mV[1],mV[2]*v.mV[2],mV[3]*v.mV[3]};}};
template<class T>T llclamp(T v,T lo,T hi){return std::clamp(v,lo,hi);}
struct VSWorldRenderer{struct Vertex{float x,y,z,nx,ny,nz,u,v,u1=0,v1=0,tx=0,ty=0,tz=0,tw=0,r=1,g=1,b=1,a=1;};};
struct LLRender{enum{TRIANGLE_FAN,TRIANGLES,MM_MODELVIEW};};
struct Sink{std::vector<std::array<float,5>>calls;void matrixMode(int m){calls.push_back({0,float(m)});}void pushMatrix(){calls.push_back({1});}void popMatrix(){calls.push_back({2});}void translatef(float x,float y,float z){calls.push_back({3,x,y,z});}void begin(int mode){calls.push_back({4,float(mode)});}void end(){calls.push_back({5});}void color4fv(const float*v){calls.push_back({6,v[0],v[1],v[2],v[3]});}void vertex3f(float x,float y,float z){calls.push_back({7,x,y,z});}}gGL;
struct LLTracker{static inline bool sCheesyBeacon=false;};enum{TRACKING_AVATAR,TRACKING_LOCATION};int tracking=TRACKING_LOCATION;int getTrackingStatus(){return tracking;}
struct Clock{float time=0;float getElapsedTimeF32()const{return time;}}gRenderStartTime;
struct LLViewerCamera{LLVector3 axis{.6f,-.8f,0};static LLViewerCamera*getInstance(){static LLViewerCamera v;return &v;}LLVector3 getLeftAxis()const{return axis;}};
F32 gold_pulse(F32 t, F32 z, bool tracking_avatar, std::string direction)
{
    if (!LLTracker::sCheesyBeacon)
    {
        return 0.f;
    }

    t *= F_PI;
    if ("DOWN" == direction)
    {
        z += t*64.f - 256.f;
    }
    else
    {
    z -= t*64.f - 256.f;
    }

    F32 a = cosf(z*F_PI/512.f)*10.0f;
    a = llmax(a, 9.9f);
    a -= 9.9f;
    a *= 10.f;
    return a;
}
void gold_shockwave(F32 center_z, F32 t, S32 steps, LLColor4 color)
{
    if (!LLTracker::sCheesyBeacon)
    {
        return;
    }

    t *= 0.6284f/F_PI;

    t -= (F32) (S32) t;

    t = llmax(t, 0.5f);
    t -= 0.5f;
    t *= 2.0f;

    F32 radius = t*16536.f;

    // Inexact, but reasonably fast.
    F32 delta = F_TWO_PI / steps;
    F32 sin_delta = sin( delta );
    F32 cos_delta = cos( delta );
    F32 x = radius;
    F32 y = 0.f;

    LLColor4 ccol = LLColor4(1,1,1,(1.f-t)*0.25f);
    gGL.begin(LLRender::TRIANGLE_FAN);
    gGL.color4fv(ccol.mV);
    gGL.vertex3f(0.f, 0.f, center_z);
    // make sure circle is complete
    steps += 1;

    color.mV[3] = (1.f-t*t);

    gGL.color4fv(color.mV);
    while( steps-- )
    {
        // Successive rotations
        gGL.vertex3f( x, y, center_z );
        F32 x_new = x * cos_delta - y * sin_delta;
        y = x * sin_delta +  y * cos_delta;
        x = x_new;
    }
    gGL.end();
}
void gold_beam(LLVector3 pos_agent, std::string direction, LLColor4 fogged_color, F32 dist)
{
    const F32 MAX_HEIGHT = 5020.f;
    const U32 BEACON_ROWS = 256;

    U32 nRows;
    F32 height;
    F32 rowHeight;

    LLColor4 c_col, col_next, col_edge, col_edge_next;

    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.pushMatrix();

    if ("DOWN" == direction)
    {
        gGL.translatef(pos_agent.mV[0], pos_agent.mV[1], pos_agent.mV[2]);
        gold_shockwave(1024.f, gRenderStartTime.getElapsedTimeF32(), 32, fogged_color);
        height = MAX_HEIGHT - pos_agent.mV[2];
    }
    else
    {
        gGL.translatef(pos_agent.mV[0], pos_agent.mV[1], 0);
        height = pos_agent.mV[2];
    }

    // <FS> [FIRE-35333] OpenSim allows Z coordinates to be negative based on MinSimHeight
    if (height < 0.f)
        nRows = 0;
    else
        nRows = (U32)ceil((BEACON_ROWS * height) / MAX_HEIGHT);
    // </FS>
    if(nRows<2) nRows=2;
    rowHeight = height / nRows;

    gGL.color4fv(fogged_color.mV);

    LLVector3 x_axis = LLViewerCamera::getInstance()->getLeftAxis();
    F32 t = gRenderStartTime.getElapsedTimeF32();

    F32 x = x_axis.mV[0];
    F32 y = x_axis.mV[1];
    // <FS> [FIRE-35333] OpenSim allows Z coordinates to be negative based on MinSimHeight
    //F32 z = 0.f;
    F32 z = llmin(height, 0.f);
    // </FS>
    F32 z_next;

    F32 a,an;
    F32 xa,xan;
    F32 ya,yan;

    bool tracking_avatar = getTrackingStatus() == TRACKING_AVATAR;

    gGL.begin(LLRender::TRIANGLES);

    for (U32 i = 0; i < nRows; i++)
    {
        z_next = z + rowHeight;

        a = gold_pulse(t, z, tracking_avatar, direction);
        an = gold_pulse(t, z_next, tracking_avatar, direction);

        c_col = fogged_color + LLColor4(a, a, a, a);
        col_next = fogged_color + LLColor4(an, an, an, an);
        col_edge = fogged_color * LLColor4(a, a, a, 0.0f);
        col_edge_next = fogged_color * LLColor4(an, an, an, 0.0f);

        a = a + a + 1.f;
        an = an + an + 1.f;

        xa = x*a;
        ya = y*a;
        xan = x*an;
        yan = y*an;

        gGL.color4fv(col_edge.mV);
        gGL.vertex3f(-xa, -ya, z);

        gGL.color4fv(col_next.mV);
        gGL.vertex3f(0, 0, z_next);

        gGL.color4fv(col_edge_next.mV);
        gGL.vertex3f(-xan, -yan, z_next);


        gGL.color4fv(col_edge.mV);
        gGL.vertex3f(-xa, -ya, z);

        gGL.color4fv(c_col.mV);
        gGL.vertex3f(0, 0, z);

        gGL.color4fv(col_next.mV);
        gGL.vertex3f(0, 0, z_next);


        gGL.color4fv(c_col.mV);
        gGL.vertex3f(0, 0, z);

        gGL.color4fv(col_edge_next.mV);
        gGL.vertex3f(xan, yan, z_next);

        gGL.color4fv(col_next.mV);
        gGL.vertex3f(0, 0, z_next);


        gGL.color4fv(c_col.mV);
        gGL.vertex3f(0, 0, z);

        gGL.color4fv(col_edge.mV);
        gGL.vertex3f(xa, ya, z);

        gGL.color4fv(col_edge_next.mV);
        gGL.vertex3f(xan, yan, z_next);

        z += rowHeight;
    }

    gGL.end();
    gGL.popMatrix();
}
SHARED
MESH_SINK
int main(){
for(bool cheesy:{false,true})for(bool avatar:{false,true})for(float t:{0.f,.15f,3.5f,10.f,123.f})for(float z:{-100.f,0.f,1.f,1024.f,5020.f,6000.f})for(const auto& direction:{std::string("DOWN"),std::string("UP")}){
LLTracker::sCheesyBeacon=cheesy;tracking=avatar?TRACKING_AVATAR:TRACKING_LOCATION;gRenderStartTime.time=t;
const LLVector3 p{123.5f,-75.f,z};const LLColor4 c{.1f,.2f,.3f,.4f};gGL.calls.clear();gold_beam(p,direction,c,350.f);Sink native;
vs_tracker_beam(native,p,direction,c,350.f,[t]{return t;},LLViewerCamera::getInstance()->getLeftAxis(),avatar,cheesy);
assert(native.calls==gGL.calls);for(const auto&call:native.calls)for(float v:call)assert(std::isfinite(v));
VSTrackerMeshSink mesh(10000);vs_tracker_beam(mesh,p,direction,c,350.f,[t]{return t;},LLViewerCamera::getInstance()->getLeftAxis(),avatar,cheesy);
// Independently replay the frozen original GL stream through fixed-function
// translate/color and GL triangle/fan assembly, including U8 truncation.
std::vector<VSWorldRenderer::Vertex> expected,fan;std::array<float,3>offset{},saved{};std::array<float,4>color{};int mode=-1;
auto append=[&](const VSWorldRenderer::Vertex&v){expected.push_back(v);};
for(const auto&call:gGL.calls){
 switch(int(call[0])){
 case 1:saved=offset;break;case 2:offset=saved;break;
 case 3:for(int k=0;k<3;++k)offset[k]+=call[k+1];break;
 case 4:mode=int(call[1]);fan.clear();break;
 case 6:for(int k=0;k<4;++k)color[k]=float(static_cast<U8>(std::clamp(call[k+1],0.f,1.f)*255.f))/255.f;break;
 case 7:{VSWorldRenderer::Vertex v{call[1]+offset[0],call[2]+offset[1],call[3]+offset[2],0,0,1,0,0};v.r=color[0];v.g=color[1];v.b=color[2];v.a=color[3];
 if(mode==LLRender::TRIANGLES)append(v);else{fan.push_back(v);if(fan.size()>=3){append(fan[0]);append(fan[fan.size()-2]);append(fan.back());}}break;}
 }
}
assert(mesh.vertices.size()==expected.size()&&mesh.indices.size()==expected.size());for(std::size_t i=0;i<expected.size();++i){const auto&a=mesh.vertices[i];const auto&b=expected[i];assert(a.x==b.x&&a.y==b.y&&a.z==b.z&&a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a);assert(mesh.indices[i]==i);}

}
}
