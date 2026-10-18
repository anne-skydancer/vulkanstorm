// Original scale/TE operation with genuine native source and task owners. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vstexturescaleselection.h"
#include "vstexturescaleratio.h"
#include "vsmaterialselection.h"
#include "llviewerwindow.h"
#include "vsvulkancontext.h"
#include "llfloatertools.h"
#include "llmaterialmgr.h"
#include "vsmaterialassets.h"

bool vs_capture_selected_texture_ratios(VSObjectSelection& selection,const VSWorldScene::ObjectHandle& token)
{
    const auto saved=selection.savedTransform(token);if(!saved)return false;
    saved->textureRatios.reset();
    const auto object=VSMaterialObject::find(token.id);
    if(!object || !selection.node(token) || !gViewerWindow || !gViewerWindow->nativeContext())return false;
    const auto count=object.getNumTEs();if(count<0 || count>45)return false;
    for(S32 side=0;side<count;++side)if(!object.getTE(side))return false;
    const auto scale=object.getScale();
    if(!scale.isFinite() || scale.mV[VX]==0 || scale.mV[VY]==0 || scale.mV[VZ]==0)return false;
    const std::size_t bytes=sizeof(VSTextureScaleRatios)+std::size_t(count)*(sizeof(LLVector3)+3*sizeof(std::vector<LLVector3>)+
        LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT*(sizeof(LLVector3)+2*sizeof(LLVector2)));
    auto& world=gViewerWindow->nativeContext()->world();
    auto residency=world.reserveSpatialBytes(bytes);if(!residency)return false;
    auto candidate=std::make_shared<VSTextureScaleRatios>();candidate->residency=std::move(residency);
    auto source=object;vs_capture_texture_scale_ratios(&source,*candidate);
    if(!world.current(token) || selection.savedTransform(token)!=saved || object.getNumTEs()!=count)return false;
    saved->textureRatios=std::move(candidate);return true;
}

bool vs_adjust_selected_textures_by_scale(VSObjectSelection& selection,bool sendToSimulator,bool stretch)
{
    if(!gFloaterTools)return false;
    bool complete=true;
    for(const auto& token:selection.selected())
    {
        const auto saved=selection.savedTransform(token);
        auto object=VSMaterialObject::find(token.id);
        if(!object || !object.permModify())continue;
        const auto ratios=saved?saved->textureRatios:nullptr;
        const auto count=object.getNumTEs();
        if(!ratios || count<0 || ratios->textureScaleRatios.size()!=std::size_t(count) ||
           ratios->gltfScaleRatios.size()!=std::size_t(count) || ratios->gltfScales.size()!=std::size_t(count) || ratios->gltfOffsets.size()!=std::size_t(count))
        {complete=false;continue;}
        vs_adjust_texture_scale(&object,*ratios,sendToSimulator,stretch,
            [](LLMaterialPtr value){return gFloaterTools->createDefaultMaterial(value);},
            [](VSMaterialObject* source,U8 side,const LLMaterial& value){LLMaterialMgr::instance().put(source->getID(),side,value);},
            [](VSMaterialObject* source,U8 side,const LLGLTFMaterial* value){source->queueModify(side,value);});
    }
    return complete;
}

#include "llgltfmateriallist.h"
#include "llsdutil.h"

struct VSPreparedTextureAdjustment
{
    struct Face
    {
        LLTextureEntry original,value;
        LLUUID materialID;
    };
    struct Event
    {
        enum Kind {Legacy,GLTF} kind;
        U8 side=0;
        LLMaterialPtr legacy;
        std::shared_ptr<VSMaterialAssets::PreparedPut> preparedLegacy;
        LLPointer<LLGLTFMaterial> gltf;
    };
    struct Row
    {
        VSWorldScene::ObjectHandle object;
        LLVector3 scale;
        std::vector<Face> faces;
        std::vector<Event> events;
        std::function<bool()> current;
        std::string capability;
        bool send=false;
        // This is a charged immutable operation snapshot, not a scene object.
        bool permModify()const{return true;}
        S32 getNumTEs()const{return S32(faces.size());}
        LLTextureEntry* getTE(U8 side){return side<faces.size()?&faces[side].value:nullptr;}
        LLVector3 getScale()const{return scale;}
        void setTEScale(U8 side,F32 s,F32 t){faces[side].value.setScale(s,t);}
        void sendTEUpdate(){send=true;}
    };
    std::vector<Row> rows;
    std::function<bool()> current;
    std::shared_ptr<const void> residency;
    bool published=false;
};

