/**
 * @file llfloaterobjectweights.cpp
 * @brief Object weights advanced view floater
 *
 * $LicenseInfo:firstyear=2011&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */
#include "llviewerprecompiledheaders.h"
#include "vsregioncontext.h"
#include "llviewerwindow.h"
#if VS_NATIVE_VULKAN
#include "vseditselection.h"
#include "vsnativeim.h"
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#endif

#include "llfloaterobjectweights.h"

#include "llparcel.h"

#include "llfloaterreg.h"
#include "lltextbox.h"

#include "llagent.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"

static const std::string lod_strings[4] =
{
    "lowest_lod",
    "low_lod",
    "medium_lod",
    "high_lod",
};

// virtual
bool LLCrossParcelFunctor::apply(LLViewerObject* obj)
{
    // Add the root object box.
    mBoundingBox.addBBoxAgent(LLBBox(obj->getPositionRegion(), obj->getRotationRegion(), obj->getScale() * -0.5f, obj->getScale() * 0.5f).getAxisAligned());

    // Extend the bounding box across all the children.
    LLViewerObject::const_child_list_t children = obj->getChildren();
    for (LLViewerObject::const_child_list_t::const_iterator iter = children.begin();
         iter != children.end(); iter++)
    {
        LLViewerObject* child = *iter;
        mBoundingBox.addBBoxAgent(LLBBox(child->getPositionRegion(), child->getRotationRegion(), child->getScale() * -0.5f, child->getScale() * 0.5f).getAxisAligned());
    }

    bool result = false;

    LLViewerRegion* region = obj->getRegion();
    if (region)
    {
        std::vector<LLBBox> boxes;
        boxes.push_back(mBoundingBox);
        result = region->objectsCrossParcel(boxes);
    }

    return result;
}

LLFloaterObjectWeights::LLFloaterObjectWeights(const LLSD& key)
:   LLFloater(key),
    mSelectedObjects(NULL),
    mSelectedPrims(NULL),
    mSelectedDownloadWeight(NULL),
    mSelectedPhysicsWeight(NULL),
    mSelectedServerWeight(NULL),
    mSelectedDisplayWeight(NULL),
    mSelectedOnLand(NULL),
    mRezzedOnLand(NULL),
    mRemainingCapacity(NULL),
    mTotalCapacity(NULL),
    mLodLevel(nullptr),
    mTrianglesShown(nullptr),
    mPixelArea(nullptr)
{
}

LLFloaterObjectWeights::~LLFloaterObjectWeights()
{
}

// virtual
bool LLFloaterObjectWeights::postBuild()
{
    mSelectedObjects = getChild<LLTextBox>("objects");
    mSelectedPrims = getChild<LLTextBox>("prims");

    mSelectedDownloadWeight = getChild<LLTextBox>("download");
    mSelectedPhysicsWeight = getChild<LLTextBox>("physics");
    mSelectedServerWeight = getChild<LLTextBox>("server");
    mSelectedDisplayWeight = getChild<LLTextBox>("display");

    mSelectedOnLand = getChild<LLTextBox>("selected");
    mRezzedOnLand = getChild<LLTextBox>("rezzed_on_land");
    mRemainingCapacity = getChild<LLTextBox>("remaining_capacity");
    mTotalCapacity = getChild<LLTextBox>("total_capacity");

    mLodLevel = getChild<LLTextBox>("lod_level");
    mTrianglesShown = getChild<LLTextBox>("triangles_shown");
    mPixelArea = getChild<LLTextBox>("pixel_area");

    return true;
}

// virtual
void LLFloaterObjectWeights::onOpen(const LLSD& key)
{
#if VS_NATIVE_VULKAN
    if (const auto owner = VSNativeSession::active())
    {
        mAccountCurrent = vs_native_im_guard();
        const auto weak = getHandle();
        const auto current = mAccountCurrent;
        mSelectionChanged = owner->objectSelection()->changed([weak, current]
        {
            auto* panel = dynamic_cast<LLFloaterObjectWeights*>(weak.get());
            if (current() && panel) panel->refresh();
        });
    }
#endif
    refresh();
    updateLandImpacts(LLViewerParcelMgr::getInstance()->getFloatingParcelSelection()->getParcel());
}

