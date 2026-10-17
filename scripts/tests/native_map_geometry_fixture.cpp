#include <cassert>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>
using S32=int;using U32=unsigned;using F32=float;using F64=double;
#define VS_NATIVE_VULKAN 1
#define DEBUG_DRAW_TILE 0
#define llassert assert
int lltrunc(double v){return int(v);}int ll_round(double v){return int(std::lround(v));}
constexpr int VX=0,VY=1,VZ=2;constexpr float REGION_WIDTH_METERS=256;
struct LLVector3d {double v[3];LLVector3d(double x=0,double y=0,double z=0):v{x,y,z}{}double& operator[](int i){return v[i];}double operator[](int i)const{return v[i];}};
struct LLVector3 {float mV[3];LLVector3(float x=0,float y=0,float z=0):mV{x,y,z}{}float& operator[](int i){return mV[i];}float operator[](int i)const{return mV[i];}};
struct Rect {int x,y,w,h;};std::vector<Rect> drawn;
struct LLUIImage {void draw(int x,int y,int w,int h){drawn.push_back({x,y,w,h});}};
template<class T>using LLPointer=std::shared_ptr<T>;
struct VSMapImage{LLPointer<LLUIImage> image;bool missing=false;};
struct LLWorldMipmap {static constexpr int MAP_LEVELS=8,MAP_TILE_SIZE=256;static void globalToMipmap(F64,F64,S32,U32*,U32*);};
MIPMAP_GRID_METHOD
int gl_calls=0;struct LLViewerFetchedTexture {bool hasGLTexture(){return false;}void setAddressMode(int){++gl_calls;}};
struct LLTexUnit{enum{TAM_CLAMP};void bind(LLViewerFetchedTexture*){++gl_calls;}};
struct LLGLSUIDefault {LLGLSUIDefault(){++gl_calls;}};
struct LLRender{enum{TRIANGLES};};
struct GL{LLTexUnit unit;LLTexUnit* getTexUnit(int){++gl_calls;return &unit;}void color4f(float,float,float,float){++gl_calls;}void begin(int){++gl_calls;}void texCoord2f(float,float){++gl_calls;}void vertex3f(float,float,float){++gl_calls;}void end(){++gl_calls;}}gGL;
struct LLRender2D{static bool isNativeUI(){return true;}};
struct LLWorldMap {
    enum class State {Loading,Missing,Ready};State state=State::Ready;int requests=0;bool lastLoad=false;
    static LLWorldMap* getInstance(){static LLWorldMap value;return &value;}
    VSMapImage getNativeObjectsTile(U32,U32,int,bool load){++requests;lastLoad=load;return {state==State::Ready?std::make_shared<LLUIImage>():nullptr,state==State::Missing};}
    LLPointer<LLViewerFetchedTexture> getObjectsTile(U32,U32,int,bool){++gl_calls;return {};}
};
struct LLWorldMapView {
    double origin=0,metersPerPixel=1;
    LLVector3d viewPosToGlobal(int x,int y){return {origin+x*metersPerPixel,origin+y*metersPerPixel,0};}
    LLVector3 globalPosToView(LLVector3d p){return {float((p[0]-origin)/metersPerPixel),float((p[1]-origin)/metersPerPixel),0};}
    bool drawMipmapLevel(S32 width,S32 height,S32 level,bool load=true);
};
DRAW_LEVEL_METHOD
int main(){
    LLWorldMapView view;auto* world=LLWorldMap::getInstance();
    assert(view.drawMipmapLevel(256,256,1));assert(gl_calls==0&&drawn.size()==4);
    assert(drawn[0].x==0&&drawn[0].y==0&&drawn[0].w==256&&drawn[0].h==256);
    assert(drawn[1].x==256&&drawn[1].y==0&&drawn[2].x==0&&drawn[2].y==256);
    drawn.clear();view.metersPerPixel=2;assert(view.drawMipmapLevel(256,256,2));assert(drawn.size()==4&&drawn[0].w==256&&drawn[1].x==256);
    drawn.clear();view.origin=100;view.metersPerPixel=1;assert(view.drawMipmapLevel(256,256,1,false));assert(!world->lastLoad&&drawn[0].x==-100&&drawn[0].y==-100&&drawn[0].w==256);
    drawn.clear();view.origin=100;view.metersPerPixel=1.3;assert(view.drawMipmapLevel(256,256,1));assert(drawn[1].x==drawn[0].x+drawn[0].w);auto nextRow=std::find_if(drawn.begin(),drawn.end(),[&](const auto& r){return r.y>drawn[0].y;});assert(nextRow!=drawn.end()&&nextRow->y==drawn[0].y+drawn[0].h);
    drawn.clear();world->state=LLWorldMap::State::Loading;assert(!view.drawMipmapLevel(256,256,1)&&drawn.empty());
    world->state=LLWorldMap::State::Missing;assert(view.drawMipmapLevel(256,256,1)&&drawn.empty());assert(gl_calls==0);
}
