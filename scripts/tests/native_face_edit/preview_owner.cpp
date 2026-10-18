// Actual temporary BP owner; controlled current/account/region/resource services.
#include <algorithm>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <vector>
using U8=unsigned char;using U32=unsigned;using U64=unsigned long long;
struct LLUUID{int id=0;LLUUID()=default;LLUUID(int v):id(v){}bool notNull()const{return id;}bool operator==(const LLUUID&)const=default;static const LLUUID null;};const LLUUID LLUUID::null;
using uuid_vec_t=std::vector<LLUUID>;
template<class T>struct LLPointer{T*p=nullptr;LLPointer()=default;LLPointer(T*v):p(v){}T*get()const{return p;}T*operator->()const{return p;}operator T*()const{return p;}LLPointer&operator=(T*v){p=v;return *this;}};
struct LLGLTFMaterial{int value=4;};
struct LLTextureEntry{LLPointer<LLGLTFMaterial>base,override;LLGLTFMaterial*getGLTFMaterial()const{return base;}LLGLTFMaterial*getGLTFMaterialOverride()const{return override;}void setGLTFMaterial(LLGLTFMaterial*v){base=v;}void setGLTFMaterialOverride(LLGLTFMaterial*v){override=v;}void setGLTFRenderMaterial(std::nullptr_t){}};
constexpr int LL_PCODE_VOLUME=9;
struct VSResourceBudget{struct Reservation{static inline int live=0;Reservation(){++live;}~Reservation(){--live;}};};
struct VSWorldScene
{
    struct ObjectHandle{int host=1,local=1,id=5,epoch=1;};
    struct Impl
    {
        struct Object
        {
            PREVIEW
            int id=5,pcode=LL_PCODE_VOLUME;U64 revision=1;bool dirty=false;
            struct Motion{bool selected=true;void setSelectionState(bool v){selected=v;}}motion;
            struct Params{uuid_vec_t ids{{14},{21}};LLUUID getMaterial(U8 i)const{return ids.at(i);}void setMaterial(U8 i,LLUUID id){ids.at(i)=id;}}renderMaterials;
            struct Face{LLTextureEntry material;};std::vector<Face>faces=std::vector<Face>(2);
            std::shared_ptr<LegacyMaterialPreview>legacyPreview;
        };
        int host=1,epoch=1;bool retired=false;U64 nextRevision=2;std::map<int,Object>objects{{1,Object{}}};
    };
    std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();std::map<int,std::shared_ptr<VSWorldScene>>mNeighbours;
    int reservations=0;bool budget=true;std::function<void()>reserveHook;
    bool current(const ObjectHandle&t)const
    {if(t.host!=mImpl->host)return mNeighbours.count(t.host)&&mNeighbours.at(t.host)->current(t);const auto found=mImpl->objects.find(t.local);return !mImpl->retired&&mImpl->epoch==t.epoch&&found!=mImpl->objects.end()&&found->second.id==t.id;}
    std::shared_ptr<const VSResourceBudget::Reservation>reserveSpatialBytes(std::size_t)
    {++reservations;if(reserveHook)reserveHook();return budget?std::make_shared<VSResourceBudget::Reservation>():nullptr;}
    bool setLegacyMaterialPreview(const ObjectHandle&,bool);
    bool legacyMaterialPreview(const ObjectHandle&)const;
    LLUUID renderMaterialID(const ObjectHandle&,U32)const;
    bool setSelectionState(const ObjectHandle&,bool);
};
METHODS
int main()
{
    VSWorldScene scene;VSWorldScene::ObjectHandle token;auto& object=scene.mImpl->objects.at(1);
    object.faces[0].material.base=new LLGLTFMaterial{15};object.faces[0].material.override=new LLGLTFMaterial{31};
    scene.budget=false;assert(!scene.setLegacyMaterialPreview(token,true));assert(!scene.legacyMaterialPreview(token)&&scene.renderMaterialID(token,0)==LLUUID{14}&&VSResourceBudget::Reservation::live==0);
    scene.budget=true;assert(scene.setLegacyMaterialPreview(token,true));assert(scene.legacyMaterialPreview(token)&&scene.renderMaterialID(token,0)==LLUUID::null&&VSResourceBudget::Reservation::live==1);
    assert(object.renderMaterials.getMaterial(0)==LLUUID{14});const auto saved=object.legacyPreview;assert(saved->bases[0].get()==object.faces[0].material.base.get());assert(saved->overrides[0].get()!=object.faces[0].material.override.get());
    const auto before=scene.reservations;assert(scene.setLegacyMaterialPreview(token,true)&&scene.reservations==before);
    object.faces[0].material.override->value=99;object.renderMaterials.setMaterial(0,{44});assert(scene.setSelectionState(token,false));assert(!object.motion.selected&&!scene.legacyMaterialPreview(token)&&scene.renderMaterialID(token,0)==LLUUID{14}&&object.faces[0].material.override->value==31);assert(scene.reservations==before);
    // A new cache is independent of the old snapshot still held by this fixture.
    assert(scene.setLegacyMaterialPreview(token,true));assert(object.legacyPreview!=saved);assert(scene.setLegacyMaterialPreview(token,false));
    scene.reserveHook=[&]{++object.revision;};assert(!scene.setLegacyMaterialPreview(token,true));assert(!object.legacyPreview);scene.reserveHook={};
    scene.reserveHook=[&]{++scene.mImpl->epoch;};assert(!scene.setLegacyMaterialPreview(token,true));assert(!object.legacyPreview);scene.reserveHook={};token.epoch=scene.mImpl->epoch;
    assert(scene.setLegacyMaterialPreview(token,true));token.id=8;assert(!scene.setLegacyMaterialPreview(token,false));assert(object.legacyPreview);token.id=5;assert(scene.setLegacyMaterialPreview(token,false));
    auto neighbor=std::make_shared<VSWorldScene>();neighbor->mImpl->host=2;scene.mNeighbours[2]=neighbor;auto other=token;other.host=2;other.epoch=1;assert(scene.setLegacyMaterialPreview(other,true));assert(!scene.legacyMaterialPreview(token)&&scene.legacyMaterialPreview(other));assert(scene.setSelectionState(other,false));assert(!scene.legacyMaterialPreview(other));
    object.pcode=47;assert(scene.setLegacyMaterialPreview(token,true));assert(!object.legacyPreview);object.pcode=LL_PCODE_VOLUME;
    // Retiring/replacing a source releases its cache without touching another ID.
    assert(scene.setLegacyMaterialPreview(token,true));std::weak_ptr<VSWorldScene::Impl::Object::LegacyMaterialPreview>weak=object.legacyPreview;scene.mImpl->objects.clear();assert(weak.expired());assert(!scene.setLegacyMaterialPreview(token,false));
}
