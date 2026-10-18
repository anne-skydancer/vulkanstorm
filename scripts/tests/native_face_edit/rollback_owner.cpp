// Test the actual rollback owner methods, including reentry and identity changes.
#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
using S32=int;using U32=unsigned;using F64=double;using LLUUID=int;using uuid_vec_t=std::vector<int>;constexpr int PERM_COPY=1,PERM_TRANSFER=2;
template<class T>using LLPointer=std::shared_ptr<T>;
namespace VSResourceBudget{struct Reservation{};}
struct LLColor4{float r=0,g=0,b=0,a=1;LLColor4()=default;explicit LLColor4(float n):r(n),g(n),b(n){};static const LLColor4 white;};const LLColor4 LLColor4::white{1};
struct LLGLTFMaterial{int value=0;};struct Material{LLColor4 spec;LLColor4 getSpecularLightColor()const{return spec;}};
struct LLTextureEntry{LLColor4 color;std::shared_ptr<Material> material;std::shared_ptr<LLGLTFMaterial> gltf;LLColor4 getColor()const{return color;}std::shared_ptr<Material>getMaterialParams()const{return material;}LLGLTFMaterial*getGLTFMaterialOverride()const{return gltf.get();}};
struct LLAggregatePermissions{enum{AP_NONE,AP_EMPTY,AP_SOME,AP_ALL};int copy=AP_ALL,transfer=AP_ALL;int getValue(int v)const{return v==PERM_COPY?copy:transfer;}};
struct VSObjectProperties{LLAggregatePermissions ag_texture_perms_owner,ag_texture_perms;uuid_vec_t texture_ids;};
namespace VSObjectContext{bool ownedByAgent(unsigned flags){return flags&1;}}
namespace VSWorldScene{using ObjectHandle=int;}
struct World{struct Row{unsigned rootUpdateFlags=1;};bool budget=true,live=true;std::function<void()>reserveHook,copyHook;int reserves=0;std::vector<LLTextureEntry>entries;std::vector<int>ids;std::optional<Row>objectAdmission(int)const{return Row{};}S32 textureEntryCount(int)const{return S32(entries.size());}std::shared_ptr<const VSResourceBudget::Reservation>reserveSpatialBytes(std::size_t){++reserves;if(reserveHook)reserveHook();return budget?std::make_shared<VSResourceBudget::Reservation>():nullptr;}bool copyMaterialEntry(int,U32 s,LLTextureEntry&out){if(copyHook)copyHook();if(s>=entries.size())return false;out=entries[s];return true;}LLUUID renderMaterialID(int,U32 s){return ids.at(s);}};
struct Session{int generation=1;std::function<bool()>accountGuard(){int g=generation;return [this,g]{return g==generation;};}};
struct LLFrameTimer{static double getTotalSeconds(){return 123;}};
class VSObjectSelection{public:
 struct SavedMaterials{bool ready=false;uuid_vec_t textures,ids;std::vector<LLPointer<LLGLTFMaterial>>overrides;std::shared_ptr<const VSResourceBudget::Reservation>residency;};
 struct SavedColors{std::vector<LLColor4>colors,shiny;std::shared_ptr<const VSResourceBudget::Reservation>residency;};
 struct Publication{std::shared_ptr<const VSObjectProperties>properties;};
 struct Node{int object=0;bool pending=false;double retryAt=0;std::shared_ptr<SavedMaterials>materials=std::make_shared<SavedMaterials>();std::shared_ptr<SavedColors>colors;std::shared_ptr<Publication>publication;};
 struct Impl{std::weak_ptr<Session>session;bool retired=false;int revision=1;std::vector<Node>nodes;bool valid=true;int sends=0;std::function<void()>sendHook;bool current(int object)const{return valid&&!retired&&object==3;}void sendSelection(const char*name,const std::vector<int>&objects){assert(std::string(name)=="ObjectSelect"&&objects==std::vector<int>{3});++sends;if(sendHook)sendHook();}};
 std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();World*w=nullptr;World*scene(){return w&&w->live?w:nullptr;}std::function<bool()>selectionGuard(){int r=mImpl->revision;return [this,r]{return !mImpl->retired&&mImpl->revision==r;};}
 bool refreshSavedTextures();bool saveColors(const VSWorldScene::ObjectHandle&,bool);std::shared_ptr<const SavedColors>savedColors(const VSWorldScene::ObjectHandle&)const;bool captureSavedMaterials(const VSWorldScene::ObjectHandle&,const std::shared_ptr<const VSObjectProperties>&);
};
bool sameObject(int a,int b){return a==b;}
CURRENT
int main(){World w;w.entries.resize(2);w.ids={31,32};w.entries[0].color=LLColor4(.3f);w.entries[1].color=LLColor4(.7f);w.entries[0].material=std::make_shared<Material>();w.entries[0].material->spec=LLColor4(.4f);w.entries[0].gltf=std::make_shared<LLGLTFMaterial>();w.entries[0].gltf->value=41;
auto session=std::make_shared<Session>();VSObjectSelection s;s.w=&w;s.mImpl->session=session;s.mImpl->nodes.resize(1);auto&node=s.mImpl->nodes[0];node.object=3;auto props=std::make_shared<VSObjectProperties>();props->texture_ids={101,102};node.publication=std::make_shared<VSObjectSelection::Publication>();node.publication->properties=props;
assert(s.captureSavedMaterials(3,props));auto saved=node.materials;assert(saved->ready&&saved->textures==props->texture_ids&&saved->ids==w.ids&&saved->overrides[0]->value==41);w.entries[0].gltf->value=99;assert(saved->overrides[0]->value==41);assert(!s.captureSavedMaterials(4,props));assert(!s.captureSavedMaterials(3,std::make_shared<VSObjectProperties>(*props)));
props->ag_texture_perms_owner.copy=LLAggregatePermissions::AP_SOME;w.ids[0]=67;assert(!s.captureSavedMaterials(3,props)&&saved->ids[0]==31);props->ag_texture_perms_owner.copy=LLAggregatePermissions::AP_EMPTY;assert(s.captureSavedMaterials(3,props)&&saved->ids[0]==67);
w.budget=false;assert(!s.captureSavedMaterials(3,props)&&!saved->ready);w.budget=true;assert(s.captureSavedMaterials(3,props));w.reserveHook=[&]{++session->generation;};w.ids[0]=89;assert(!s.captureSavedMaterials(3,props)&&saved->ids[0]==67);w.reserveHook={};assert(s.captureSavedMaterials(3,props));
assert(s.saveColors(3,false));auto colors=s.savedColors(3);assert(colors&&colors->colors[0].r==.3f&&colors->shiny.empty());assert(s.saveColors(3,true));colors=s.savedColors(3);assert(colors->colors[0].r==.3f&&colors->shiny[0].r==.4f&&colors->shiny[1].r==1);w.budget=false;w.entries[0].color=LLColor4(.9f);assert(!s.saveColors(3,false)&&s.savedColors(3)==colors);w.budget=true;w.copyHook=[&]{s.mImpl->valid=false;};assert(!s.saveColors(3,false));w.copyHook={};s.mImpl->valid=true;assert(s.savedColors(4)==nullptr);
assert(s.refreshSavedTextures());assert(node.pending&&node.retryAt==128&&s.mImpl->sends==1&&node.materials==saved&&saved->ready);s.mImpl->sendHook=[&]{++s.mImpl->revision;};assert(!s.refreshSavedTextures());s.mImpl->retired=true;assert(!s.refreshSavedTextures()&&!s.savedColors(3));
}
