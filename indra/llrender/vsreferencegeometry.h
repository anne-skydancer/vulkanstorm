// Real OpenGL peer for shared immediate geometry controllers. LGPL-2.1.
#pragma once
#include "llrender.h"
#include "llglstates.h"
#include "llrendersphere.h"
#include "llglslshader.h"
#include <stdexcept>
#include "vsrenderprimitivegeometry.h"
#include <functional>
class VSReferenceGeometry
{
    std::function<void()>mWhite;LLGLSLShader*mDebug=nullptr,*mVertex=nullptr;
public:
    explicit VSReferenceGeometry(std::function<void()>white={}):mWhite(std::move(white)){}
    VSReferenceGeometry(LLGLSLShader*debug,LLGLSLShader*vertex,std::function<void()>white={}):mWhite(std::move(white)),mDebug(debug),mVertex(vertex){}
    void debugShader(){if(!mDebug)throw std::logic_error("Reference debug shader owner is missing");mDebug->bind();}
    void vertexShader(){if(!mVertex)throw std::logic_error("Reference vertex-color shader owner is missing");mVertex->bind();}
    void begin(unsigned mode){gGL.begin(LLRender::eGeomModes(mode));}
    void end(){gGL.end();}void flush(){gGL.flush();}
    void vertex3f(float x,float y,float z){gGL.vertex3f(x,y,z);}void vertex3fv(const float*p){gGL.vertex3fv(p);}
    void texCoord2f(float u,float v){gGL.texCoord2f(u,v);}void texCoord2fv(const float*p){gGL.texCoord2fv(p);}
    void color4f(float r,float g,float b,float a){gGL.color4f(r,g,b,a);}void color4fv(const float*p){gGL.color4fv(p);}
    void diffuseColor4fv(const float*p){gGL.diffuseColor4fv(p);}
    void diffuseColor4f(float r,float g,float b,float a){gGL.diffuseColor4f(r,g,b,a);}
    void diffuseColor3f(float r,float g,float b){gGL.diffuseColor3f(r,g,b);}
    void matrixMode(unsigned mode){gGL.matrixMode(LLRender::eMatrixMode(mode));}void pushMatrix(){gGL.pushMatrix();}void popMatrix(){gGL.popMatrix();}
    void loadIdentity(){gGL.loadIdentity();}void loadMatrix(const float*p){gGL.loadMatrix(p);}void multMatrix(const float*p){gGL.multMatrix(p);}
    void translatef(float x,float y,float z){gGL.translatef(x,y,z);}void scalef(float x,float y,float z){gGL.scalef(x,y,z);}
    void rotatef(float angle,float x,float y,float z){gGL.rotatef(angle,x,y,z);}
    void setLineWidth(float width){gGL.setLineWidth(width);}void setPointSize(float pixels){gGL.flush();glPointSize(pixels);}
    void cullFace(unsigned face){gGL.flush();glCullFace(face);}
    LLGLDepthTest depth(bool test,bool write,unsigned compare=GL_LEQUAL){return LLGLDepthTest(test,write,compare);}
    LLGLState blend(bool enabled){return LLGLState(GL_BLEND,enabled);}
    LLGLState cull(bool enabled){return LLGLState(GL_CULL_FACE,enabled);}
    LLGLState clipEnabled(unsigned plane,bool enabled=true){return LLGLState(GL_CLIP_PLANE0+plane,enabled);}
    void white(){if(mWhite)mWhite();else gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);}
    void sphere(){gSphere.render();}void cone(int sides=12){vs_render_cone_geometry(gGL,sides);}
};
