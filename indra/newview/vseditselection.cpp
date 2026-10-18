// Genuine native object/property views for the existing Build controllers.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vseditselection.h"
#include "vsbuildselectionpolicy.h"
#include "vsbuildobjectflags.h"
#include "vsnativesession.h"
#include "llagent.h"
#include "llviewerwindow.h"
#include "llviewercontrol.h"
#include "llviewernetwork.h"
#include "vsvulkancontext.h"
#include "llslurl.h"
#include "lltrans.h"
#include "message.h"
#include "vsobjectphysics.h"
#include "vsobjectcosts.h"
#include "vstexturescaleselection.h"
#include "llvolumemessage.h"
#include "llmaterialtable.h"
#include "llmediaentry.h"
#include "material_codes.h"
#include "llmeshrepository.h"
#include "vsregioncontext.h"
#include "llbbox.h"
#include "rlvhandler.h"
#include <tuple>

U32 VSEditObject::flags() const
{
    if (!*this || !gViewerWindow || !gViewerWindow->nativeContext())
        return 0;
    const auto value = gViewerWindow->nativeContext()->world().objectAdmission(task().nativeObject());
    return value ? value->updateFlags : 0;
}
bool VSEditObject::permMove() const
{
#ifdef HACKED_GODLIKE_VIEWER
    return true;
#else
#ifdef TOGGLE_HACKED_GODLIKE_VIEWER
    if (LLGridManager::instance().isInSLBeta() && gAgent.getGodLevel() >= GOD_MAINTENANCE)
        return true;
#endif
    const auto root = task().getRootEdit();
    return root && (root.objectFlags() & FLAGS_OBJECT_MOVE) != 0;
#endif
}
bool VSEditObject::flagObjectPermanent() const
{ return (flags() & FLAGS_AFFECTS_NAVMESH) != 0; }
bool VSEditObject::flagCharacter() const
{ return (flags() & FLAGS_CHARACTER) != 0; }
bool VSEditObject::getIncludeInSearch() const
{ return (flags() & FLAGS_INCLUDE_IN_SEARCH) != 0; }
U8 VSEditObject::getClickAction() const
{
    if (!*this || !gViewerWindow || !gViewerWindow->nativeContext())
        return 0;
    const auto value = gViewerWindow->nativeContext()->world().objectAdmission(task().nativeObject());
    return value ? value->clickAction : 0;
}
bool VSEditObject::isRootEdit() const
{
    if (!*this || !gViewerWindow || !gViewerWindow->nativeContext())
        return false;
    return gViewerWindow->nativeContext()->world().objectIsRootEdit(task().nativeObject()).value_or(false);
}
bool VSEditObject::payable() const
{
    if (!*this || !gViewerWindow || !gViewerWindow->nativeContext())
        return false;
    auto&      world      = gViewerWindow->nativeContext()->world();
    const auto parent     = world.parentObject(task().nativeObject());
    const auto parentInfo = parent ? world.objectAdmission(*parent) : std::nullopt;
    return (flags() & FLAGS_TAKES_MONEY) != 0 || (parentInfo && (parentInfo->updateFlags & FLAGS_TAKES_MONEY) != 0);
}

VSEditNode::VSEditNode(const std::shared_ptr<VSObjectSelection>& selection, const VSWorldScene::ObjectHandle& object) :
    source(object),
    mObject(object),
    mSelection(selection)
{
    if (const auto metadata = selection->node(object))
        mIndividualSelection = metadata->individual;
    if (const auto property = selection->properties(object))
    {
        mProperties                = property->properties;
        permissionValue            = mProperties->permissions();
        mName                      = mProperties->name;
        mDescription               = mProperties->desc;
        mSaleInfo                  = mProperties->sale_info;
        mAggregateTexturePerm      = mProperties->ag_texture_perms;
        mAggregateTexturePermOwner = mProperties->ag_texture_perms_owner;
        mValid                     = true;
    }
}
VSEditSelection::VSEditSelection()
{
    const auto owner = VSNativeSession::active();
    mSelection       = owner ? owner->retainObjectSelection() : nullptr;
    if (!mSelection)
        return;
    mCurrent = mSelection->selectionGuard();
    for (const auto& token : mSelection->selected())
    {
        auto  node   = mSelection->editNode(token);
        auto* object = node ? node->getObject() : nullptr;
        if (!object)
        {
            mNodes.clear();
            mRoots.clear();
            mRootObjects.clear();
            mStorage.clear();
            mCurrent = []
            {
                return false;
            };
            return;
        }
        mNodes.push_back(node.get());
        if (object->isRootEdit())
        {
            mRootObjects.push_back(node.get());
            if (!node->mIndividualSelection)
                mRoots.push_back(node.get());
        }
        mStorage.push_back(std::move(node));
    }
}
VSEditNode* VSEditSelection::getFirstNode()
{ return mNodes.empty() ? nullptr : mNodes.front(); }
VSEditNode* VSEditSelection::getFirstRootNode()
{ return mRoots.empty() ? nullptr : mRoots.front(); }
bool VSEditSelection::isAttachment() const
{
    const auto type = mSelection ? mSelection->selectionType() : VSObjectSelection::SelectionTypeState{};
    return type.type && *type.type != VSObjectSelection::SelectionType::World;
}
bool VSEditSelection::selectionAllPCode(U8 pcode) const
{ return mSelection && mSelection->selectionAllPCode(pcode); }
template<class Get>
bool VSEditSelection::identity(Get get, LLUUID& result) const
{
    bool       first = true, identical = true;
    const bool parts = gSavedSettings.getBOOL("EditLinkedParts");
    for (const auto* node : parts ? mNodes : mRootObjects)
    {
        if (!node->mValid)
        {
            if (parts)
                continue;
            break;
        }
        const auto id = get(*node->mPermissions);
        if (first)
        {
            result = id;
            first  = false;
        }
        else if (result != id)
        {
            identical = false;
            break;
        }
    }
    return identical;
}
bool VSEditSelection::selectGetCreator(LLUUID& id, std::string& name) const
{
    LLUUID     value;
    const bool same = identity([](const auto& p) { return p.getCreator(); }, value);
    if (value.isNull())
    {
        name = LLTrans::getString("AvatarNameNobody");
        return false;
    }
    id   = value;
    name = same ? LLSLURL("agent", value, "inspect").getSLURLString() : LLTrans::getString("AvatarNameMultiple");
    return same;
}
bool VSEditSelection::selectGetOwner(LLUUID& id, std::string& name) const
{
    LLUUID     value;
    const bool same = identity([](const auto& p) { return p.isGroupOwned() ? p.getGroup() : p.getOwner(); }, value);
    if (value.isNull())
        return false;
    id   = value;
    name = same ? LLSLURL(selectIsGroupOwned() ? "group" : "agent", value, "inspect").getSLURLString()
                : LLTrans::getString("AvatarNameMultiple");
    return same;
}
bool VSEditSelection::selectGetLastOwner(LLUUID& id, std::string& name) const
{
    LLUUID     value;
    const bool same = identity([](const auto& p) { return p.getLastOwner(); }, value);
    if (value.isNull())
        return false;
    id   = value;
    name = same ? LLSLURL("agent", value, "inspect").getSLURLString() : LLTrans::getString("AvatarNameMultiple");
    return same;
}
bool VSEditSelection::selectGetGroup(LLUUID& id) const
{
    LLUUID     value;
    const bool same = identity([](const auto& p) { return p.getGroup(); }, value);
    id              = value;
    return same;
}
bool VSEditSelection::selectIsGroupOwned() const
{
    LLUUID value;
    identity([](const auto& p) { return p.isGroupOwned() ? p.getGroup() : LLUUID::null; }, value);
    return value.notNull();
}
bool VSEditSelection::selectionGetClickAction(U8* out)
{
    const auto* first = getFirstObject();
    if (!first)
        return false;
    *out        = first->getClickAction();
    bool result = true;
    for (const auto* node : mNodes)
        if (const auto* object = node->getObject())
            result = (object->getClickAction() == *out) && result;
    return result;
}
bool VSEditSelection::selectionGetIncludeInSearch(bool* out)
{
    const auto* first = getFirstRootObject();
    if (!first)
        return false;
    *out = first->getIncludeInSearch();
    for (const auto* node : mRoots)
        if (node->getObject()->getIncludeInSearch() != *out)
            return false;
    return true;
}

