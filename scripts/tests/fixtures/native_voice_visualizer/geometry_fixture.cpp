#include <array>
#include <vector>
#include <map>
#include <string>
#include <cmath>
#include <cassert>
#include <random>
using F32=float;using F64=double;using S32=int;
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_UI
#define GL_TRUE true
#define GL_FALSE false
struct LLVector3{float mV[3]{};LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{};LLVector3 operator+(const LLVector3&b)const{return{mV[0]+b.mV[0],mV[1]+b.mV[1],mV[2]+b.mV[2]};}LLVector3 operator-(const LLVector3&b)const{return{mV[0]-b.mV[0],mV[1]-b.mV[1],mV[2]-b.mV[2]};}LLVector3 operator*(float b)const{return{mV[0]*b,mV[1]*b,mV[2]*b};}};
struct LLColor4{float mV[4];LLColor4(float r,float g,float b,float a):mV{r,g,b,a}{}};
struct LLRender{enum eGeomModes{TRIANGLE_STRIP};};
std::map<std::string,bool> settings;int gSavedSettings;
template<class T>struct LLCachedControl{std::string name;LLCachedControl(int&,const char*n,T=T{}):name(n){}operator T()const{return settings[name];}};
struct LLGLSPipelineAlpha{};struct LLGLDepthTest{LLGLDepthTest(bool,bool){}};
struct LLViewerCamera{static LLViewerCamera*getInstance(){static LLViewerCamera c;return&c;}LLVector3 left{1,0,0},up{0,0,1};LLVector3 getLeftAxis()const{return left;}LLVector3 getUpAxis()const{return up;}};
struct Agent{LLVector3 getPosAgentFromGlobal(const LLVector3&p){return p;}}gAgent;
struct Timer{double time=0;double getTotalSeconds()const{return time;}};
struct Recorder{
 struct Vertex{std::array<float,3>position;std::array<float,2>uv;std::array<float,4>color;unsigned image;bool operator==(const Vertex&)const=default;};
 std::vector<Vertex>vertices;unsigned image=0;std::array<float,4>color{};std::array<float,2>uv{};
 Recorder*getTexUnit(int){return this;}void bind(unsigned i){image=i;}void pipeline(){}void color4fv(const float*c){std::copy_n(c,4,color.begin());}void begin(LLRender::eGeomModes){}void end(){}void texCoord2i(int u,int v){uv={float(u),float(v)};}void vertex3fv(const float*p){vertices.push_back({{p[0],p[1],p[2]},uv,color,image});}
}gGL;
CONSTANTS
struct Owner{
 double mCurrentTime=0,mPreviousTime=0;Timer mTimer;bool mVoiceEnabled=true,mCurrentlySpeaking=false;float mSpeakingAmplitude=0;LLVector3 mPositionGlobal;
 struct Symbol{bool mActive=true;LLVector3 mPosition;float mWaveExpansion[NUM_VOICE_SYMBOL_WAVES];bool mWaveActive[NUM_VOICE_SYMBOL_WAVES];double mWaveFadeOutStartTime[NUM_VOICE_SYMBOL_WAVES];float mWaveOpacity[NUM_VOICE_SYMBOL_WAVES];unsigned mTexture[NUM_VOICE_SYMBOL_WAVES];}mSoundSymbol;
 Owner(){for(unsigned i=0;i<NUM_VOICE_SYMBOL_WAVES;++i){mSoundSymbol.mTexture[i]=i;mSoundSymbol.mWaveExpansion[i]=1;mSoundSymbol.mWaveActive[i]=false;mSoundSymbol.mWaveFadeOutStartTime[i]=0;mSoundSymbol.mWaveOpacity[i]=1;}}
 void frozen();template<class P>void renderSymbol(P&);
};
FROZEN
CURRENT
int main(){std::mt19937 rng(3019);Owner old,current;settings["VoiceVisualizerEnabled"]=true;settings["FSShowVoiceVisualizerWithDot"]=true;
 for(int frame=0;frame<20000;++frame){old.mTimer.time=current.mTimer.time=frame*.016+(rng()%100)*.0001;old.mSpeakingAmplitude=current.mSpeakingAmplitude=(rng()%1250)*.001f;old.mCurrentlySpeaking=current.mCurrentlySpeaking=(rng()%3)!=0;old.mVoiceEnabled=current.mVoiceEnabled=(rng()%11)!=0;old.mSoundSymbol.mActive=current.mSoundSymbol.mActive=(rng()%19)!=0;settings["FSShowVoiceVisualizerWithDot"]=(rng()%3)!=0;settings["VoiceVisualizerEnabled"]=(rng()%17)!=0;gGL.vertices.clear();old.frozen();Recorder painter;struct NativeBindings:Recorder{void bind(unsigned i){Recorder::bind(i?i-1:0);}}native;current.renderSymbol(native);assert(gGL.vertices==native.vertices);assert(old.mCurrentTime==current.mCurrentTime&&old.mPreviousTime==current.mPreviousTime);for(int i=0;i<NUM_VOICE_SYMBOL_WAVES;++i){assert(old.mSoundSymbol.mWaveExpansion[i]==current.mSoundSymbol.mWaveExpansion[i]);assert(old.mSoundSymbol.mWaveActive[i]==current.mSoundSymbol.mWaveActive[i]);assert(old.mSoundSymbol.mWaveOpacity[i]==current.mSoundSymbol.mWaveOpacity[i]);}}
}