// virtual
void LLFloaterObjectWeights::onWeightsUpdate(const SelectionCost& selection_cost)
{
    if (!mAccountCurrent() || !mSelectionCurrent()) return;
    mSelectedDownloadWeight->setText(llformat("%.1f", selection_cost.mNetworkCost));
    mSelectedPhysicsWeight->setText(llformat("%.1f", selection_cost.mPhysicsCost));
    mSelectedServerWeight->setText(llformat("%.1f", selection_cost.mSimulationCost));

    S32 render_cost = LLSelectMgr::getInstance()->getSelection()->getSelectedObjectRenderCost();
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active() && gViewerWindow && gViewerWindow->nativeContext())
    {
        F32 cost = 0;
        VSEditSelection selection;
        std::set<LLUUID> computed;
        auto& world = gViewerWindow->nativeContext()->world();
        for (const auto* node : selection)
            if (const auto* object = node->getObject(); object && object->isRootEdit())
            {
                cost += world.objectLinksetRenderCost(object->task().nativeObject()).value_or(0);
                computed.insert(object->getID());
                for (const auto* child : object->getChildren()) computed.insert(child->getID());
            }
        for (const auto* node : selection)
            if (const auto* object = node->getObject(); object && computed.insert(object->getID()).second)
            {
                const auto metrics = world.objectMetrics(object->task().nativeObject());
                if (metrics && metrics->renderCost) cost += F32(*metrics->renderCost);
            }
        render_cost = S32(cost);
    }
#endif
    mSelectedDisplayWeight->setText(llformat("%d", render_cost));

    toggleWeightsLoadingIndicators(false);
}

//virtual
void LLFloaterObjectWeights::setErrorStatus(S32 status, const std::string& reason)
{
    if (!mAccountCurrent() || !mSelectionCurrent()) return;
    const std::string text = getString("nothing_selected");

    mSelectedDownloadWeight->setText(text);
    mSelectedPhysicsWeight->setText(text);
    mSelectedServerWeight->setText(text);
    mSelectedDisplayWeight->setText(text);

    toggleWeightsLoadingIndicators(false);
}

void LLFloaterObjectWeights::draw()
{
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active()) { drawNative(); LLFloater::draw(); return; }
#endif

    // Normally it's a bad idea to set text and visibility inside draw
    // since it can cause rect updates go to different, already drawn elements,
    // but floater is very simple and these elements are supposed to be isolated
    LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
    if (selection->isEmpty())
    {
        const std::string text = getString("nothing_selected");
        mLodLevel->setText(text);
        mTrianglesShown->setText(text);
        mPixelArea->setText(text);

        toggleRenderLoadingIndicators(false);
    }
    else
    {
        S32 object_lod = -1;
        bool multiple_lods = false;
        S32 total_tris = 0;
        F32 pixel_area = 0;
        for (LLObjectSelection::valid_root_iterator iter = selection->valid_root_begin();
            iter != selection->valid_root_end(); ++iter)
        {
            LLViewerObject* object = (*iter)->getObject();
            S32 lod = object->getLOD();
            if (object_lod < 0)
            {
                object_lod = lod;
            }
            else if (object_lod != lod)
            {
                multiple_lods = true;
            }

            if (object->isRootEdit())
            {
                total_tris += object->recursiveGetTriangleCount();
                pixel_area += object->getPixelArea();
            }
        }

        if (multiple_lods)
        {
            mLodLevel->setText(getString("multiple_lods"));
            toggleRenderLoadingIndicators(false);
        }
        else if (object_lod < 0)
        {
            // nodes are waiting for data
            toggleRenderLoadingIndicators(true);
        }
        else
        {
            mLodLevel->setText(getString(lod_strings[object_lod]));
            toggleRenderLoadingIndicators(false);
        }
        mTrianglesShown->setText(llformat("%d", total_tris));
        mPixelArea->setText(llformat("%ld", (S64)pixel_area)); // value capped at 10M
    }
    LLFloater::draw();
}