namespace
{
void buildAgent(LLMessageSystem& msg)
{
    msg.nextBlockFast(_PREHASH_AgentData);
    msg.addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg.addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
}
} // namespace
void VSEditSelection::send(const char* name, bool roots,
    const std::function<void(LLMessageSystem&)>& header,
    const std::function<void(LLMessageSystem&, const VSEditNode&)>& body)
{
    if (!mSelection || !mCurrent()) return;
    mSelection->sendEditCommand(name,
        roots ? VSObjectSelection::SendOrder::RootsOnly : VSObjectSelection::SendOrder::Individuals,
        [&](LLMessageSystem& messages)
        {
            buildAgent(messages);
            header(messages);
        },
        [&](LLMessageSystem& messages, const VSWorldScene::ObjectHandle& object)
        {
            // The retained selection snapshot and source guard keep these
            // actual cached nodes valid throughout this synchronous command.
            for (const auto* node : mNodes)
                if (node->source.id == object.id && node->source.host == object.host &&
                    node->source.local == object.local && node->source.region == object.region &&
                    node->source.generation == object.generation)
                {
                    body(messages, *node);
                    return;
                }
        });
}

void VSEditSelection::selectionSetObjectName(const std::string& name)
{
    if (name.empty())
        return;
    send(
        "ObjectName", !mRoots.empty(), [](auto&) {},
        [&](auto& msg, const auto& node)
        {
            msg.nextBlockFast(_PREHASH_ObjectData);
            msg.addU32Fast(_PREHASH_LocalID, node.source.local);
            msg.addStringFast(_PREHASH_Name, name);
        });
}
void VSEditSelection::selectionSetObjectDescription(const std::string& description)
{
    send(
        "ObjectDescription", !mRoots.empty(), [](auto&) {},
        [&](auto& msg, const auto& node)
        {
            msg.nextBlockFast(_PREHASH_ObjectData);
            msg.addU32Fast(_PREHASH_LocalID, node.source.local);
            msg.addStringFast(_PREHASH_Description, description);
        });
}
void VSEditSelection::selectionSetObjectSaleInfo(const LLSaleInfo& sale)
{
    send(
        "ObjectSaleInfo", true, [](auto&) {},
        [&](auto& msg, const auto& node)
        {
            msg.nextBlockFast(_PREHASH_ObjectData);
            msg.addU32Fast(_PREHASH_LocalID, node.source.local);
            sale.packMessage(&msg);
        });
}
void VSEditSelection::selectionSetObjectPermissions(U8 field, bool enabled, U32 mask, bool override)
{
    send(
        "ObjectPermissions", true,
        [&](auto& msg)
        {
            msg.nextBlockFast(_PREHASH_HeaderData);
            msg.addBOOLFast(_PREHASH_Override, override);
        },
        [&](auto& msg, const auto& node)
        {
            msg.nextBlockFast(_PREHASH_ObjectData);
            msg.addU32Fast(_PREHASH_ObjectLocalID, node.source.local);
            msg.addU8Fast(_PREHASH_Field, field);
            msg.addBOOLFast(_PREHASH_Set, enabled);
            msg.addU32Fast(_PREHASH_Mask, mask);
        });
}
void VSEditSelection::selectionSetClickAction(U8 action)
{
    if (!mCurrent || !mCurrent() || !gViewerWindow || !gViewerWindow->nativeContext())
        return;
    for (const auto* node : mNodes)
        gViewerWindow->nativeContext()->world().previewClickAction(node->source, action);
    send(
        "ObjectClickAction", false, [](auto&) {},
        [&](auto& msg, const auto& node)
        {
            msg.nextBlockFast(_PREHASH_ObjectData);
            msg.addU32Fast(_PREHASH_ObjectLocalID, node.source.local);
            msg.addU8("ClickAction", action);
        });
}
void VSEditSelection::selectionSetIncludeInSearch(bool include)
{
    if (!mCurrent || !mCurrent() || !gViewerWindow || !gViewerWindow->nativeContext())
        return;
    for (const auto* node : mRoots)
        gViewerWindow->nativeContext()->world().previewObjectFlag(node->source, FLAGS_INCLUDE_IN_SEARCH, include);
    send(
        "ObjectIncludeInSearch", true, [](auto&) {},
        [&](auto& msg, const auto& node)
        {
            msg.nextBlockFast(_PREHASH_ObjectData);
            msg.addU32Fast(_PREHASH_ObjectLocalID, node.source.local);
            msg.addBOOL("IncludeInSearch", include);
        });
}
void VSEditSelection::sendGroup(const LLUUID& group)
{
    send(
        "ObjectGroup", true, [&](auto& msg) { msg.addUUIDFast(_PREHASH_GroupID, group); },
        [](auto& msg, const auto& node)
        {
            msg.nextBlockFast(_PREHASH_ObjectData);
            msg.addU32Fast(_PREHASH_ObjectLocalID, node.source.local);
        });
}
void VSEditSelection::sendOwner(const LLUUID& owner, const LLUUID& group, bool override)
{
    send(
        "ObjectOwner", true,
        [&](auto& msg)
        {
            msg.nextBlockFast(_PREHASH_HeaderData);
            msg.addBOOLFast(_PREHASH_Override, override);
            msg.addUUIDFast(_PREHASH_OwnerID, owner);
            msg.addUUIDFast(_PREHASH_GroupID, group);
        },
        [](auto& msg, const auto& node)
        {
            msg.nextBlockFast(_PREHASH_ObjectData);
            msg.addU32Fast(_PREHASH_ObjectLocalID, node.source.local);
        });
}