namespace
{
void detachTextureMaterials(LLTextureEntry& value)
{
    if(const auto* material=value.getGLTFMaterialOverride())
        value.setGLTFMaterialOverride(new LLGLTFMaterial(*material));
    // The kernel writes only the override and a newly composed render material;
    // the fetched base remains the genuine read-only asset reference.
    value.setGLTFRenderMaterial(nullptr);
}
bool sameTextureInputs(const LLTextureEntry& a,const LLTextureEntry& b)
{
    if(a!=b || a.getGLTFMaterial()!=b.getGLTFMaterial())return false;
    const auto* left=a.getGLTFMaterialOverride();const auto* right=b.getGLTFMaterialOverride();
    if(bool(left)!=bool(right) || (left && *left!=*right))return false;
    const auto& lm=a.getMaterialParams();const auto& rm=b.getMaterialParams();
    return bool(lm)==bool(rm) && (!lm || llsd_equals(lm->asLLSD(),rm->asLLSD()));
}
}
std::shared_ptr<VSPreparedTextureAdjustment> vs_prepare_selected_texture_adjustment(
    VSObjectSelection& selection,const std::map<LLUUID,LLVector3>& proposedScales,bool sendToSimulator,bool stretch)
{
    if(!gFloaterTools || !gViewerWindow || !gViewerWindow->nativeContext())return {};
    const auto current=selection.selectionGuard();if(!current())return {};
    auto& world=gViewerWindow->nativeContext()->world();
    const auto selected=selection.selected();
    std::size_t bytes=sizeof(VSPreparedTextureAdjustment)+selected.size()*sizeof(VSPreparedTextureAdjustment::Row);
    for(const auto& token:selected)
    {
        const auto object=VSMaterialObject::find(token.id);
        if(!object || !selection.node(token))return {};
        const auto count=object.getNumTEs();if(count<0 || count>45)return {};
        bytes+=std::size_t(count)*(sizeof(VSPreparedTextureAdjustment::Face)+2*sizeof(VSPreparedTextureAdjustment::Event)+
            3*sizeof(LLGLTFMaterial)+sizeof(LLFetchedGLTFMaterial)+sizeof(LLMaterial));
    }
    auto residency=world.reserveSpatialBytes(bytes);if(!residency)return {};
    auto result=std::make_shared<VSPreparedTextureAdjustment>();result->residency=std::move(residency);result->current=current;
    result->rows.reserve(selected.size());
    for(const auto& token:selected)
    {
        if(!current())return {};
        auto object=VSMaterialObject::find(token.id);if(!object || !selection.node(token))return {};
        if(!object.permModify())continue;
        const auto saved=selection.savedTransform(token);const auto ratios=saved?saved->textureRatios:nullptr;
        const auto count=object.getNumTEs();
        if(!ratios || count<0 || ratios->textureScaleRatios.size()!=std::size_t(count) ||
            ratios->gltfScaleRatios.size()!=std::size_t(count) || ratios->gltfScales.size()!=std::size_t(count) || ratios->gltfOffsets.size()!=std::size_t(count))return {};
        VSPreparedTextureAdjustment::Row row;row.object=token;row.current=object.task().objectGuard();
        row.scale=object.getScale();if(const auto scale=proposedScales.find(token.id);scale!=proposedScales.end())row.scale=scale->second;
        if(!row.scale.isFinite() || row.scale.mV[VX]==0 || row.scale.mV[VY]==0 || row.scale.mV[VZ]==0)return {};
        row.capability=object.task().getCapability("ModifyMaterialParams");
        row.faces.reserve(count);row.events.reserve(std::size_t(count)*2);
        for(S32 side=0;side<count;++side)
        {
            const auto* entry=object.getTE(side);if(!entry)return {};
            const auto channels=LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT;
            if(ratios->gltfScaleRatios[side].size()!=channels || ratios->gltfScales[side].size()!=channels || ratios->gltfOffsets[side].size()!=channels)return {};
            VSPreparedTextureAdjustment::Face face;face.original=*entry;face.value=*entry;
            face.materialID=object.getRenderMaterialID(side);
            detachTextureMaterials(face.original);detachTextureMaterials(face.value);row.faces.push_back(std::move(face));
        }
        vs_adjust_texture_scale(&row,*ratios,sendToSimulator,stretch,
            [](LLMaterialPtr value){return gFloaterTools->createDefaultMaterial(value);},
            [](VSPreparedTextureAdjustment::Row* row,U8 side,const LLMaterial& value)
            {VSPreparedTextureAdjustment::Event event;event.kind=VSPreparedTextureAdjustment::Event::Legacy;event.side=side;event.legacy=new LLMaterial(value);row->events.push_back(std::move(event));},
            [](VSPreparedTextureAdjustment::Row* row,U8 side,const LLGLTFMaterial* value)
            {VSPreparedTextureAdjustment::Event event;event.kind=VSPreparedTextureAdjustment::Event::GLTF;event.side=side;event.gltf=new LLGLTFMaterial(*value);row->events.push_back(std::move(event));});
        for(const auto& face:row.faces)
        {
            F32 s,t;face.value.getScale(&s,&t);
            if(!std::isfinite(s) || !std::isfinite(t))return {};
            if(const auto* material=face.value.getGLTFMaterialOverride())
                for(const auto& transform:material->mTextureTransform)
                    if(!std::isfinite(transform.mScale.mV[VX]) || !std::isfinite(transform.mScale.mV[VY]) ||
                       !std::isfinite(transform.mOffset.mV[VX]) || !std::isfinite(transform.mOffset.mV[VY]) || !std::isfinite(transform.mRotation))return {};
        }
        for(auto& event:row.events)
            if(event.kind==VSPreparedTextureAdjustment::Event::Legacy)
            {
                auto* assets=world.legacyMaterialAssets(token.host);if(!assets)return {};
                event.preparedLegacy=assets->preparePut(token,event.side,*event.legacy);
                if(!event.preparedLegacy)return {};
            }
        if(sendToSimulator && row.capability.empty())
            for(const auto& event:row.events)if(event.kind==VSPreparedTextureAdjustment::Event::GLTF)return {};
        result->rows.push_back(std::move(row));
    }
    if(!current())return {};
    return result;
}

