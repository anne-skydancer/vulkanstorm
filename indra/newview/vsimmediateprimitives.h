// Primitive assembly and clip-plane policy for original immediate geometry.
// LGPL-2.1, like the viewer.
#pragma once
#include "vsimmediategeometry.h"

template<class Emit>
bool vs_immediate_primitives(const VSImmediateGeometry::Batch& batch,const Emit& emit)
{
    using Vertex=VSImmediateGeometry::Vertex;
    const auto interpolate=[](const Vertex& a,const Vertex& b,float t)
    {return Vertex{glm::mix(a.position,b.position,t),glm::mix(a.normal,b.normal,t),glm::mix(a.uv,b.uv,t),glm::mix(a.color,b.color,t)};};
    const auto distance=[](const Vertex& v,const glm::vec4& plane){return glm::dot(glm::vec4(v.position,1),plane);};
    const auto triangle=[&](const Vertex& a,const Vertex& b,const Vertex& c)
    {
        std::array<Vertex,16> polygon,next;polygon[0]=a;polygon[1]=b;polygon[2]=c;unsigned count=3;
        for(const auto& plane:batch.state.planes)if(plane)
        {
            unsigned size=0;
            for(unsigned i=0;i<count;++i)
            {
                const auto& before=polygon[(i+count-1)%count];const auto& vertex=polygon[i];
                const float d0=distance(before,*plane),d1=distance(vertex,*plane);
                if((d0>=0)!=(d1>=0))next[size++]=interpolate(before,vertex,d0/(d0-d1));
                if(d1>=0)next[size++]=vertex;
            }
            polygon=next;count=size;if(count<3)return true;
        }
        for(unsigned i=1;i+1<count;++i)if(!emit(polygon[0],polygon[i],polygon[i+1],3))return false;
        return true;
    };
    const auto line=[&](Vertex a,Vertex b)
    {
        for(const auto& plane:batch.state.planes)if(plane)
        {
            const float d0=distance(a,*plane),d1=distance(b,*plane);
            if(d0<0 && d1<0)return true;
            if(d0<0)a=interpolate(a,b,d0/(d0-d1));
            else if(d1<0)b=interpolate(a,b,d0/(d0-d1));
        }
        return emit(a,b,b,2);
    };
    const auto& vertices=batch.vertices;const auto count=vertices.size();
    switch(batch.primitive)
    {
    case VSImmediateGeometry::Triangles:
        for(std::size_t i=0;i+2<count;i+=3)if(!triangle(vertices[i],vertices[i+1],vertices[i+2]))return false;
        break;
    case VSImmediateGeometry::TriangleStrip:
        for(std::size_t i=2;i<count;++i)
            if(!triangle(vertices[i-2+(i&1)],vertices[i-1-(i&1)],vertices[i]))return false;
        break;
    case VSImmediateGeometry::TriangleFan:
        for(std::size_t i=2;i<count;++i)if(!triangle(vertices[0],vertices[i-1],vertices[i]))return false;
        break;
    case VSImmediateGeometry::Lines:
        for(std::size_t i=0;i+1<count;i+=2)if(!line(vertices[i],vertices[i+1]))return false;
        break;
    case VSImmediateGeometry::LineStrip:case VSImmediateGeometry::LineLoop:
        for(std::size_t i=1;i<count;++i)if(!line(vertices[i-1],vertices[i]))return false;
        if(batch.primitive==VSImmediateGeometry::LineLoop && count>1 && !line(vertices.back(),vertices.front()))return false;
        break;
    case VSImmediateGeometry::Points:
        for(const auto& point:vertices)
        {
            bool inside=true;for(const auto& plane:batch.state.planes)if(plane && distance(point,*plane)<0){inside=false;break;}
            if(inside && !emit(point,point,point,1))return false;
        }
        break;
    default:return false;
    }
    return true;
}