bool VSEditSelection::selectGetModify()
{ return vs_build_selectGetModify(*this); }
bool VSEditSelection::selectGetRootsModify()
{ return vs_build_selectGetRootsModify(*this); }
bool VSEditSelection::selectGetNonPermanentEnforced()
{ return vs_build_selectGetNonPermanentEnforced(*this); }
bool VSEditSelection::selectGetRootsNonPermanentEnforced()
{ return vs_build_selectGetRootsNonPermanentEnforced(*this); }
bool VSEditSelection::selectGetNonPathfinding()
{ return vs_build_selectGetNonPathfinding(*this); }
bool VSEditSelection::selectGetRootsNonPathfinding()
{ return vs_build_selectGetRootsNonPathfinding(*this); }
bool VSEditSelection::selectGetPermanent()
{ return vs_build_selectGetPermanent(*this); }
bool VSEditSelection::selectGetRootsPermanent()
{ return vs_build_selectGetRootsPermanent(*this); }
bool VSEditSelection::selectGetCharacter()
{ return vs_build_selectGetCharacter(*this); }
bool VSEditSelection::selectGetRootsCharacter()
{ return vs_build_selectGetRootsCharacter(*this); }
bool VSEditSelection::selectGetRootsCopy()
{ return vs_build_selectGetRootsCopy(*this); }
bool VSEditSelection::selectGetRootsTransfer()
{ return vs_build_selectGetRootsTransfer(*this); }
bool VSEditSelection::selectGetPerm(U8 which_perm, U32* mask_on, U32* mask_off)
{ return vs_build_selectGetPerm(*this, which_perm, mask_on, mask_off); }
bool VSEditSelection::selectGetSaleInfo(LLSaleInfo& result_sale_info)
{ return vs_build_selectGetSaleInfo(*this, result_sale_info); }
void VSEditSelection::selectGetAggregateSaleInfo(U32&  num_for_sale,
                                                 bool& is_for_sale_mixed,
                                                 bool& is_sale_price_mixed,
                                                 S32&  total_sale_price,
                                                 S32&  individual_sale_price)
{
    vs_build_selectGetAggregateSaleInfo(*this, num_for_sale, is_for_sale_mixed, is_sale_price_mixed, total_sale_price,
                                        individual_sale_price);
}

LLColor3 VSEditObject::getLightLinearColor() const
{
    const LLLightParams* param_block = getLightParams();
    if (param_block)
    {
        return LLColor3(param_block->getLinearColor()) * param_block->getLinearColor().mV[3];
    }
    else
    {
        return LLColor3(1, 1, 1);
    }
}
LLColor3 VSEditObject::getLightLinearBaseColor() const
{
    const LLLightParams* param_block = getLightParams();
    if (param_block)
    {
        return LLColor3(param_block->getLinearColor());
    }
    else
    {
        return LLColor3(1, 1, 1);
    }
}
LLColor3 VSEditObject::getLightSRGBColor() const
{
    LLColor3 ret = getLightLinearColor();
    ret          = srgbColor3(ret);
    return ret;
}
LLColor3 VSEditObject::getLightSRGBBaseColor() const
{ return srgbColor3(getLightLinearBaseColor()); }
F32 VSEditObject::getLightIntensity() const
{
    const LLLightParams* param_block = getLightParams();
    if (param_block)
    {
        return param_block->getLinearColor().mV[3];
    }
    else
    {
        return 1.f;
    }
}
F32 VSEditObject::getLightRadius() const
{
    const LLLightParams* param_block = getLightParams();
    if (param_block)
    {
        return param_block->getRadius();
    }
    else
    {
        return 0.f;
    }
}
F32 VSEditObject::getLightFalloff(const F32 fudge_factor) const
{
    const LLLightParams* param_block = getLightParams();
    if (param_block)
    {
        return param_block->getFalloff() * fudge_factor;
    }
    else
    {
        return 0.f;
    }
}
void VSEditObject::setLightLinearColor(const LLColor3& color)
{
    LLLightParams* param_block = getLightParams();
    if (param_block)
    {
        if (param_block->getLinearColor() != color)
        {
            param_block->setLinearColor(LLColor4(color, param_block->getLinearColor().mV[3]));
            parameterChanged(LLNetworkData::PARAMS_LIGHT, true);
        }
    }
}
void VSEditObject::setLightSRGBColor(const LLColor3& color)
{ setLightLinearColor(linearColor3(color)); }
void VSEditObject::setLightIntensity(F32 intensity)
{
    LLLightParams* param_block = getLightParams();
    if (param_block)
    {
        if (param_block->getLinearColor().mV[3] != intensity)
        {
            param_block->setLinearColor(LLColor4(LLColor3(param_block->getLinearColor()), intensity));
            parameterChanged(LLNetworkData::PARAMS_LIGHT, true);
        }
    }
}
void VSEditObject::setLightRadius(F32 radius)
{
    LLLightParams* param_block = getLightParams();
    if (param_block)
    {
        if (param_block->getRadius() != radius)
        {
            param_block->setRadius(radius);
            parameterChanged(LLNetworkData::PARAMS_LIGHT, true);
        }
    }
}
void VSEditObject::setLightFalloff(F32 falloff)
{
    LLLightParams* param_block = getLightParams();
    if (param_block)
    {
        if (param_block->getFalloff() != falloff)
        {
            param_block->setFalloff(falloff);
            parameterChanged(LLNetworkData::PARAMS_LIGHT, true);
        }
    }
}
const LLVector3& VSEditObject::getSpotLightParams() const
{
    const LLLightImageParams* param_block = getLightImageParams();
    if (param_block)
    {
        return param_block->getParams();
    }

    return LLVector3::zero;
}
void VSEditObject::setSpotLightParams(LLVector3 params)
{
    LLLightImageParams* param_block = getLightImageParams();
    if (param_block && param_block->getParams() != params)
    {
        param_block->setParams(params);
        parameterChanged(LLNetworkData::PARAMS_LIGHT_IMAGE, true);
    }
}
const LLUUID& VSEditObject::getLightTextureID() const
{
    const LLLightImageParams* param_block = getLightImageParams();
    if (param_block)
    {
        return param_block->getLightTexture();
    }

    return LLUUID::null;
}
bool VSEditObject::isLightSpotlight() const
{
    const LLLightImageParams* params = getLightImageParams();
    if (params)
    {
        return params->isLightSpotlight();
    }
    return false;
}
F32 VSEditObject::getReflectionProbeAmbiance() const
{
    const LLReflectionProbeParams* param_block = getReflectionProbeParams();
    if (param_block)
    {
        return param_block->getAmbiance();
    }
    else
    {
        return 0.f;
    }
}
F32 VSEditObject::getReflectionProbeNearClip() const
{
    const LLReflectionProbeParams* param_block = getReflectionProbeParams();
    if (param_block)
    {
        return param_block->getClipDistance();
    }
    else
    {
        return 0.f;
    }
}
bool VSEditObject::getReflectionProbeIsBox() const
{
    const LLReflectionProbeParams* param_block = getReflectionProbeParams();
    if (param_block)
    {
        return param_block->getIsBox();
    }

    return false;
}
bool VSEditObject::getReflectionProbeIsDynamic() const
{
    const LLReflectionProbeParams* param_block = getReflectionProbeParams();
    if (param_block)
    {
        return param_block->getIsDynamic();
    }

    return false;
}
bool VSEditObject::getReflectionProbeIsMirror() const
{
    const LLReflectionProbeParams* param_block = getReflectionProbeParams();
    if (param_block)
    {
        return param_block->getIsMirror();
    }

    return false;
}
bool VSEditObject::setReflectionProbeAmbiance(F32 ambiance)
{
    LLReflectionProbeParams* param_block = getReflectionProbeParams();
    if (param_block)
    {
        if (param_block->getAmbiance() != ambiance)
        {
            param_block->setAmbiance(ambiance);
            parameterChanged(LLNetworkData::PARAMS_REFLECTION_PROBE, true);
            return true;
        }
    }

    return false;
}
bool VSEditObject::setReflectionProbeNearClip(F32 near_clip)
{
    LLReflectionProbeParams* param_block = getReflectionProbeParams();
    if (param_block)
    {
        if (param_block->getClipDistance() != near_clip)
        {
            param_block->setClipDistance(near_clip);
            parameterChanged(LLNetworkData::PARAMS_REFLECTION_PROBE, true);
            return true;
        }
    }

    return false;
}
bool VSEditObject::setReflectionProbeIsBox(bool is_box)
{
    LLReflectionProbeParams* param_block = getReflectionProbeParams();
    if (param_block)
    {
        if (param_block->getIsBox() != is_box)
        {
            param_block->setIsBox(is_box);
            parameterChanged(LLNetworkData::PARAMS_REFLECTION_PROBE, true);
            return true;
        }
    }

    return false;
}
bool VSEditObject::setReflectionProbeIsDynamic(bool is_dynamic)
{
    LLReflectionProbeParams* param_block = getReflectionProbeParams();
    if (param_block)
    {
        if (param_block->getIsDynamic() != is_dynamic)
        {
            param_block->setIsDynamic(is_dynamic);
            parameterChanged(LLNetworkData::PARAMS_REFLECTION_PROBE, true);
            return true;
        }
    }

    return false;
}
bool VSEditObject::setReflectionProbeIsMirror(bool is_mirror)
{
    LLReflectionProbeParams* param_block = getReflectionProbeParams();
    if (param_block)
    {
        if (param_block->getIsMirror() != is_mirror)
        {
            LL_INFOS() << "Setting reflection probe mirror to " << is_mirror << LL_ENDL;
            param_block->setIsMirror(is_mirror);
            parameterChanged(LLNetworkData::PARAMS_REFLECTION_PROBE, true);

            return true;
        }
    }

    return false;
}

