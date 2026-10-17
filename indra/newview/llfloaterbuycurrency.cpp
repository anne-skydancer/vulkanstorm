/**
 * @file llfloaterbuycurrency.cpp
 * @brief LLFloaterBuyCurrency class implementation
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
#include "vsnativeim.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#endif

#include "llfloaterbuycurrency.h"

// viewer includes
#include "llcurrencyuimanager.h"
#include "llfloater.h"
#include "llfloaterreg.h"
#include "lllayoutstack.h"
#include "lliconctrl.h"
#include "llnotificationsutil.h"
#include "llstatusbar.h"
#include "lltextbox.h"
#include "llviewchildren.h"
#include "llviewerwindow.h"
#include "lluictrlfactory.h"
#include "llweb.h"
#include "llwindow.h"
#include "llappviewer.h"
#ifdef OPENSIM
#include "fsgridhandler.h" // <FS:Beq> FIRE-30481 open sim buy floater fixup
#endif

static const S32 MINIMUM_BALANCE_AMOUNT = 0;

class LLFloaterBuyCurrencyUI
:   public LLFloater
{
public:
    LLFloaterBuyCurrencyUI(const LLSD& key);
    virtual ~LLFloaterBuyCurrencyUI();


public:
    LLViewChildren      mChildren;
    LLCurrencyUIManager mManager;

    bool        mHasTarget;
    S32         mTargetPrice;
    S32         mRequiredAmount = 0;
    bool mNativeBalanceKnown = false;
    S32 mLastNativeBalance = 0;
    std::function<bool()> mCurrent = vs_native_im_guard();

public:
    void noTarget();
    void target(const std::string& name, S32 price);

    virtual bool postBuild();

    void updateUI();
    void collapsePanels(bool collapse);

    virtual void draw();
    virtual bool canClose();

    void onClickBuy();
    void onClickCancel();
};

bool LLFloaterBuyCurrency::isNativeInternalType(const std::type_info& type)
{
    return type == typeid(LLFloaterBuyCurrencyUI);
}

LLFloater* LLFloaterBuyCurrency::buildFloater(const LLSD& key)
{
    LLFloaterBuyCurrencyUI* floater = new LLFloaterBuyCurrencyUI(key);
    return floater;
}

#if LL_WINDOWS
// passing 'this' during construction generates a warning. The callee
// only uses the pointer to hold a reference to 'this' which is
// already valid, so this call does the correct thing. Disable the
// warning so that we can compile without generating a warning.
#pragma warning(disable : 4355)
#endif
LLFloaterBuyCurrencyUI::LLFloaterBuyCurrencyUI(const LLSD& key)
:   LLFloater(key),
    mChildren(*this),
    mManager(*this),
    mHasTarget(false),
    mTargetPrice(0)
{
}

LLFloaterBuyCurrencyUI::~LLFloaterBuyCurrencyUI()
{
}


void LLFloaterBuyCurrencyUI::noTarget()
{
    mHasTarget = false;
    mTargetPrice = 0;
    mManager.setAmount(0);
}

void LLFloaterBuyCurrencyUI::target(const std::string& name, S32 price)
{
    mHasTarget = true;
    mTargetPrice = price;

    if (!name.empty())
    {
        getChild<LLUICtrl>("target_price_label")->setValue(name);
    }

    mRequiredAmount = 0;
    mManager.setAmount(0);
    S32 balance = gStatusBar ? gStatusBar->getBalance() : 0;
#if VS_NATIVE_VULKAN
    if (auto owner = VSNativeSession::active())
    {
        if (!owner->balanceKnown()) return;
        balance = owner->balance();
    }
#endif
    S32 need = price - balance;
    if (need < 0)
    {
        need = 0;
    }

    mRequiredAmount = need + MINIMUM_BALANCE_AMOUNT;
}


// virtual
bool LLFloaterBuyCurrencyUI::postBuild()
{
    mManager.prepare();

    getChild<LLUICtrl>("buy_btn")->setCommitCallback( boost::bind(&LLFloaterBuyCurrencyUI::onClickBuy, this));
    getChild<LLUICtrl>("cancel_btn")->setCommitCallback( boost::bind(&LLFloaterBuyCurrencyUI::onClickCancel, this));

#ifdef OPENSIM
    if (LLGridManager::instance().isInOpenSim())
    {
        getChild<LLTextBox>("currency_links")->setText(LLStringExplicit(""));
    }
#endif

    center();

    updateUI();

    return true;
}

void LLFloaterBuyCurrencyUI::draw()
{
    if (!mCurrent()) return;
#if VS_NATIVE_VULKAN
    if (auto owner = VSNativeSession::active(); owner && owner->balanceKnown() &&
        (!mNativeBalanceKnown || mLastNativeBalance != owner->balance()))
    {
        mNativeBalanceKnown = true;
        mLastNativeBalance = owner->balance();
        if (mHasTarget) mRequiredAmount = llmax(0, mTargetPrice - mLastNativeBalance) + MINIMUM_BALANCE_AMOUNT;
        updateUI();
    }
#endif
    if (mManager.process())
    {
        if (mManager.bought())
        {
            LLNotificationsUtil::add("BuyLindenDollarSuccess");
            closeFloater();
            return;
        }

        updateUI();
    }

    // disable the Buy button when we are not able to buy
    getChildView("buy_btn")->setEnabled(mManager.canBuy());

    LLFloater::draw();
}

bool LLFloaterBuyCurrencyUI::canClose()
{
    return mManager.canCancel();
}

void LLFloaterBuyCurrencyUI::updateUI()
{
    bool hasError = mManager.hasError();
    mManager.updateUI(!hasError && !mManager.buying());

    // hide most widgets - we'll turn them on as needed next
    getChildView("info_buying")->setVisible(false);
    getChildView("info_need_more")->setVisible(false);
    getChildView("purchase_warning_repurchase")->setVisible(false);
    getChildView("purchase_warning_notenough")->setVisible(false);
    getChildView("contacting")->setVisible(false);

    if (hasError)
    {
        // display an error from the server
        LLSD args;
        args["TITLE"] = getString("info_cannot_buy");
        args["MESSAGE"] = mManager.errorMessage();
// <FS:Beq> FIRE-30481 restore access to help/donate link for OpenSim
#ifdef OPENSIM
        if( !LLGridManager::getInstance()->isInSecondLife() )
        {
            args["LINK"] = mManager.errorURI();
            LLNotificationsUtil::add("CouldNotBuyCurrencyOS", args);
        }
        else // trailing else required for OS builds
#endif
// </FS:Beq>
        LLNotificationsUtil::add("CouldNotBuyCurrency", args);
        mManager.clearError();
        closeFloater();
    }
    else
    {
        // display the main Buy L$ interface
        getChildView("normal_background")->setVisible(true);

        if (mHasTarget)
        {
            getChildView("info_need_more")->setVisible(true);
        }
        else
        {
            getChildView("info_buying")->setVisible(true);
        }

        if (mManager.buying())
        {
            getChildView("contacting")->setVisible( true);
        }
        else
        {
            if (mHasTarget)
            {
                getChild<LLUICtrl>("target_price")->setTextArg("[AMT]", llformat("%d", mTargetPrice));
                getChild<LLUICtrl>("required_amount")->setTextArg("[AMT]", llformat("%d", mRequiredAmount));
#if VS_NATIVE_VULKAN
                if (auto owner = VSNativeSession::active(); owner && !owner->balanceKnown())
                    getChild<LLUICtrl>("required_amount")->setTextArg("[AMT]", "--");
#endif
            }
        }

        S32 balance = gStatusBar ? gStatusBar->getBalance() : 0;
        bool balance_known = true;
#if VS_NATIVE_VULKAN
        if (auto owner = VSNativeSession::active())
        {
            balance_known = owner->balanceKnown();
            if (balance_known) balance = owner->balance();
        }
#endif
        getChildView("balance_label")->setVisible(true);
        getChildView("balance_amount")->setVisible(true);
        getChild<LLUICtrl>("balance_amount")->setTextArg("[AMT]", balance_known ? llformat("%d", balance) : "--");

        S32 buying = mManager.getAmount();
        getChildView("buying_label")->setVisible(true);
        getChildView("buying_amount")->setVisible(true);
        getChild<LLUICtrl>("buying_amount")->setTextArg("[AMT]", llformat("%d", buying));

        S32 total = balance + buying;
        getChildView("total_label")->setVisible(true);
        getChildView("total_amount")->setVisible(true);
        getChild<LLUICtrl>("total_amount")->setTextArg("[AMT]", balance_known ? llformat("%d", total) : "--");

        if (mHasTarget)
        {
            getChildView("purchase_warning_repurchase")->setVisible( !getChildView("currency_links")->getVisible());
        }
    }

    getChildView("getting_data")->setVisible( !mManager.canBuy() && !hasError && !getChildView("currency_est")->getVisible());
}

void LLFloaterBuyCurrencyUI::collapsePanels(bool collapse)
{
    LLLayoutPanel* price_panel = getChild<LLLayoutPanel>("layout_panel_price");

    if (price_panel->isCollapsed() == collapse)
        return;

    LLLayoutStack* outer_stack = getChild<LLLayoutStack>("outer_stack");
    LLLayoutPanel* required_panel = getChild<LLLayoutPanel>("layout_panel_required");
    LLLayoutPanel* msg_panel = getChild<LLLayoutPanel>("layout_panel_msg");

    S32 delta_height = price_panel->getRect().getHeight() + required_panel->getRect().getHeight() + msg_panel->getRect().getHeight();
    delta_height *= (collapse ? -1 : 1);

    LLIconCtrl* icon = getChild<LLIconCtrl>("normal_background");
    LLRect rect = icon->getRect();
    icon->setRect(rect.setOriginAndSize(rect.mLeft, rect.mBottom - delta_height, rect.getWidth(), rect.getHeight() + delta_height));

    outer_stack->collapsePanel(price_panel, collapse);
    outer_stack->collapsePanel(required_panel, collapse);
    outer_stack->collapsePanel(msg_panel, collapse);

    outer_stack->updateLayout();

    LLRect floater_rect = getRect();
    floater_rect.mBottom -= delta_height;
    setShape(floater_rect, false);
}

void LLFloaterBuyCurrencyUI::onClickBuy()
{
    // <COLOSI opensim multi-currency support>
    // Do not wrap the "buy_currency" string because it is the mOrig later stored
    // in an LLUIString with params and will be wrapped when params are inserted.
    // wrapping here would break multi-currency support as it would remove the L$
    //</COLOSI opensim multi-currency support>>
    mManager.buy(getString("buy_currency"));
    updateUI();
    // Update L$ balance
    LLStatusBar::sendMoneyBalanceRequest();
}

void LLFloaterBuyCurrencyUI::onClickCancel()
{
    closeFloater();
    // Update L$ balance
    LLStatusBar::sendMoneyBalanceRequest();
}

LLFetchAvatarPaymentInfo* LLFloaterBuyCurrency::sPropertiesRequest = NULL;

void LLFloaterBuyCurrency::resetAccountRequests()
{
    delete sPropertiesRequest;
    sPropertiesRequest = nullptr;
}

// static
void LLFloaterBuyCurrency::buyCurrency()
{
    if (!vs_native_im_guard()()) return;
    // <FS:Ansariel> OpenSim support
    //delete sPropertiesRequest;
    //sPropertiesRequest = new LLFetchAvatarPaymentInfo(false);
#if OPENSIM
    if (LLGridManager::instance().isInOpenSim())
    {
        LLFloaterBuyCurrencyUI* ui = LLFloaterReg::showTypedInstance<LLFloaterBuyCurrencyUI>("buy_currency");
        if (!ui) return;
        ui->noTarget();
        ui->updateUI();
    }
    else
#endif
    {
        delete sPropertiesRequest;
        sPropertiesRequest = new LLFetchAvatarPaymentInfo(false);
    }
    // <FS:Ansariel>
}

// static
void LLFloaterBuyCurrency::buyCurrency(const std::string& name, S32 price)
{
    if (!vs_native_im_guard()()) return;
    // <FS:Ansariel> OpenSim support
    //delete sPropertiesRequest;
    //sPropertiesRequest = new LLFetchAvatarPaymentInfo(true, name, price);
#if OPENSIM
    if (LLGridManager::instance().isInOpenSim())
    {
        LLFloaterBuyCurrencyUI* ui = LLFloaterReg::showTypedInstance<LLFloaterBuyCurrencyUI>("buy_currency");
        if (!ui) return;
        // <COLOSI opensim multi-currency support>
        // Not wrapping name here because according to llfloaterbuycurrency.h
        // name should not include currency symbols as the price will be appended to the string.
        // If an "L$" is ever included in a name, then we should call Tea::wrapCurrency on it here.
        // </COLOSI opensim multi-currency support>>
        ui->target(name, price);
        ui->updateUI();
    }
    else
#endif
    {
        delete sPropertiesRequest;
        sPropertiesRequest = new LLFetchAvatarPaymentInfo(true, name, price);
    }
    // <FS:Ansariel>
}

// static
void LLFloaterBuyCurrency::handleBuyCurrency(bool has_piof, bool has_target, const std::string name, S32 price)
{
    delete sPropertiesRequest;
    sPropertiesRequest = NULL;

    if (has_piof)
    {
        LLFloaterBuyCurrencyUI* ui = LLFloaterReg::showTypedInstance<LLFloaterBuyCurrencyUI>("buy_currency");
        if (!ui) return;
        if (has_target)
        {
            ui->target(name, price);
        }
        else
        {
            ui->noTarget();
        }
        ui->updateUI();
        ui->collapsePanels(!has_target);
    }
    else
    {
        LLFloaterReg::showInstance("add_payment_method");
    }
}

LLFetchAvatarPaymentInfo::LLFetchAvatarPaymentInfo(bool has_target, const std::string& name, S32 price)
:   mCurrent(vs_native_im_guard()),
    mAvatarID(gAgent.getID()),
    mHasTarget(has_target),
    mPrice(price),
    mName(name)
{
    LLAvatarPropertiesProcessor* processor = LLAvatarPropertiesProcessor::getInstance();
    // register ourselves as an observer
    processor->addObserver(mAvatarID, this);
    // send a request (duplicates will be suppressed inside the avatar
    // properties processor)
    // <FS> OpenSim
    //processor->sendAvatarPropertiesRequest(mAvatarID);
    if (!gAgent.getRegionCapability("AgentProfile").empty())
    processor->sendAvatarPropertiesRequest(mAvatarID);
    else
        processor->sendAvatarLegacyPropertiesRequest(mAvatarID);
    // </FS>
}

LLFetchAvatarPaymentInfo::~LLFetchAvatarPaymentInfo()
{
    LLAvatarPropertiesProcessor::getInstance()->removeObserver(mAvatarID, this);
}

void LLFetchAvatarPaymentInfo::processProperties(void* data, EAvatarProcessorType type)
{
    if (!mCurrent() || LLFloaterBuyCurrency::sPropertiesRequest != this) return;
    if (data && type == APT_PROPERTIES)
    {
        LLAvatarData* avatar_data = static_cast<LLAvatarData*>(data);
        if (avatar_data->avatar_id != mAvatarID) return;
        LLFloaterBuyCurrency::handleBuyCurrency(LLAvatarPropertiesProcessor::hasPaymentInfoOnFile(avatar_data), mHasTarget, mName, mPrice);
    }
    else if (data && type == APT_PROPERTIES_LEGACY)
    {
        LLAvatarData avatar_data(*static_cast<LLAvatarLegacyData*>(data));
        if (avatar_data.avatar_id != mAvatarID) return;
        LLFloaterBuyCurrency::handleBuyCurrency(LLAvatarPropertiesProcessor::hasPaymentInfoOnFile(&avatar_data), mHasTarget, mName, mPrice);
    }
}

// <COLOSI opensim multi-currency support>
// static
void LLFloaterBuyCurrency::updateCurrencySymbols()
{
    LLFloater* fbc = LLFloaterReg::findInstance("buy_currency");
    if (fbc)
    {
        // Call update on the floater to update the title
        fbc->updateCurrencySymbols();

        // update all text boxes with currency symbols.
        LLTextBox* tb = NULL;
        static const std::list<std::string> sctb = { "info_need_more", "info_buying", "target_price", "balance_amount",
            "required_amount", "currency_label", "total_amount", "purchase_warning_repurchase" };
        // Do not include balance_amount and total_amount because they are updated on every display when amounts are replaced.
        for (std::list<std::string>::const_iterator iter = sctb.begin(); iter != sctb.end(); ++iter)
        {
            tb = fbc->findChild<LLTextBox>(*iter);
            if (tb)
            {
                tb->updateCurrencySymbols();
            }
        }
    }
}
// </COLOSI opensim multi-currency support>