void LLFloaterObjectWeights::updateLandImpacts(const LLParcel* parcel)
{
    bool empty = LLSelectMgr::getInstance()->getSelection()->isEmpty();
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active()) empty = VSEditSelection().isEmpty();
#endif
    if (!mAccountCurrent() || !parcel || empty)
    {
        updateIfNothingSelected();
    }
    else
    {
        S32 rezzed_prims = parcel->getSimWidePrimCount();
        S32 total_capacity = parcel->getSimWideMaxPrimCapacity();
        // Can't have more than region max tasks, regardless of parcel
        // object bonus factor.
        const auto region=LLViewerParcelMgr::getInstance()->getSelectionRegionContext();
        if (region)
        {
            S32 max_tasks_per_region = (S32)region->getMaxTasks();
            total_capacity = llmin(total_capacity, max_tasks_per_region);
        }

        mRezzedOnLand->setText(llformat("%d", rezzed_prims));
        mRemainingCapacity->setText(llformat("%d", total_capacity - rezzed_prims));
        mTotalCapacity->setText(llformat("%d", total_capacity));

        toggleLandImpactsLoadingIndicators(false);
    }
}

void LLFloaterObjectWeights::refresh()
{
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active()) { refreshNative(); return; }
#endif

    LLSelectMgr* sel_mgr = LLSelectMgr::getInstance();

    if (sel_mgr->getSelection()->isEmpty())
    {
        updateIfNothingSelected();
    }
    else
    {
        S32 prim_count = sel_mgr->getSelection()->getObjectCount();
        S32 link_count = sel_mgr->getSelection()->getRootObjectCount();
        F32 prim_equiv = sel_mgr->getSelection()->getSelectedLinksetCost();

        mSelectedObjects->setText(llformat("%d", link_count));
        mSelectedPrims->setText(llformat("%d", prim_count));
        mSelectedOnLand->setText(llformat("%.1d", (S32)prim_equiv));

        LLCrossParcelFunctor func;
        if (sel_mgr->getSelection()->applyToRootObjects(&func, true))
        {
            // Some of the selected objects cross parcel bounds.
            // We don't display object weights and land impacts in this case.
            const std::string text = getString("nothing_selected");

            mRezzedOnLand->setText(text);
            mRemainingCapacity->setText(text);
            mTotalCapacity->setText(text);

            toggleLandImpactsLoadingIndicators(false);
        }

        LLViewerRegion* region = gAgent.getRegion();
        if (region && region->capabilitiesReceived())
        {
            for (LLObjectSelection::valid_root_iterator iter = sel_mgr->getSelection()->valid_root_begin();
                    iter != sel_mgr->getSelection()->valid_root_end(); ++iter)
            {
                LLAccountingCostManager::getInstance()->addObject((*iter)->getObject()->getID());
            }

            std::string url = region->getCapability("ResourceCostSelected");
            if (!url.empty())
            {
                // Update the transaction id before the new fetch request
                generateTransactionID();

                LLAccountingCostManager::getInstance()->fetchCosts(Roots, url, getObserverHandle());
                toggleWeightsLoadingIndicators(true);
            }
        }
        else
        {
            LL_WARNS() << "Failed to get region capabilities" << LL_ENDL;
        }
    }
}

// virtual
void LLFloaterObjectWeights::generateTransactionID()
{
    mTransactionID.generate();
}

void LLFloaterObjectWeights::toggleWeightsLoadingIndicators(bool visible)
{
    childSetVisible("download_loading_indicator", visible);
    childSetVisible("physics_loading_indicator", visible);
    childSetVisible("server_loading_indicator", visible);
    childSetVisible("display_loading_indicator", visible);

    mSelectedDownloadWeight->setVisible(!visible);
    mSelectedPhysicsWeight->setVisible(!visible);
    mSelectedServerWeight->setVisible(!visible);
    mSelectedDisplayWeight->setVisible(!visible);
}

void LLFloaterObjectWeights::toggleLandImpactsLoadingIndicators(bool visible)
{
    childSetVisible("selected_loading_indicator", visible);
    childSetVisible("rezzed_on_land_loading_indicator", visible);
    childSetVisible("remaining_capacity_loading_indicator", visible);
    childSetVisible("total_capacity_loading_indicator", visible);

    mSelectedOnLand->setVisible(!visible);
    mRezzedOnLand->setVisible(!visible);
    mRemainingCapacity->setVisible(!visible);
    mTotalCapacity->setVisible(!visible);
}

void LLFloaterObjectWeights::toggleRenderLoadingIndicators(bool visible)
{
    childSetVisible("lod_level_loading_indicator", visible);
    childSetVisible("triangles_shown_loading_indicator", visible);
    childSetVisible("pixel_area_loading_indicator", visible);

    mLodLevel->setVisible(!visible);
    mTrianglesShown->setVisible(!visible);
    mPixelArea->setVisible(!visible);
}