namespace
{
VSWorldScene* edit_world()
{ return gViewerWindow && gViewerWindow->nativeContext() ? &gViewerWindow->nativeContext()->world() : nullptr; }
} // namespace
LLPointer<LLVolume> VSEditObject::getVolume() const
{
    auto* world = edit_world();
    return world && *this ? world->objectVolume(task().nativeObject()) : nullptr;
}
const VSObjectEditParameters* VSEditObject::parameters() const
{
    auto* world = edit_world();
    if (!world || !*this)
        return nullptr;
    return world->editParameters(task().nativeObject()).get();
}
VSObjectEditParameters* VSEditObject::mutableParameters()
{
    auto* world = edit_world();
    if (!world || !*this)
        return nullptr;
    const auto source = world->editParameters(task().nativeObject());
    if (!source)
        return nullptr;
    if (!mParameters || mParameterOwner.lock() != source)
    {
        if (!mParameterResidency)
            mParameterResidency = world->reserveSpatialBytes(sizeof(VSObjectEditParameters) + 64);
        if (!mParameterResidency)
            return nullptr;
        mParameters     = std::make_unique<VSObjectEditParameters>(*source);
        mParameterOwner = source;
    }
    return mParameters.get();
}
void VSEditObject::parameterChanged(U16 type, bool localOrigin)
{
    auto* world = edit_world();
    auto* data  = mutableParameters();
    if (!world || !data)
        return;
    if (const auto* value = data->get(type))
        world->setExtraParameter(task().nativeObject(), type, *value, data->inUse(type), localOrigin);
}
bool VSEditObject::setParameterEntry(U16 type, const LLNetworkData& value, bool localOrigin)
{
    auto* world = edit_world();
    if (!world || !*this)
        return false;
    const bool changed = world->setExtraParameter(task().nativeObject(), type, value, true, localOrigin);
    mParameters.reset();
    return changed;
}
bool VSEditObject::setParameterEntryInUse(U16 type, bool use, bool localOrigin)
{
    auto* world = edit_world();
    const auto* data = parameters();
    if (!world || !data) return false;
    const auto* value = data->get(type);
    if (!value) return false;
    const bool changed = world->setExtraParameter(task().nativeObject(), type, *value, use, localOrigin);
    mParameters.reset();
    return changed;
}

