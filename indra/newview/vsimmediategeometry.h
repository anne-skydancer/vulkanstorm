// CPU geometry/state peer for original immediate viewer drawing. LGPL-2.1.
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include "vsrenderprimitivegeometry.h"

class VSImmediateGeometry
{
public:
    // Values match LLRender::eGeomModes, checked by the actual viewer adapter.
    enum Primitive:unsigned{Triangles,TriangleStrip,TriangleFan,Points,Lines,LineStrip,LineLoop};
    struct Vertex{glm::vec3 position{},normal{0,0,1};glm::vec2 uv{};glm::vec4 color{1};};
    struct State
    {
        bool depthTest=true,depthWrite=true,blend=false,cull=true,cullFront=false,hud=false,debugShader=false;
        glm::vec4 uniformColor{1};
        unsigned depthCompare=0x0203; // GL_LEQUAL
        unsigned sourceBlend=0x0302,destinationBlend=0x0303; // original alpha blend
        float linePixels=1,pointPixels=1;
        std::array<std::optional<glm::vec4>,6> planes;
        std::shared_ptr<const void> image;
        std::optional<glm::mat4> projection;
    };
    struct Batch
    {
        Primitive primitive=Triangles;State state;
        std::vector<Vertex> vertices;
        std::shared_ptr<const void> residency;
    };
    using Reserve=std::function<std::shared_ptr<const void>(std::size_t)>;
    using Submit=std::function<bool(Batch&&)>;
    VSImmediateGeometry(Reserve reserve,Submit submit,const glm::mat4& sourceModelview=glm::mat4(1),
                        std::optional<glm::mat4> sourceProjection={}):
        mBaseModelview(sourceModelview),mInverseBaseModelview(glm::inverse(sourceModelview)),
        mProjectionKnown(bool(sourceProjection)),mReserve(std::move(reserve)),mSubmit(std::move(submit))
    {
        mBaseResidency=mReserve(sizeof(*this)+2*sizeof(glm::mat4));mGood=bool(mBaseResidency);
        if(sourceProjection){mMatrices[1].back()=*sourceProjection;mState.projection=*sourceProjection*mBaseModelview;}
        if(glm::determinant(sourceModelview)==0)mGood=false;
        for(unsigned i=0;i<4;++i)
            if(!finite(mBaseModelview[i]) || !finite(mInverseBaseModelview[i]) ||
               (sourceProjection && !finite((*sourceProjection)[i])))mGood=false;
    }
    bool good()const{return mGood;}
    void sphere(){vs_render_sphere_geometry(*this);flush();}
    void cone(int sides=12){vs_render_cone_geometry(*this,sides);}
    void white(){image({});}
    void hud(bool value){flush();mState.hud=value;}
    void begin(unsigned mode)
    {
        if(mode>LineLoop){mGood=false;return;}
        if(mBegun && mode!=mMode)
        {
            if(mMode==Triangles || mMode==Lines || mMode==Points)flush();
            else if(!mVertices.empty()){mGood=false;return;}
        }
        mMode=Primitive(mode);mBegun=true;
    }
    void end()
    {
        // Preserve LLRender's batching: simple primitives continue across
        // begin/end until a real state/matrix/flush boundary.
        if(mMode!=Triangles && mMode!=Lines && mMode!=Points)flush();
        else if(mVertices.size()>2048)flush();
    }
    void flush()
    {
        if(!mBegun)return;
        if(!mVertices.empty() && mGood)
        {
            Batch batch{mMode,mState,std::move(mVertices),std::move(mResidency)};
            if(!mSubmit(std::move(batch)))mGood=false;
        }
        std::vector<Vertex>().swap(mVertices);mResidency.reset();
    }
    void vertex3f(float x,float y,float z)
    {
        if(!mBegun || !mGood)return;
        if(!ensureCapacity())return;
        const auto point=mMatrices[0].back()*glm::vec4(x,y,z,1);
        const auto normal=mNormalMatrix*mNormal;
        if(!finite(point) || !finite(glm::vec4(normal,0))){mGood=false;return;}
        mVertices.push_back({glm::vec3(point),normal,mUV,mColor});
    }
    void vertex3fv(const float* p){vertex3f(p[0],p[1],p[2]);}
    void normal3f(float x,float y,float z){mNormal={x,y,z};}
    void normal3fv(const float*p){normal3f(p[0],p[1],p[2]);}
    void texCoord2f(float u,float v){mUV={u,v};}
    void texCoord2fv(const float*p){texCoord2f(p[0],p[1]);}
    void color4f(float r,float g,float b,float a)
    {
        const glm::vec4 value(r,g,b,a);if(!finite(value)){mGood=false;return;}
        // Exact LLRender::color4f -> color4ub truncation, not rounding.
        for(unsigned i=0;i<4;++i)mColor[i]=float(static_cast<unsigned char>(std::clamp(value[i],0.f,1.f)*255))/255.f;
    }
    void color4fv(const float*p){color4f(p[0],p[1],p[2],p[3]);}
    void diffuseColor4fv(const float*p)
    {
        flush();mState.uniformColor=glm::make_vec4(p);if(!finite(mState.uniformColor))mGood=false;
    }
    void diffuseColor4f(float r,float g,float b,float a){const float value[4]{r,g,b,a};diffuseColor4fv(value);}
    void debugShader(){flush();mState.debugShader=true;}
    void vertexShader(){flush();mState.debugShader=false;}
    void color3f(float r,float g,float b){color4f(r,g,b,1);}
    void diffuseColor3f(float r,float g,float b){diffuseColor4f(r,g,b,1);}
    // Matrix mode zero is MODELVIEW; one is PROJECTION. Texture transforms
    // stay a separate explicit texture owner rather than changing geometry.
    void matrixMode(unsigned mode){if(mode>1){mGood=false;return;}mMatrixMode=mode;}
    void pushMatrix()
    {
        flush();
        auto charge=mReserve(sizeof(glm::mat4));if(!charge){mGood=false;return;}
        auto& stack=mMatrices[mMatrixMode];
        try
        {
            mMatrixResidency[mMatrixMode].push_back(std::move(charge));
            try{stack.push_back(stack.back());}catch(...){mMatrixResidency[mMatrixMode].pop_back();throw;}
        }
        catch(const std::bad_alloc&){mGood=false;}
    }
    void popMatrix()
    {
        flush();
        if(mMatrices[mMatrixMode].size()==1){mGood=false;return;}
        mMatrices[mMatrixMode].pop_back();mMatrixResidency[mMatrixMode].pop_back();updateMatrices();
    }
    void loadIdentity(){setMatrix(mMatrixMode==0?mInverseBaseModelview:glm::mat4(1));}
    void loadMatrix(const float*p){setMatrix(mMatrixMode==0?mInverseBaseModelview*glm::make_mat4(p):glm::make_mat4(p));}
    void multMatrix(const float*p){setMatrix(mMatrices[mMatrixMode].back()*glm::make_mat4(p));}
    void translatef(float x,float y,float z){setMatrix(glm::translate(mMatrices[mMatrixMode].back(),glm::vec3(x,y,z)));}
    void scalef(float x,float y,float z){setMatrix(glm::scale(mMatrices[mMatrixMode].back(),glm::vec3(x,y,z)));}
    void rotatef(float angle,float x,float y,float z)
    {
        const glm::vec3 axis(x,y,z);if(glm::dot(axis,axis)==0){mGood=false;return;}
        setMatrix(glm::rotate(mMatrices[mMatrixMode].back(),glm::radians(angle),axis));
    }
    void setLineWidth(float width){flush();mState.linePixels=width;if(!(width>0)||!std::isfinite(width))mGood=false;}
    void setPointSize(float size){flush();mState.pointPixels=size;if(!(size>0)||!std::isfinite(size))mGood=false;}
    void cullFace(unsigned face){flush();mState.cullFront=face==0x0404;if(face!=0x0404 && face!=0x0405)mGood=false;}
    void blendFunc(unsigned source,unsigned destination){flush();mState.sourceBlend=source;mState.destinationBlend=destination;}
    void image(std::shared_ptr<const void> value){flush();mState.image=std::move(value);}
    class Scope
    {
        VSImmediateGeometry* mOwner;State mPrevious;unsigned mMask;unsigned mPlane;
    public:
        explicit Scope(VSImmediateGeometry& owner,unsigned mask,unsigned plane=0):mOwner(&owner),mPrevious(owner.mState),mMask(mask),mPlane(plane){owner.flush();}
        Scope(const Scope&)=delete;
        Scope(Scope&& other)noexcept:mOwner(std::exchange(other.mOwner,nullptr)),mPrevious(std::move(other.mPrevious)),mMask(other.mMask),mPlane(other.mPlane){}
        ~Scope()
        {
            if(!mOwner)return;
            mOwner->flush();auto& state=mOwner->mState;
            // Match each original GL scope's own fields. A cull scope must
            // not undo a line width, blend function, image or color change.
            if(mMask&1){state.depthTest=mPrevious.depthTest;state.depthWrite=mPrevious.depthWrite;state.depthCompare=mPrevious.depthCompare;}
            if(mMask&2)state.blend=mPrevious.blend;
            if(mMask&4)state.cull=mPrevious.cull;
            if(mMask&8)state.planes[mPlane]=mPrevious.planes[mPlane];
        }
    };
    Scope depth(bool test,bool write,unsigned compare=0x0203)
    {Scope scope(*this,1);mState.depthTest=test;mState.depthWrite=write;mState.depthCompare=compare;return scope;}
    Scope blend(bool enabled){Scope scope(*this,2);mState.blend=enabled;return scope;}
    Scope cull(bool enabled){Scope scope(*this,4);mState.cull=enabled;return scope;}
    Scope clipEnabled(unsigned index,bool enabled=true)
    {
        Scope scope(*this,8,std::min(index,5u));if(index>=mState.planes.size()){mGood=false;return scope;}
        if(enabled){if(!mState.planes[index])mState.planes[index]=glm::vec4(0);}
        else mState.planes[index].reset();
        return scope;
    }
    Scope clipPlane(unsigned index,const float* equation)
    {
        Scope scope(*this,8,std::min(index,5u));
        if(index>=mState.planes.size()){mGood=false;return scope;}
        const auto& matrix=mMatrices[0].back();
        if(glm::determinant(matrix)==0){mGood=false;return scope;}
        mState.planes[index]=glm::transpose(glm::inverse(matrix))*glm::make_vec4(equation);
        return scope;
    }
private:
    bool mGood=true,mBegun=false;Primitive mMode=Triangles;unsigned mMatrixMode=0;
    glm::mat4 mBaseModelview{1},mInverseBaseModelview{1};bool mProjectionKnown=false;
    State mState;glm::mat3 mNormalMatrix{1};std::shared_ptr<const void>mBaseResidency;glm::vec3 mNormal{0,0,1};glm::vec2 mUV{};glm::vec4 mColor{1};
    std::array<std::vector<glm::mat4>,2> mMatrices{{{glm::mat4(1)},{glm::mat4(1)}}};
    std::array<std::vector<std::shared_ptr<const void>>,2> mMatrixResidency;
    std::vector<Vertex> mVertices;std::shared_ptr<const void>mResidency;Reserve mReserve;Submit mSubmit;
    static bool finite(const glm::vec4& value)
    {for(unsigned i=0;i<4;++i)if(!std::isfinite(value[i]))return false;return true;}
    bool ensureCapacity()
    {
        if(mVertices.size()<mVertices.capacity())return true;
        if(mVertices.capacity()>mVertices.max_size()/2){mGood=false;return false;}
        const auto capacity=std::max(std::size_t(8),mVertices.capacity()*2);
        if(capacity>mVertices.max_size()){mGood=false;return false;}
        auto charge=mReserve(capacity*sizeof(Vertex));if(!charge){mGood=false;return false;}
        try{std::vector<Vertex> next;next.reserve(capacity);next.insert(next.end(),mVertices.begin(),mVertices.end());mVertices=std::move(next);mResidency=std::move(charge);}
        catch(const std::bad_alloc&){mGood=false;return false;}
        return true;
    }
    void setMatrix(const glm::mat4& value)
    {
        flush();
        for(unsigned i=0;i<4;++i)if(!finite(value[i])){mGood=false;return;}
        mMatrices[mMatrixMode].back()=value;updateMatrices();
    }
    void updateMatrices()
    {
        if(mMatrixMode==1)mProjectionKnown=true;
        if(mProjectionKnown)mState.projection=mMatrices[1].back()*mBaseModelview;
        if(mMatrixMode!=1)
        {
            const auto matrix=glm::mat3(mMatrices[0].back());
            mNormalMatrix=glm::determinant(matrix)!=0?glm::transpose(glm::inverse(matrix)):glm::mat3(1);
        }
    }
};
