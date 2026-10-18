// Reference parcel boundary geometry transposed from llglsandbox.cpp. LGPL-2.1.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <initializer_list>
namespace VSParcelGeometry
{
enum class Direction { West, South, East, North };
struct Vertex { float x,y,z,u=0,v=0; bool textured=false; };
// Keep vertices region-local; texture coordinates retain the reference floating
// agent origin so neighboring regions and camera-origin changes remain aligned.
template<class Height>
std::array<Vertex,6> segment(float x1,float y1,float x2,float y2,float height,
    Direction direction,float width,float regionHeight,float originX,float originY,
    bool absoluteHeight,Height&& resolveHeight)
{
    const float z1=resolveHeight(std::min(x1,width-.1f),std::min(y1,regionHeight-.1f));
    const float z2=resolveHeight(std::min(x2,width-.1f),std::min(y2,regionHeight-.1f));
    if(height<1.f)
    {
        const float top1=absoluteHeight?height:z1+height;
        const float top2=absoluteHeight?height:z2+height;
        return {{{x1,y1,top1},{x1,y1,z1},{x2,y2,z2},
                 {x1,y1,top1},{x2,y2,z2},{x2,y2,top2}}};
    }
    float along1,along2;
    switch(direction)
    {
    case Direction::West:along1=y1+originY;along2=y2+originY;break;
    case Direction::South:along1=x1+originX;along2=x2+originX;break;
    case Direction::East:along1=y2+originY;along2=y1+originY;break;
    default:along1=x2+originX;along2=x1+originX;break;
    }
    const float u1=along1*.5f+.5f,u2=along2*.5f+.5f;
    const float top=absoluteHeight?height:std::max(z2+height,z1+height);
    return {{{x1,y1,z1,u1,z1*.5f,true},{x2,y2,z2,u2,z2*.5f,true},
             {x2,y2,top,u2,top*.5f,true},{x1,y1,z1,u1,z1*.5f,true},
             {x2,y2,top,u2,top*.5f,true},{x1,y1,top,u1,top*.5f,true}}};
}
// Same two-triangle interpolation and edge-buffer policy as LLSurface.
// Sample receives integer coordinates in actual received terrain heights.
template<class Sample>
float terrainHeight(float x,float y,float spacing,int columns,int rows,Sample&& sample)
{
    const float inverse=1.f/spacing;
    if(!(x>=0 && x<=columns*spacing && y>=0 && y<=rows*spacing))return 0.f;
    const int left=int(std::floor(x*inverse)),bottom=int(std::floor(y*inverse));
    const int right=left+1<columns?left+1:left,top=bottom+1<rows?bottom+1:bottom;
    const float lb=sample(left,bottom),rb=sample(right,bottom),lt=sample(left,top),rt=sample(right,top);
    float dx=x-left*spacing,dy=y-bottom*spacing;
    if(dy>dx){dy*=lt-lb;dx*=rt-lt;}
    else{dx*=rb-lb;dy*=rt-rb;}
    return lb+(dx+dy)*inverse;
}
struct ScreenPoint { float x,y,z; };
// Homogeneous clipping precedes extrusion. Two physical-pixel parcel posts must
// work on Vulkan devices without the optional wideLines feature.
inline std::optional<std::array<ScreenPoint,6>> screenLine(std::array<float,4> a,
    std::array<float,4> b,float pixels,unsigned width,unsigned height)
{
    if(!width || !height || !std::isfinite(pixels) || pixels<=0)return {};
    for(float value:a)if(!std::isfinite(value))return {};
    for(float value:b)if(!std::isfinite(value))return {};
    float first=0,last=1;
    for(unsigned axis=0;axis<3;++axis)
        for(float sign:{-1.f,1.f})
        {
            const float av=a[3]+sign*a[axis],bv=b[3]+sign*b[axis];
            if(av<0 && bv<0)return {};
            if(av<0)first=std::max(first,av/(av-bv));
            if(bv<0)last=std::min(last,av/(av-bv));
        }
    if(first>last)return {};
    const auto initial=a;
    for(unsigned axis=0;axis<4;++axis)
    {
        a[axis]=initial[axis]+first*(b[axis]-initial[axis]);
        b[axis]=initial[axis]+last*(b[axis]-initial[axis]);
    }
    if(a[3]<=0 || b[3]<=0)return {};
    ScreenPoint start{a[0]/a[3],a[1]/a[3],a[2]/a[3]};
    ScreenPoint end{b[0]/b[3],b[1]/b[3],b[2]/b[3]};
    const float dx=(end.x-start.x)*width,dy=(end.y-start.y)*height;
    const float length=std::hypot(dx,dy);
    if(length<=1e-12f)return {};
    const float ox=-dy/length*pixels/width,oy=dx/length*pixels/height;
    return std::array<ScreenPoint,6>{{{start.x-ox,start.y-oy,start.z},
        {end.x-ox,end.y-oy,end.z},{end.x+ox,end.y+oy,end.z},
        {start.x-ox,start.y-oy,start.z},{end.x+ox,end.y+oy,end.z},
        {start.x+ox,start.y+oy,start.z}}};
}

}
