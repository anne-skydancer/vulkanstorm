// Backend-neutral sink for original LLTracker geometry. LGPL-2.1.
#pragma once
#include "vsworldrenderer.h"
#include "llrender.h"
#include "v3math.h"
#include <array>
#include <stdexcept>
class VSTrackerMeshSink
{
public:
    std::vector<VSWorldRenderer::Vertex> vertices;
    std::vector<unsigned> indices;
    explicit VSTrackerMeshSink(std::size_t maximum):mMaximum(maximum){vertices.reserve(maximum);indices.reserve(maximum);}
    void matrixMode(unsigned mode){if(mode!=LLRender::MM_MODELVIEW)throw std::logic_error("Unexpected tracker matrix mode");}
    void pushMatrix(){if(mPushed)throw std::logic_error("Unexpected tracker matrix nesting");mSaved=mTranslation;mPushed=true;}
    void popMatrix(){if(!mPushed)throw std::logic_error("Unbalanced tracker matrix stack");mTranslation=mSaved;mPushed=false;}
    void translatef(float x,float y,float z){mTranslation+=LLVector3(x,y,z);}
    void color4fv(const float* value)
    {
        // Exactly LLRender::color4f: clamp then truncate to U8, not round.
        for(unsigned c=0;c<4;++c)mColor[c]=float(static_cast<U8>(llclamp(value[c],0.f,1.f)*255.f))/255.f;
    }
    void begin(unsigned mode){if(mDrawing || (mode!=LLRender::TRIANGLES && mode!=LLRender::TRIANGLE_FAN))throw std::logic_error("Unexpected tracker primitive");mMode=mode;mCount=0;mDrawing=true;}
    void end(){if(!mDrawing || (mMode==LLRender::TRIANGLES && mCount%3))throw std::logic_error("Incomplete tracker primitive");mDrawing=false;}
    void vertex3f(float x,float y,float z)
    {
        if(!mDrawing)throw std::logic_error("Tracker vertex outside primitive");
        VSWorldRenderer::Vertex point{x+mTranslation.mV[0],y+mTranslation.mV[1],z+mTranslation.mV[2],0,0,1,0,0};
        point.r=mColor[0];point.g=mColor[1];point.b=mColor[2];point.a=mColor[3];
        if(mMode==LLRender::TRIANGLES)append(point);
        else if(mCount==0)mFirst=point;
        else if(mCount==1)mPrevious=point;
        else{append(mFirst);append(mPrevious);append(point);mPrevious=point;}
        ++mCount;
    }
private:
    void append(const VSWorldRenderer::Vertex& point){if(vertices.size()>=mMaximum)throw std::logic_error("Tracker geometry exceeds reserved input");indices.push_back(unsigned(vertices.size()));vertices.push_back(point);}
    std::size_t mMaximum;
    LLVector3 mTranslation,mSaved;
    bool mPushed=false,mDrawing=false;
    unsigned mMode=0,mCount=0;
    std::array<float,4> mColor{1,1,1,1};
    VSWorldRenderer::Vertex mFirst{},mPrevious{};
};