bool vs_publish_prepared_texture_adjustment(const std::shared_ptr<VSPreparedTextureAdjustment>& operation)
{
    if(!operation || operation->published || !operation->current() || !gViewerWindow || !gViewerWindow->nativeContext())return false;
    auto& world=gViewerWindow->nativeContext()->world();
    // Verify every retained input before any TE publication. Position/scale is
    // deliberately separate: it has just been published in the original order.
    for(const auto& row:operation->rows)
    {
        if(!row.current())return false;
        auto object=VSMaterialObject::find(row.object.id);
        if(!object || object.getScale()!=row.scale || object.getNumTEs()!=S32(row.faces.size()))return false;
        for(U32 side=0;side<row.faces.size();++side)
        {
            const auto* entry=object.getTE(side);
            if(!entry || object.getRenderMaterialID(side)!=row.faces[side].materialID || !sameTextureInputs(*entry,row.faces[side].original))return false;
        }
    }
    operation->published=true;
    for(const auto& row:operation->rows)
    {
        if(!operation->current() || !row.current())return false;
        for(U32 side=0;side<row.faces.size();++side)
        {
            const auto& face=row.faces[side];
            // Preserve the live read-only asset/composed material when this face
            // was outside the original planar/stretch adjustment branch.
            if(sameTextureInputs(face.original,face.value))continue;
            world.editTextureEntry(row.object,side,[&face](LLTextureEntry& entry){entry=face.value;return TEM_CHANGE_TEXTURE;});
        }
        for(const auto& event:row.events)
        {
            if(!operation->current() || !row.current())return false;
            if(event.kind==VSPreparedTextureAdjustment::Event::Legacy)
            {
                auto* assets=world.legacyMaterialAssets(row.object.host);
                if(!assets || !assets->put(event.preparedLegacy))return false;
            }
            else
            {
                const auto lease=operation->residency;const auto current=row.current;
                LLGLTFMaterialList::queueModify(row.object.id,event.side,event.gltf,[lease,current]{return current();},row.capability);
            }
        }
        if(row.send && operation->current() && row.current())VSMaterialObject::find(row.object.id).sendTEUpdate();
    }
    return true;
}