const LLLightParams* VSEditObject::getLightParams() const
{
    auto* data = parameters();
    return data && data->inUse(LLNetworkData::PARAMS_LIGHT) ? &data->light : nullptr;
}
LLLightParams* VSEditObject::getLightParams()
{
    auto* data = mutableParameters();
    return data && data->inUse(LLNetworkData::PARAMS_LIGHT) ? &data->light : nullptr;
}
const LLLightImageParams* VSEditObject::getLightImageParams() const
{
    auto* data = parameters();
    return data && data->inUse(LLNetworkData::PARAMS_LIGHT_IMAGE) ? &data->lightImage : nullptr;
}
LLLightImageParams* VSEditObject::getLightImageParams()
{
    auto* data = mutableParameters();
    return data && data->inUse(LLNetworkData::PARAMS_LIGHT_IMAGE) ? &data->lightImage : nullptr;
}
const LLReflectionProbeParams* VSEditObject::getReflectionProbeParams() const
{
    auto* data = parameters();
    return data && data->inUse(LLNetworkData::PARAMS_REFLECTION_PROBE) ? &data->reflection : nullptr;
}
LLReflectionProbeParams* VSEditObject::getReflectionProbeParams()
{
    auto* data = mutableParameters();
    return data && data->inUse(LLNetworkData::PARAMS_REFLECTION_PROBE) ? &data->reflection : nullptr;
}
const LLFlexibleObjectData* VSEditObject::getFlexibleObjectData() const
{
    auto* data = parameters();
    return data && data->inUse(LLNetworkData::PARAMS_FLEXIBLE) ? &data->flexible : nullptr;
}
const LLSculptParams* VSEditObject::getSculptParams() const
{
    auto* data = parameters();
    return data && data->inUse(LLNetworkData::PARAMS_SCULPT) ? &data->sculpt : nullptr;
}
bool VSEditObject::getIsLight() const
{ return getLightParams() != nullptr; }
void VSEditObject::setIsLight(bool enabled)
{
    if (getIsLight() != enabled)
        setParameterEntryInUse(LLNetworkData::PARAMS_LIGHT, enabled, true);
}
void VSEditObject::setLightTextureID(LLUUID id)
{
    if (id.isNull())
    {
        if (getLightImageParams())
            setParameterEntryInUse(LLNetworkData::PARAMS_LIGHT_IMAGE, false, true);
        return;
    }
    if (!getLightImageParams())
        setParameterEntryInUse(LLNetworkData::PARAMS_LIGHT_IMAGE, true, true);
    if (auto* data = getLightImageParams(); data && data->getLightTexture() != id)
    {
        data->setLightTexture(id);
        parameterChanged(LLNetworkData::PARAMS_LIGHT_IMAGE, true);
    }
}
bool VSEditObject::isReflectionProbe() const
{ return getReflectionProbeParams() != nullptr; }
bool VSEditObject::setIsReflectionProbe(bool enabled)
{
    const bool changed = isReflectionProbe() != enabled;
    if (changed)
        setParameterEntryInUse(LLNetworkData::PARAMS_REFLECTION_PROBE, enabled, true);
    return changed;
}
U8 VSEditObject::getPhysicsShapeType() const
{
    auto* world = edit_world();
    if (!world || !*this)
        return 0;
    const auto data = world->editParameters(task().nativeObject());
    if (!data)
        return 0;
    if (data->physics.unknown)
        if (const auto owner = VSNativeSession::active())
            if (auto* service = owner->objectPhysics())
                service->request(task().nativeObject());
    return data->physics.shape;
}
F32 VSEditObject::getPhysicsGravity() const
{
    auto*      world = edit_world();
    const auto data  = world && *this ? world->editParameters(task().nativeObject()) : nullptr;
    return data ? data->physics.gravity : 0.f;
}
void VSEditObject::setPhysicsGravity(F32 value)
{
    auto* world = edit_world();
    if (!world || !*this)
        return;
    const auto data = world->editParameters(task().nativeObject());
    if (!data)
        return;
    auto physics    = data->physics;
    physics.gravity = value;
    world->setPhysicsProperties(task().nativeObject(), physics);
}
F32 VSEditObject::getPhysicsDensity() const
{
    auto*      world = edit_world();
    const auto data  = world && *this ? world->editParameters(task().nativeObject()) : nullptr;
    return data ? data->physics.density : 0.f;
}
void VSEditObject::setPhysicsDensity(F32 value)
{
    auto* world = edit_world();
    if (!world || !*this)
        return;
    const auto data = world->editParameters(task().nativeObject());
    if (!data)
        return;
    auto physics    = data->physics;
    physics.density = value;
    world->setPhysicsProperties(task().nativeObject(), physics);
}
F32 VSEditObject::getPhysicsFriction() const
{
    auto*      world = edit_world();
    const auto data  = world && *this ? world->editParameters(task().nativeObject()) : nullptr;
    return data ? data->physics.friction : 0.f;
}
void VSEditObject::setPhysicsFriction(F32 value)
{
    auto* world = edit_world();
    if (!world || !*this)
        return;
    const auto data = world->editParameters(task().nativeObject());
    if (!data)
        return;
    auto physics     = data->physics;
    physics.friction = value;
    world->setPhysicsProperties(task().nativeObject(), physics);
}
F32 VSEditObject::getPhysicsRestitution() const
{
    auto*      world = edit_world();
    const auto data  = world && *this ? world->editParameters(task().nativeObject()) : nullptr;
    return data ? data->physics.restitution : 0.f;
}
void VSEditObject::setPhysicsRestitution(F32 value)
{
    auto* world = edit_world();
    if (!world || !*this)
        return;
    const auto data = world->editParameters(task().nativeObject());
    if (!data)
        return;
    auto physics        = data->physics;
    physics.restitution = value;
    world->setPhysicsProperties(task().nativeObject(), physics);
}
void VSEditObject::setPhysicsShapeType(U8 value)
{
    auto* world = edit_world();
    if (!world || !*this)
        return;
    const auto data = world->editParameters(task().nativeObject());
    if (!data)
        return;
    auto       physics = data->physics;
    const bool changed = physics.shape != value;
    physics.shape      = value;
    physics.unknown    = false;
    world->setPhysicsProperties(task().nativeObject(), physics);
    if (changed)
        setObjectCostStale();
}
bool VSEditObject::flagUsePhysics() const
{ return (flags() & FLAGS_USE_PHYSICS) != 0; }
bool VSEditObject::flagTemporaryOnRez() const
{ return (flags() & FLAGS_TEMPORARY_ON_REZ) != 0; }
bool VSEditObject::flagPhantom() const
{ return (flags() & FLAGS_PHANTOM) != 0; }
bool VSEditObject::flagVolumeDetect() const
{ return (flags() & FLAGS_VOLUME_DETECT) != 0; }
bool VSEditObject::setFlags(U32 mask, bool value)
{
    const U32 previous = flags();
    auto*     world    = edit_world();
    if (!world || !*this || !world->previewObjectFlag(task().nativeObject(), mask, value))
        return false;
    updateFlags(); // Reference deliberately resends even if the local bit did not change.
    return flags() != previous;
}
U8 VSEditObject::getMaterial() const
{
    auto*      world = edit_world();
    const auto value = world && *this ? world->objectSnapshot(task().nativeObject()) : std::nullopt;
    return value ? value->material : 0;
}
bool VSEditObject::setMaterial(U8 value)
{
    auto* world = edit_world();
    return world && *this && world->previewMaterial(task().nativeObject(), value);
}
void VSEditObject::updateFlags(bool physicsChanged)
{
    if (!*this || !gMessageSystem)
        return;
    const auto current = task().objectGuard();
    gMessageSystem->newMessage("ObjectFlagUpdate");
    gMessageSystem->nextBlockFast(_PREHASH_AgentData);
    gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID, task().getLocalID());
    gMessageSystem->addBOOLFast(_PREHASH_UsePhysics, flagUsePhysics());
    gMessageSystem->addBOOL("IsTemporary", flagTemporaryOnRez());
    gMessageSystem->addBOOL("IsPhantom", flagPhantom());
    gMessageSystem->addBOOL("CastsShadows", false);
    if (physicsChanged)
    {
        gMessageSystem->nextBlock("ExtraPhysics");
        gMessageSystem->addU8("PhysicsShapeType", getPhysicsShapeType());
        gMessageSystem->addF32("Density", getPhysicsDensity());
        gMessageSystem->addF32("Friction", getPhysicsFriction());
        gMessageSystem->addF32("Restitution", getPhysicsRestitution());
        gMessageSystem->addF32("GravityMultiplier", getPhysicsGravity());
    }
    if (current())
        gMessageSystem->sendReliable(task().getHost());
    else
        gMessageSystem->clearMessage();
}
void VSEditObject::sendMaterialUpdate() const
{
    if (!*this || !gMessageSystem)
        return;
    const auto current = task().objectGuard();
    gMessageSystem->newMessageFast(_PREHASH_ObjectMaterial);
    gMessageSystem->nextBlockFast(_PREHASH_AgentData);
    gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
    gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID, task().getLocalID());
    gMessageSystem->addU8Fast(_PREHASH_Material, getMaterial());
    if (current())
        gMessageSystem->sendReliable(task().getHost());
    else
        gMessageSystem->clearMessage();
}
void VSEditObject::sendShapeUpdate() const
{
    const auto volume = getVolume();
    if (!volume || !*this || !gMessageSystem)
        return;
    const auto current = task().objectGuard();
    gMessageSystem->newMessageFast(_PREHASH_ObjectShape);
    gMessageSystem->nextBlockFast(_PREHASH_AgentData);
    gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
    gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID, task().getLocalID());
    LLVolumeMessage::packVolumeParams(&volume->getParams(), gMessageSystem);
    if (current())
        gMessageSystem->sendReliable(task().getHost());
    else
        gMessageSystem->clearMessage();
}

void VSEditSelection::selectionSetDensity(F32 value)
{
    for (const auto* node : mNodes)
    {
        if (!mCurrent())
            return;
        auto* object = const_cast<VSEditNode*>(node)->getObject();
        if (object && object->permModify())
        {
            object->setPhysicsDensity(value);
            object->updateFlags(true);
        }
    }
}

void VSEditSelection::selectionSetFriction(F32 value)
{
    for (const auto* node : mNodes)
    {
        if (!mCurrent())
            return;
        auto* object = const_cast<VSEditNode*>(node)->getObject();
        if (object && object->permModify())
        {
            object->setPhysicsFriction(value);
            object->updateFlags(true);
        }
    }
}

void VSEditSelection::selectionSetGravity(F32 value)
{
    for (const auto* node : mNodes)
    {
        if (!mCurrent())
            return;
        auto* object = const_cast<VSEditNode*>(node)->getObject();
        if (object && object->permModify())
        {
            object->setPhysicsGravity(value);
            object->updateFlags(true);
        }
    }
}

void VSEditSelection::selectionSetRestitution(F32 value)
{
    for (const auto* node : mNodes)
    {
        if (!mCurrent())
            return;
        auto* object = const_cast<VSEditNode*>(node)->getObject();
        if (object && object->permModify())
        {
            object->setPhysicsRestitution(value);
            object->updateFlags(true);
        }
    }
}

