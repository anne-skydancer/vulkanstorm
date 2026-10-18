#include <cassert>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
struct LLUUID{int value=0;bool isNull()const{return !value;}void setNull(){value=0;}bool operator<(LLUUID o)const{return value<o.value;}bool operator==(LLUUID o)const{return value==o.value;}bool operator!=(LLUUID o)const{return !(*this==o);}static const LLUUID null;};
const LLUUID LLUUID::null{};
struct LLVector3{double x=0;bool isFinite()const{return std::isfinite(x);}static const LLVector3 zero;};const LLVector3 LLVector3::zero{};
using LLVector3d=LLVector3;
struct VSCameraFocusObject{LLVector3 position;std::function<bool()> valid;bool current()const{return valid&&valid();}};
struct Agent{LLVector3 getPosGlobalFromAgent(LLVector3 p){return p;}}gAgent;
constexpr int LL_PCODE_LEGACY_AVATAR=47;
struct Scene
{
 struct ObjectHandle{LLUUID id;int generation=0;};
 struct Row{int generation=1,pcode=9,parent=0;bool attachment=false;double position=0;};
 struct Admission{int pcode;bool attachment;};
 std::map<int,Row> rows;int queries=0;
 std::optional<ObjectHandle> object(LLUUID id){const auto i=rows.find(id.value);return i==rows.end()?std::nullopt:std::optional(ObjectHandle{id,i->second.generation});}
 bool current(ObjectHandle h){const auto i=rows.find(h.id.value);return i!=rows.end()&&i->second.generation==h.generation;}
 std::optional<Admission> objectAdmission(ObjectHandle h){if(!current(h))return{};const auto& r=rows.at(h.id.value);return Admission{r.pcode,r.attachment};}
 std::optional<ObjectHandle> parentObject(ObjectHandle h){return current(h)?object({rows.at(h.id.value).parent}):std::nullopt;}
 std::optional<VSCameraFocusObject> focusObject(ObjectHandle h,LLVector3){++queries;if(!current(h))return{};return VSCameraFocusObject{{rows.at(h.id.value).position},[this,h]{return current(h);}};}
};
struct Context{Scene state;Scene& world(){return state;}};
struct Window{Context context;Context* nativeContext(){return &context;}}window,*gViewerWindow=&window;
struct VSNativeSession:std::enable_shared_from_this<VSNativeSession>
{
 inline static std::shared_ptr<VSNativeSession> selected;
 int generation=1;bool live=true;
 static std::shared_ptr<VSNativeSession>active(){return selected;}
 std::function<bool()> accountGuard(){const auto weak=weak_from_this();const int epoch=generation;return[weak,epoch]{const auto p=weak.lock();return p&&p->live&&p->generation==epoch;};}
};
struct LLAgentCamera
{
 struct NativeFocusState;std::shared_ptr<NativeFocusState> mNativeFocus;
 LLVector3d mCameraSmoothingLastPositionGlobal,mFocusGlobal;LLVector3 mCameraUpVector;
 void ensureNativeFocusState();void bindNativeFocusObject(const LLUUID&);const VSCameraFocusObject* nativeFocusObject()const;bool nativeCameraPose(LLVector3d&,LLVector3d&,LLVector3&)const;
};
STATE
METHODS
int main()
{
 auto first=std::make_shared<VSNativeSession>();VSNativeSession::selected=first;
 auto& scene=window.context.state;
 // Attachment child -> root attachment -> avatar -> seat. Focus must stop at
 // the genuine avatar; rootObject(false) incorrectly reaches the seat.
 scene.rows[1]={1,9,2,true,10};scene.rows[2]={1,9,3,true,11};scene.rows[3]={1,LL_PCODE_LEGACY_AVATAR,4,false,12};scene.rows[4]={1,9,0,false,13};
 LLAgentCamera camera;camera.bindNativeFocusObject({1});assert(camera.mNativeFocus->id==LLUUID{3});assert(scene.queries==1);
 for(int n=0;n<100;++n)assert(camera.nativeFocusObject()->position.x==12);
 assert(scene.queries==1); // Getters never query/step a second competing pose.
 scene.rows[3].position=20;camera.mNativeFocus->object=camera.mNativeFocus->query();assert(scene.queries==2&&camera.nativeFocusObject()->position.x==20);
 ++scene.rows[3].generation;assert(!camera.nativeFocusObject());assert(!camera.mNativeFocus->query()); // Exact identity, no UUID replacement binding.
 camera.bindNativeFocusObject({1});assert(camera.nativeFocusObject());
 auto old=camera.mNativeFocus;auto second=std::make_shared<VSNativeSession>();VSNativeSession::selected=second;
 assert(!camera.nativeFocusObject()&&!old->query()); // Even a still-live old owner may not become the new account's camera.
 camera.ensureNativeFocusState();assert(camera.mNativeFocus!=old);
 camera.bindNativeFocusObject({4});assert(camera.mNativeFocus->id==LLUUID{4});
 camera.mNativeFocus->published=true;LLVector3d eye,focus;LLVector3 up;
 assert(camera.nativeCameraPose(eye,focus,up));++second->generation;assert(!camera.nativeCameraPose(eye,focus,up)&&!camera.nativeFocusObject());
 camera.ensureNativeFocusState();scene.rows[1].parent=2;scene.rows[2].parent=1;camera.bindNativeFocusObject({1});assert(!camera.nativeFocusObject()&&!camera.mNativeFocus->query);
 scene.rows[1].parent=99;camera.bindNativeFocusObject({1});assert(!camera.nativeFocusObject());
 camera.bindNativeFocusObject({});assert(!camera.nativeFocusObject());
}
