// Original sphere/cone CPU geometry, shared with native tool painters.
// LGPL-2.1, like the viewer.
#pragma once
#include <array>
#include <cmath>
inline std::array<float,3> vs_sphere_point(float latitude,float longitude)
{
    constexpr float twoPi=6.283185307179586476925286766559f;
    return {std::sin(twoPi*latitude)*std::cos(twoPi*longitude),
            std::sin(twoPi*latitude)*std::sin(twoPi*longitude),std::cos(twoPi*latitude)};
}
template<class Paint>void vs_render_sphere_geometry(Paint& paint)
{
    constexpr unsigned latitude=20,longitude=30;
    // Match the original process-wide immutable sphere cache, rather than
    // recomputing trigonometry for every selected light/pose handle per frame.
    static const auto points=[]
    {
        std::array<std::array<std::array<float,3>,longitude+1>,latitude+1> result;
        for(unsigned lat=0;lat<=latitude;++lat)for(unsigned lon=0;lon<=longitude;++lon)
            result[lat][lon]=vs_sphere_point(float(lat)/latitude,float(lon)/longitude);
        return result;
    }();
    paint.begin(0); // LLRender::TRIANGLES
    for(unsigned lat=0;lat<latitude;++lat)for(unsigned lon=0;lon<longitude;++lon)
    {
        paint.vertex3fv(points[lat][lon].data());paint.vertex3fv(points[lat][lon+1].data());paint.vertex3fv(points[lat+1][lon].data());
        paint.vertex3fv(points[lat+1][lon].data());paint.vertex3fv(points[lat][lon+1].data());paint.vertex3fv(points[lat+1][lon+1].data());
    }
    paint.end();
}
template<class Paint>void vs_render_cone_geometry(Paint& paint,int sides)
{
    constexpr float pi=3.1415926535897932384626433832795f;
    paint.begin(2);paint.vertex3f(0,0,0); // LLRender::TRIANGLE_FAN
    for(int i=0;i<sides;++i)
    {
        const float a=float(i)/sides*pi*2.f;
        paint.vertex3f(std::cos(a)*.5f,std::sin(a)*.5f,-.5f);
    }
    paint.vertex3f(std::cos(0.f)*.5f,std::sin(0.f)*.5f,-.5f);paint.end();
    paint.begin(2);paint.vertex3f(0,0,.5f);
    for(int i=0;i<sides;++i)
    {
        const float a=float(i)/sides*pi*2.f;
        paint.vertex3f(std::cos(a)*.5f,std::sin(a)*.5f,-.5f);
    }
    paint.vertex3f(std::cos(0.f)*.5f,std::sin(0.f)*.5f,-.5f);paint.end();
}