void VSEditSelection::selectionSetPhysicsType(U8 value)
{
    for (const auto* node : mNodes)
    {
        if (!mCurrent())
            return;
        auto* object = const_cast<VSEditNode*>(node)->getObject();
        if (object && object->permModify())
        {
            object->setPhysicsShapeType(value);
            object->updateFlags(true);
        }
    }
}

void VSEditSelection::selectionSetMaterial(U8 value)
{
    for (const auto* node : mNodes)
    {
        if (!mCurrent())
            return;
        auto* object = const_cast<VSEditNode*>(node)->getObject();
        if (object && object->permModify())
        {
            object->setMaterial(value | (object->getMaterial() & ~LL_MCODE_MASK));
            object->sendMaterialUpdate();
        }
    }
}

VSEditObject* VSEditObject::getRootEdit() const
{
    if (!*this)
        return nullptr;
    const auto root = task().getRootEdit();
    if (!root)
        return nullptr;
    if (task().sameSource(root))
        return const_cast<VSEditObject*>(this);
    if (!mRoot || !mRoot->task().sameSource(root) || !*mRoot)
        mRoot = std::make_unique<VSEditObject>(root.nativeObject());
    return *mRoot ? mRoot.get() : nullptr;
}
VSEditObject* VSEditObject::getParent() const
{
    if (!*this)
        return nullptr;
    const auto parent = task().getParent();
    if (!parent)
        return nullptr;
    if (!mParent || !mParent->task().sameSource(parent) || !*mParent)
        mParent = std::make_unique<VSEditObject>(parent.nativeObject());
    return *mParent ? mParent.get() : nullptr;
}
std::vector<VSEditObject*> VSEditObject::getChildren() const
{
    std::vector<VSEditObject*> result;
    auto*                      world = edit_world();
    if (!world || !*this)
        return result;
    const auto children = world->directChildren(task().nativeObject());
    if (!children)
        return result;
    mChildren.clear();
    for (const auto& child : *children)
    {
        auto object = std::make_unique<VSEditObject>(child);
        if (*object)
        {
            result.push_back(object.get());
            mChildren.push_back(std::move(object));
        }
    }
    return result;
}
bool VSEditObject::isSelected() const
{
    const auto  owner     = VSNativeSession::active();
    const auto* selection = owner ? owner->existingObjectSelection() : nullptr;
    return selection && selection->contains(task().nativeObject());
}
LLVector3 VSEditObject::getScale() const
{
    auto*      world = edit_world();
    const auto value = world && *this ? world->objectLocalTransform(task().nativeObject()) : std::nullopt;
    return value ? value->scale : LLVector3::zero;
}
bool VSEditObject::setScale(const LLVector3& scale, bool)
{
    auto* world = edit_world();
    auto value = world && *this ? world->objectLocalTransform(task().nativeObject()) : std::nullopt;
    if (!value) return false;
    value->scale = scale;
    return world->previewLocalTransform(task().nativeObject(), *value, UPD_SCALE);
}
void VSEditObject::updateVolume(const LLVolumeParams& params)
{
    auto* world = edit_world();
    if (world && *this && world->previewVolume(task().nativeObject(), params))
        sendShapeUpdate();
}
bool VSEditObject::isMesh() const
{
    const auto* params = getSculptParams();
    return params && (params->getSculptType() & LL_SCULPT_TYPE_MASK) == LL_SCULPT_TYPE_MESH;
}
bool VSEditObject::isFlexible() const
{
    const auto volume = getVolume();
    return volume && volume->getParams().getPathParams().getCurveType() == LL_PCODE_PATH_FLEXIBLE;
}
bool VSEditObject::canBeFlexible() const
{
    const auto volume = getVolume();
    if (!volume)
        return false;
    const U8 path = volume->getParams().getPathParams().getCurveType();
    return path == LL_PCODE_PATH_FLEXIBLE || path == LL_PCODE_PATH_LINE;
}
bool VSEditObject::setIsFlexible(bool enabled)
{
    if (enabled == isFlexible() || !canBeFlexible())
        return false;
    auto* world = edit_world();
    auto* data  = parameters();
    if (!world || !data)
        return false;
    if (!world->setExtraParameter(task().nativeObject(), LLNetworkData::PARAMS_FLEXIBLE, data->flexible, enabled, true))
        return false;
    mParameters.reset();
    if (enabled)
    {
        setFlags(FLAGS_USE_PHYSICS, false);
        setFlags(FLAGS_PHANTOM, true);
    }
    else
        setFlags(FLAGS_PHANTOM, false);
    return true;
}
U32 VSEditObject::getExtendedMeshFlags() const
{
    const auto* data = parameters();
    return data && data->inUse(LLNetworkData::PARAMS_EXTENDED_MESH) ? data->mesh.getFlags() : 0;
}
void VSEditObject::setExtendedMeshFlags(U32 flags)
{
    auto* data = parameters();
    if (!data || getExtendedMeshFlags() == flags)
        return;
    auto value = data->mesh;
    value.setFlags(flags);
    setParameterEntry(LLNetworkData::PARAMS_EXTENDED_MESH, value, true);
}
bool VSEditObject::isAnimatedObject() const
{
    const auto* root = getRootEdit();
    return root && root->getPCode() == LL_PCODE_VOLUME &&
           (root->getExtendedMeshFlags() & LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG) != 0;
}
bool VSEditObject::canBeAnimatedObject() const
{
    auto*      world = edit_world();
    const auto owner = VSNativeSession::active();
    if (!world || !owner || !*this)
        return false;
    const auto family = world->linkedFamily(task().nativeObject(), false, true);
    if (!family)
        return false;
    F32 triangles = 0;
    for (const auto& handle : *family)
    {
        VSEditObject object(handle.id);
        const auto*  sculpt = object.getSculptParams();
        if (sculpt && (sculpt->getSculptType() & LL_SCULPT_TYPE_MASK) == LL_SCULPT_TYPE_MESH)
            triangles += gMeshRepo.getEstTrianglesMax(sculpt->getSculptTexture());
    }
    const auto features = owner->simulatorFeatures(owner->host());
    const S32  limit =
        features && features->has("AnimatedObjects") ? (*features)["AnimatedObjects"]["AnimatedObjectMaxTris"].asInteger() : 0;
    return triangles >= 0 && triangles <= limit;
}

void VSEditObject::refreshBakeTexture()
{
    if (auto* world = edit_world(); world && *this)
        world->refreshBakeTexture(task().nativeObject());
}

VSEditObject* VSEditSelection::getPrimaryObject()
{
    const auto primary = mSelection ? mSelection->primaryObject() : std::nullopt;
    if (primary)
        for (const auto& node : mStorage)
            if (node->source.id == primary->id)
                return node->getObject();
    return nullptr;
}

