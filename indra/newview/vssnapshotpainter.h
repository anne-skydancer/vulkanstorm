// Snapshot preview's original immediate draw stream on real GL/native images. LGPL-2.1.
#pragma once
#include "llrender.h"
#include "llrender2dutils.h"
#include "lluiimage.h"
#include "llviewertexture.h"
#include <array>
#include <cmath>
#include <stdexcept>
class VSSnapshotPainter
{
    const bool native=LLRender2D::isNativeUI();
    std::array<F32,6> transform{1,0,0,1,0,0};
    std::array<std::array<F32,6>,4> stack;
    unsigned depth=0;
    LLUIImagePtr image;
    LLColor4 color=LLColor4::white;
    F32 u=0,v=0;
    unsigned count=0;
    LLUIImage::native_triangles_t vertices;
public:
    void bind(LLViewerTexture* gl,LLUIImagePtr vk)
    {if(native)image=std::move(vk);else gGL.getTexUnit(0)->bind(gl);}
    void unbind(){if(native)image=nullptr;else gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);}
    void color4fv(const F32* value){if(native){color.setVec(value);LLRender2D::setNativeColor(color);}else gGL.color4fv(value);}
    void color4f(F32 r,F32 g,F32 b,F32 a){const F32 value[4]={r,g,b,a};color4fv(value);}
    void pushMatrix()
    {if(!native){gGL.pushMatrix();return;}if(depth==stack.size())throw std::logic_error("Native snapshot transform overflow");stack[depth++]=transform;}
    void popMatrix()
    {if(!native){gGL.popMatrix();return;}if(!depth)throw std::logic_error("Unbalanced native snapshot transform");transform=stack[--depth];}
    void translatef(F32 x,F32 y,F32 z)
    {
        if(!native){gGL.translatef(x,y,z);return;}
        if(z!=0)throw std::logic_error("Snapshot preview must remain in its UI plane");
        transform[4]+=transform[0]*x+transform[2]*y;transform[5]+=transform[1]*x+transform[3]*y;
    }
    void rotatef(F32 degrees,F32 x,F32 y,F32 z)
    {
        if(!native){gGL.rotatef(degrees,x,y,z);return;}
        if(x!=0||y!=0||z!=1)throw std::logic_error("Snapshot preview requires a planar rotation");
        const F32 c=std::cos(degrees*DEG_TO_RAD),s=std::sin(degrees*DEG_TO_RAD);const auto previous=transform;
        transform[0]=previous[0]*c+previous[2]*s;transform[1]=previous[1]*c+previous[3]*s;
        transform[2]=previous[2]*c-previous[0]*s;transform[3]=previous[3]*c-previous[1]*s;
    }
    void begin(U32 mode)
    {if(!native){gGL.begin(mode);return;}if(mode!=LLRender::TRIANGLES||count)throw std::logic_error("Invalid native snapshot primitive");}
    void texCoord2f(F32 x,F32 y){if(native){u=x;v=y;}else gGL.texCoord2f(x,y);}
    void vertex2i(S32 x,S32 y)
    {
        if(!native){gGL.vertex2i(x,y);return;}
        vertices[count++]={transform[0]*x+transform[2]*y+transform[4],transform[1]*x+transform[3]*y+transform[5],u,v,color.mV[0],color.mV[1],color.mV[2],color.mV[3]};
        if(count!=6)return;
        if(image)image->drawNativeTriangles(vertices);
        else for(unsigned first=0;first<6;first+=3)
        {
            LLRender2D::native_triangle_t triangle;
            for(unsigned j=0;j<3;++j){const auto& value=vertices[first+j];triangle[j]={value[0],value[1],value[4],value[5],value[6],value[7]};}
            LLRender2D::nativeTriangle(triangle);
        }
        count=0;
    }
    void end(){if(!native)gGL.end();else if(count)throw std::logic_error("Incomplete native snapshot triangles");}
};