void LLFloaterObjectWeights::updateIfNothingSelected()
{
    const std::string text = getString("nothing_selected");

    mSelectedObjects->setText(text);
    mSelectedPrims->setText(text);

    mSelectedDownloadWeight->setText(text);
    mSelectedPhysicsWeight->setText(text);
    mSelectedServerWeight->setText(text);
    mSelectedDisplayWeight->setText(text);

    mSelectedOnLand->setText(text);
    mRezzedOnLand->setText(text);
    mRemainingCapacity->setText(text);
    mTotalCapacity->setText(text);

    mLodLevel->setText(text);
    mTrianglesShown->setText(text);
    mPixelArea->setText(text);

    toggleWeightsLoadingIndicators(false);
    toggleLandImpactsLoadingIndicators(false);
    toggleRenderLoadingIndicators(false);
}


void LLFloaterObjectWeights::refreshNative()
{
#if VS_NATIVE_VULKAN
    if (!mAccountCurrent()) return;
    VSEditSelection selection;
    mSelectionCurrent = selection.guard();
    if (selection.isEmpty()) { updateIfNothingSelected(); return; }
    mSelectedObjects->setText(llformat("%d", selection.getRootObjectCount()));
    mSelectedPrims->setText(llformat("%d", selection.getObjectCount()));
    mSelectedOnLand->setText(llformat("%.1d", S32(selection.getSelectedLinksetCost())));
    updateLandImpacts(LLViewerParcelMgr::getInstance()->getFloatingParcelSelection()->getParcel());
    if (selection.objectsCrossParcel())
    {
        const auto text = getString("nothing_selected");
        mRezzedOnLand->setText(text); mRemainingCapacity->setText(text); mTotalCapacity->setText(text);
        toggleLandImpactsLoadingIndicators(false);
    }
    const auto region = vs_current_region_context();
    const auto url = region ? region->getCapability("ResourceCostSelected") : std::string();
    if (url.empty()) { setErrorStatus(0, "ResourceCostSelected unavailable"); return; }
    uuid_set_t objects;
    for (auto iter = selection.root_begin(); iter != selection.root_end(); ++iter)
        if (const auto* object = (*iter)->getObject()) objects.insert(object->getID());
    generateTransactionID();
    const auto account = mAccountCurrent, selected = mSelectionCurrent;
    const auto guard = [account, selected, region] { return account() && selected() && region && region->current(); };
    toggleWeightsLoadingIndicators(true);
    LLAccountingCostManager::instance().fetchSelectionCosts(Roots, url, getObserverHandle(), objects, guard);
#endif
}

void LLFloaterObjectWeights::drawNative()
{
#if VS_NATIVE_VULKAN
    if (!mAccountCurrent() || !gViewerWindow || !gViewerWindow->nativeContext()) return;
    VSEditSelection selection;
    if (selection.isEmpty()) { updateIfNothingSelected(); return; }
    S32 object_lod = -1; bool multiple_lods = false, ready = true;
    U64 triangles = 0; F32 pixel_area = 0;
    auto& world = gViewerWindow->nativeContext()->world();
    for (auto iter = selection.root_begin(); iter != selection.root_end(); ++iter)
    {
        const auto* object = (*iter)->getObject(); if (!object) continue;
        const auto metrics = world.objectMetrics(object->task().nativeObject());
        if (!metrics || metrics->lod < 0) { ready = false; continue; }
        if (object_lod < 0) object_lod = metrics->lod;
        else if (object_lod != metrics->lod) multiple_lods = true;
        if (!object->isRootEdit()) continue;
        const auto count = [&](const VSEditObject& item) -> void
        {
            const auto row = world.objectMetrics(item.task().nativeObject());
            if (!row || !row->triangles) ready = false;
            else triangles += *row->triangles;
        };
        count(*object);
        for (const auto* child : object->getChildren()) count(*child);
        if (metrics->pixelArea) pixel_area += *metrics->pixelArea; else ready = false;
    }
    toggleRenderLoadingIndicators(!ready && !multiple_lods);
    if (multiple_lods) mLodLevel->setText(getString("multiple_lods"));
    else if (object_lod >= 0 && object_lod < 4) mLodLevel->setText(getString(lod_strings[object_lod]));
    mTrianglesShown->setText(llformat("%llu", static_cast<unsigned long long>(triangles)));
    mPixelArea->setText(llformat("%lld", static_cast<long long>(pixel_area)));
#endif
}