VSEditObject* VSEditObject::getRoot() const
{
    auto* world = edit_world();
    if (!world || !*this)
        return nullptr;
    const auto root = world->rootObject(task().nativeObject(), false);
    if (!root)
        return nullptr;
    if (task().sameSource(VSTaskInventoryObject::find(*root)))
        return const_cast<VSEditObject*>(this);
    if (!mTopRoot || !mTopRoot->task().sameSource(VSTaskInventoryObject::find(*root)) || !*mTopRoot)
        mTopRoot = std::make_unique<VSEditObject>(*root);
    return *mTopRoot ? mTopRoot.get() : nullptr;
}
bool VSEditObject::setFlagsWithoutUpdate(U32 mask, bool value)
{
    auto* world = edit_world();
    if (!world || !*this)
        return false;
    const U32 previous = flags();
    if (!world->previewObjectFlag(task().nativeObject(), mask, value))
        return false;
    return flags() != previous;
}
void VSEditObject::setClickAction(U8 action)
{
    auto* world = edit_world();
    if (!world || !*this)
        return;
    const auto current = task().objectGuard();
    world->previewClickAction(task().nativeObject(), action);
    gMessageSystem->newMessageFast(_PREHASH_ObjectClickAction);
    gMessageSystem->nextBlockFast(_PREHASH_AgentData);
    gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
    gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID, task().getLocalID());
    gMessageSystem->addU8Fast(_PREHASH_ClickAction, action);
    if (current())
        gMessageSystem->sendReliable(task().getHost());
    else
        gMessageSystem->clearMessage();
}

void VSEditSelection::selectionUpdatePhantom(bool value)
{
    for (auto* node : mNodes)
    {
        if (!mCurrent())
            return;
        if (auto* object = node->getObject())
            vs_build_apply_flags(object, FLAGS_PHANTOM, value, [](auto* value) { return value->getRoot(); });
    }
}

void VSEditSelection::selectionUpdatePhysics(bool value)
{
    for (auto* node : mNodes)
    {
        if (!mCurrent())
            return;
        if (auto* object = node->getObject())
            vs_build_apply_flags(object, FLAGS_USE_PHYSICS, value, [](auto* value) { return value->getRoot(); });
    }
}

void VSEditSelection::selectionUpdateTemporary(bool value)
{
    for (auto* node : mNodes)
    {
        if (!mCurrent())
            return;
        if (auto* object = node->getObject())
            vs_build_apply_flags(object, FLAGS_TEMPORARY_ON_REZ, value, [](auto* value) { return value->getRoot(); });
    }
}

VSObjectCostValues VSEditObject::costs() const
{
    auto* world = edit_world();
    if (!world || !*this)
        return {};
    const auto record = world->editParameters(task().nativeObject());
    if (!record)
        return {};
    if (record->costs.stale)
        if (const auto owner = VSNativeSession::active())
            if (auto* service = owner->objectCosts())
                service->request(task().nativeObject());
    return record->costs;
}
void VSEditObject::setObjectCostStale()
{
    auto* world = edit_world();
    if (!world || !*this)
        return;
    const auto record = world->editParameters(task().nativeObject());
    if (!record)
        return;
    auto costs  = record->costs;
    costs.stale = true;
    world->setObjectCostValues(task().nativeObject(), costs);
}

bool VSEditSelection::selectGetEditMoveLinksetPermissions(bool& move, bool& modify)
{
    return vs_build_selectGetEditMoveLinksetPermissions(*this, move, modify,
                                                        [](auto* object) { return object->task().agentSittingOnRoot(); });
}
bool VSEditSelection::selectGetAggregateTexturePermissions(LLAggregatePermissions& result)
{ return vs_build_selectGetAggregateTexturePermissions(*this, result); }
LLVector3 VSEditObject::getPosition() const
{
    auto*      world = edit_world();
    const auto value = world && *this ? world->objectLocalTransform(task().nativeObject()) : std::nullopt;
    return value ? value->position : LLVector3::zero;
}
LLQuaternion VSEditObject::getRotation() const
{
    auto*      world = edit_world();
    const auto value = world && *this ? world->objectLocalTransform(task().nativeObject()) : std::nullopt;
    return value ? value->rotation : LLQuaternion::DEFAULT;
}
LLVector3 VSEditObject::getPositionEdit() const
{
    auto*      world = edit_world();
    const auto value = world && *this ? world->objectEditTransform(task().nativeObject()) : std::nullopt;
    return value ? value->position : LLVector3::zero;
}
LLQuaternion VSEditObject::getRotationEdit() const
{
    auto*      world = edit_world();
    const auto value = world && *this ? world->objectEditTransform(task().nativeObject()) : std::nullopt;
    return value ? value->rotation : LLQuaternion::DEFAULT;
}

LLVector3 VSEditObject::getPositionRegion() const
{
    auto*      world  = edit_world();
    const auto value  = world && *this
                            ? world->editorTransform(task().nativeObject(), -1, gAgent.getPosGlobalFromAgent(LLVector3::zero), false)
                            : std::nullopt;
    const auto region = vs_region_context(task().getHost());
    return value && region ? region->getPosRegionFromGlobal(value->globalPosition) : LLVector3::zero;
}
LLQuaternion VSEditObject::getRotationRegion() const
{
    auto*      world = edit_world();
    const auto value = world && *this
                           ? world->editorTransform(task().nativeObject(), -1, gAgent.getPosGlobalFromAgent(LLVector3::zero), false)
                           : std::nullopt;
    return value ? value->rotation : LLQuaternion::DEFAULT;
}
LLVector3d VSEditObject::getPositionGlobal() const
{
    auto*      world = edit_world();
    const auto value =
        world && *this ? world->editorTransform(task().nativeObject(), -1, gAgent.getPosGlobalFromAgent(LLVector3::zero)) : std::nullopt;
    return value ? value->globalPosition : LLVector3d(getPosition());
}
bool VSEditObject::setPosition(const LLVector3& position, bool)
{
    auto* world = edit_world();
    auto  value = world && *this ? world->objectLocalTransform(task().nativeObject()) : std::nullopt;
    if (!value)
        return false;
    value->position = position;
    return world->previewLocalTransform(task().nativeObject(), *value, UPD_POSITION);
}
bool VSEditObject::setRotation(const LLQuaternion& rotation, bool)
{
    auto* world = edit_world();
    auto  value = world && *this ? world->objectLocalTransform(task().nativeObject()) : std::nullopt;
    if (!value)
        return false;
    value->rotation = rotation;
    return world->previewLocalTransform(task().nativeObject(), *value, UPD_ROTATION);
}
bool VSEditObject::setPositionEdit(const LLVector3& position, bool damped)
{
    auto* world = edit_world();
    if (!world || !*this)
        return false;
    // Same original parent-edit recursion; the accepted identity graph is bounded.
    std::vector<std::pair<VSWorldScene::ObjectHandle, LLVector3>> chain;
    VSEditObject*                                                 object = this;
    LLVector3                                                     target = position;
    for (U32 count = 0; count < 256; ++count)
    {
        if (!*object)
            return false;
        const auto handle = object->task().nativeObject();
        for (const auto& entry : chain)
            if (entry.first.id == handle.id)
                return false;
        chain.emplace_back(handle, target);
        if (object->isRootEdit())
            return object->setPosition(target, damped);
        auto* parent = object->getParent();
        if (!parent)
            return false;
        target -= object->getPosition() * parent->getRotation();
        object = parent;
    }
    return false;
}
std::shared_ptr<VSObjectSelection::SavedTransform> VSEditObject::transformState() const
{
    const auto owner     = VSNativeSession::active();
    const auto selection = owner ? owner->retainObjectSelection() : nullptr;
    if (!*this || !selection)
        return {};
    mTransform = selection->savedTransform(task().nativeObject());
    return mTransform;
}
bool VSEditObject::saveUnselectedChildrenPosition(std::vector<LLVector3>& positions)
{
    const auto owner     = VSNativeSession::active();
    auto*      selection = owner ? owner->existingObjectSelection() : nullptr;
    return selection && selection->saveUnselectedChildrenPosition(task().nativeObject(), positions);
}
bool VSEditObject::saveUnselectedChildrenRotation(std::vector<LLQuaternion>& rotations)
{
    const auto owner     = VSNativeSession::active();
    auto*      selection = owner ? owner->existingObjectSelection() : nullptr;
    return selection && selection->saveUnselectedChildrenRotation(task().nativeObject(), rotations);
}
bool VSEditObject::resetChildrenRotationAndPosition(const std::vector<LLQuaternion>& rotations, const std::vector<LLVector3>& positions)
{
    const auto owner     = VSNativeSession::active();
    auto*      selection = owner ? owner->existingObjectSelection() : nullptr;
    return selection && selection->resetChildrenRotationAndPosition(task().nativeObject(), rotations, positions);
}
bool VSEditObject::resetChildrenPosition(const LLVector3& offset, bool simplified, bool skipAvatarChild)
{
    const auto owner     = VSNativeSession::active();
    auto*      selection = owner ? owner->existingObjectSelection() : nullptr;
    return selection && selection->resetChildrenPosition(task().nativeObject(), offset, simplified, skipAvatarChild);
}

bool VSEditSelection::saveSelectedObjectTransform(EActionType)
{ return mSelection && mCurrent() && mSelection->saveSelectedObjectTransform(); }
bool VSEditSelection::adjustTexturesByScale(bool send, bool stretch)
{ return mSelection && mCurrent() && vs_adjust_selected_textures_by_scale(*mSelection, send, stretch); }


bool VSEditNode::isTESelected(S32 face) const
{
    const auto selection = mSelection.lock();
    return selection && selection->contains(source, face);
}
S32 VSEditSelection::getTECount() const
{
    S32 count = 0;
    if (!mCurrent()) return count;
    for (auto* node : mNodes)
        if (const auto* object = node->getObject())
            for (S32 face = 0; face < object->getNumTEs(); ++face)
                if (node->isTESelected(face)) ++count;
    return count;
}
ESelectType VSEditSelection::getSelectType() const
{
    const auto type = mSelection ? mSelection->selectionType().type : std::nullopt;
    if (type == VSObjectSelection::SelectionType::HUD) return SELECT_TYPE_HUD;
    if (type == VSObjectSelection::SelectionType::Attachment) return SELECT_TYPE_ATTACHMENT;
    return SELECT_TYPE_WORLD;
}
F32 VSEditSelection::getSelectedLinksetCost()
{
    F32 cost = 0.f;
    if (!mCurrent()) return cost;
    // A root peer can be reached from several selected children. The simulator
    // cost belongs to that real source object, not to a temporary UI wrapper.
    std::set<std::tuple<LLUUID,U32,U32,U32,U64,U64>> roots;
    for (auto* node : mNodes)
        if (auto* object = node->getObject(); object && !object->isAttachment())
            if (auto* root = object->getRoot())
            {
                const auto& token = root->task().nativeObject();
                if (roots.emplace(token.id,token.host.getAddress(),token.host.getPort(),
                                  token.local,token.region,token.generation).second)
                    cost += root->getLinksetCost();
            }
    return cost;
}


bool VSEditSelection::objectsCrossParcel() const
{
    // Same cumulative root/direct-child box as LLCrossParcelFunctor, with the
    // original applyToRootObjects(firstonly=true) empty-selection result.
    if (!mCurrent()) return false;
    LLBBox bounding;
    for (const auto* node : mRoots)
    {
        auto* object = node->getObject();
        if (!object) continue;
        bounding.addBBoxAgent(LLBBox(object->getPositionRegion(), object->getRotationRegion(),
            object->getScale() * -0.5f, object->getScale() * 0.5f).getAxisAligned());
        for (const auto* child : object->getChildren())
        {
            bounding.addBBoxAgent(LLBBox(child->getPositionRegion(), child->getRotationRegion(),
                child->getScale() * -0.5f, child->getScale() * 0.5f).getAxisAligned());
        }
        const auto region = vs_region_context(object->task().getHost());
        if (region && region->objectsCrossParcel({bounding})) return true;
    }
    return false;
}

// Same selected-face and whole-object media policy as LLSelectMgr. The peer
// retains the source selection while the shared ObjectMedia service owns PUT.
bool VSEditSelection::selectionSetMedia(U8 mediaFlags,const LLSD& mediaData)
{
    if(!mSelection || !mCurrent())return false;
    for(auto* node:mNodes)
    {
        const auto* object=node->getObject();
        if(object && object->permModify() && !object->mediaDataReady())return false;
    }
    for(auto* node:mNodes)
    {
        if(!mCurrent())return false;
        auto* object=node->getObject();if(!object || !object->permModify())continue;
        for(S32 side=0;side<object->getNumTEs();++side)
        {
            if(!mCurrent() || !*object)return false;
            if(!node->isTESelected(side))continue;
            const auto* entry=object->getTE(side);if(!entry)return false;
            if(mediaFlags&LLTextureEntry::MF_HAS_MEDIA)
            {
                if(!mediaData.isMap() || (!entry->hasMedia() && !mediaData.has(LLMediaEntry::HOME_URL_KEY)))continue;
                if(!object->clearTEWaterExclusion(U8(side)))return false;
                object->setTEMediaFlags(U8(side),mediaFlags);
                entry=object->getTE(side);
                if(!mCurrent() || !entry || entry->getMediaFlags()!=mediaFlags ||
                    !object->syncMediaData(U8(side),mediaData,true,true))return false;
            }
            else
            {
                object->setTEMediaFlags(U8(side),mediaFlags);
                entry=object->getTE(side);
                if(!mCurrent() || !entry || entry->getMediaFlags()!=mediaFlags)return false;
            }
        }
    }
    for(auto* node:mNodes)
    {
        if(!mCurrent())return false;
        auto* object=node->getObject();if(!object || !object->permModify())continue;
        object->sendTEUpdate();
        if(!mCurrent() || !*object)return false;
        bool hasMedia=false;
        for(S32 side=0;side<object->getNumTEs();++side)
        {
            const auto* entry=object->getTE(side);hasMedia=hasMedia || (entry && entry->hasMedia());
        }
        if(hasMedia && !object->sendMediaDataUpdate())return false;
    }
    return mCurrent();
}

void VSEditSelection::sendGodlikeRequest(const std::string& request,const std::string& parameter)
{
    if(!mSelection || !mCurrent() || !gMessageSystem)return;
    const char* type=gAgent.isGodlike()?"GodlikeMessage":"EstateOwnerMessage";
    const auto header=[&](LLMessageSystem& message)
    {
        message.addUUID("TransactionID",LLUUID::null);
        message.nextBlock("MethodData");message.addString("Method",request);message.addUUID("Invoice",LLUUID::null);
        if(!parameter.empty()){message.nextBlock("ParamList");message.addString("Parameter",parameter);}
    };
    if(mRoots.empty())
    {
        LLMessageSystem* message=gMessageSystem;
        message->newMessage(type);buildAgent(*message);header(*message);
        if(mCurrent())gAgent.sendReliableMessage();else message->clearMessage();
        return;
    }
    send(type,true,header,[](LLMessageSystem& message,const VSEditNode& node)
    {
        message.nextBlock("ParamList");message.addString("Parameter",llformat("%u",node.source.local));
    });
}
