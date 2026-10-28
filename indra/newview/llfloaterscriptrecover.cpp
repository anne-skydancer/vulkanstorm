/**
 *
 * Copyright (c) 2011-2012, Kitty Barnett
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 * By copying, modifying or distributing this software, you acknowledge that
 * you have read and understood your obligations described above, and agree to
 * abide by those obligations.
 *
 */

#include "llviewerprecompiledheaders.h"

#include "llagent.h"
#include "llcheckboxctrl.h"
#include "lldiriterator.h"
#include "llfloaterreg.h"
#include "llfolderview.h"
#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "llinventorypanel.h"
#include "llscrolllistctrl.h"
#include "lltrans.h"
#include "llviewerassettype.h"
#include "llviewerinventory.h"
#include "llviewerregion.h"

#include "llfloaterscriptrecover.h"

#include "llviewerassetupload.h"
#include "vsnativeim.h"
#include <exception>
#include <set>

namespace
{
std::set<std::shared_ptr<LLScriptRecoverQueue>> sRecoverQueues;
std::function<bool()> recoveryAccount()
{
    const auto current = vs_native_im_guard();
    const LLUUID agent = gAgentID, session = gAgentSessionID;
    return [current, agent, session]()
    { return current() && agent.notNull() && agent == gAgentID && session == gAgentSessionID; };
}
}
const std::string NEW_LSL_NAME = "New Script";

std::string fixNewScriptDefaultName(const std::string& scriptName)
{
    std::string fixedScriptName = scriptName;
    if (scriptName == NEW_LSL_NAME)
    {
        LLStringUtil::replaceChar(fixedScriptName, ' ', '_');
    }
    return fixedScriptName;
}

// ============================================================================
// LLFloaterScriptRecover
//

LLFloaterScriptRecover::LLFloaterScriptRecover(const LLSD& sdKey)
    : LLFloater(sdKey)
{
}

void LLFloaterScriptRecover::onOpen(const LLSD& sdKey)
{
    mAccountCurrent = recoveryAccount();
    LLScrollListCtrl* pListCtrl = findChild<LLScrollListCtrl>("script_list");

    LLSD sdBhvrRow; LLSD& sdBhvrColumns = sdBhvrRow["columns"];
    sdBhvrColumns[0] = LLSD().with("column", "script_check").with("type", "checkbox");
    sdBhvrColumns[1] = LLSD().with("column", "script_name").with("type", "text");

    pListCtrl->clearRows();
    for (LLSD::array_const_iterator itFile = sdKey["files"].beginArray(), endFile = sdKey["files"].endArray();
            itFile != endFile;  ++itFile)
    {
        const LLSD& sdFile = *itFile;

        sdBhvrRow["value"] = sdFile;
        sdBhvrColumns[0]["value"] = true;
        sdBhvrColumns[1]["value"] = sdFile["name"];

        pListCtrl->addElement(sdBhvrRow, ADD_BOTTOM);
    }
}

bool LLFloaterScriptRecover::postBuild()
{
    findChild<LLUICtrl>("recover_btn")->setCommitCallback(boost::bind(&LLFloaterScriptRecover::onBtnRecover, this));
    findChild<LLUICtrl>("cancel_btn")->setCommitCallback(boost::bind(&LLFloaterScriptRecover::onBtnCancel, this));

    return true;
}

void LLFloaterScriptRecover::onBtnCancel()
{
    if (!mAccountCurrent || !mAccountCurrent()) return;
    LLScrollListCtrl* pListCtrl = findChild<LLScrollListCtrl>("script_list");

    // Delete all listed files
    std::vector<LLScrollListItem*> items = pListCtrl->getAllData();
    for (std::vector<LLScrollListItem*>::const_iterator itItem = items.begin(); itItem != items.end(); ++itItem)
    {
        LLFile::remove((*itItem)->getValue()["path"].asString());
    }

    closeFloater();
}

void LLFloaterScriptRecover::onBtnRecover()
{
    if (!mAccountCurrent || !mAccountCurrent()) return;
    LLScrollListCtrl* pListCtrl = findChild<LLScrollListCtrl>("script_list");

    // Recover all selected, delete any unselected
    std::vector<LLScrollListItem*> items = pListCtrl->getAllData(); LLSD sdFiles;
    for (std::vector<LLScrollListItem*>::const_iterator itItem = items.begin(); itItem != items.end(); ++itItem)
    {
        LLScrollListCheck* pCheckColumn = dynamic_cast<LLScrollListCheck*>((*itItem)->getColumn(0));
        if (!pCheckColumn)
            continue;

        const LLSD sdFile = (*itItem)->getValue();
        if (pCheckColumn->getCheckBox()->getValue().asBoolean())
            sdFiles.append(sdFile);
        else
            LLFile::remove(sdFile["path"]);
    }

    if (!sdFiles.emptyArray())
        LLScriptRecoverQueue::start(sdFiles);

    closeFloater();
}

// ============================================================================
// LLCreateRecoverScriptCallback
//

class LLCreateRecoverScriptCallback : public LLInventoryCallback
{
public:
    LLCreateRecoverScriptCallback(std::weak_ptr<LLScriptRecoverQueue> queue, std::string path)
        : mRecoverQueue(std::move(queue)), mPath(std::move(path)) {}
    void fire(const LLUUID& item) override
    {
        if (auto queue = mRecoverQueue.lock()) queue->onCreateScript(item, mPath);
    }
private:
    std::weak_ptr<LLScriptRecoverQueue> mRecoverQueue;
    std::string mPath;
};

// ============================================================================
// LLScriptRecoverQueue
//

// static
void LLScriptRecoverQueue::recoverIfNeeded()
{
    resetAccount();
    const std::string strTempPath = LLFile::tmpdir();
    LLSD sdData, &sdFiles = sdData["files"];

    LLDirIterator itFiles(strTempPath, "*.l??backup"); std::string strFilename;
    while (itFiles.next(strFilename))
    {
        const std::string extension = gDirUtilp->getExtension(strFilename);
        if (extension != "lslbackup" && extension != "luabackup") continue;
        // Build a friendly name for the file
        std::string strName = gDirUtilp->getBaseFileName(strFilename, true);

        std::string agent_id_str = gAgentID.asString();
        LLStringUtil::replaceString(agent_id_str, "-", "");
        std::string::size_type agent_delim_offset = strName.find_first_of("_");
        if (agent_delim_offset != std::string::npos && agent_delim_offset != 0)
        {
            std::string file_agent_id_str = strName.substr(0, agent_delim_offset);
            if (file_agent_id_str != agent_id_str)
            {
                continue;
            }
            else
            {
                strName.erase(0, agent_delim_offset + 1);
            }
        }

        std::string::size_type offset = strName.find_last_of("-");
        if ( (std::string::npos != offset) && (offset != 0) && (offset == strName.length() - 9))
            strName.erase(strName.length() - 9);

        LLStringUtil::trim(strName);
        if (0 == strName.length())
            strName = LLTrans::getString("unknown_script");

        sdFiles.append(LLSD().with("path", strTempPath + strFilename).with("name", strName));
    }

    LL_INFOS() << "Checking for recoverable scripts. Found: " << sdFiles.size() << LL_ENDL;

    if (sdFiles.size())
        LLFloaterReg::showInstance("script_recover", sdData);
}

LLScriptRecoverQueue::LLScriptRecoverQueue(const LLSD& sdFiles)
    : mAccountCurrent(recoveryAccount())
{
    for (LLSD::array_const_iterator itFile = sdFiles.beginArray(), endFile = sdFiles.endArray(); itFile != endFile;  ++itFile)
    {
        const LLSD& sdFile = *itFile;
        if (LLFile::isfile(sdFile["path"]))
            m_FileQueue.insert(std::pair<std::string, LLSD>(sdFile["path"].asString(), sdFile));
    }
}
void LLScriptRecoverQueue::start(const LLSD& files)
{
    auto queue = std::shared_ptr<LLScriptRecoverQueue>(new LLScriptRecoverQueue(files));
    sRecoverQueues.insert(queue);
    queue->recoverNext();
}
void LLScriptRecoverQueue::resetAccount()
{
    // Async callbacks retain weak queue references. Backups remain on disk.
    for (const auto& queue : sRecoverQueues) queue->mRetired = true;
    sRecoverQueues.clear();
}
bool LLScriptRecoverQueue::current() const
{
    return !mRetired && mAccountCurrent();
}
void LLScriptRecoverQueue::finish()
{
    mRetired = true;
    sRecoverQueues.erase(shared_from_this());
}
void LLScriptRecoverQueue::failed(const std::string& path)
{
    if (!current()) { finish(); return; }
    if (path != mPendingPath) return;
    // Keep failed source files recoverable on the next login.
    m_FileQueue.erase(path); mPendingPath.clear();
    recoverNext();
}
bool LLScriptRecoverQueue::recoverNext()
{
    const auto keepAlive = shared_from_this();
    if (!current()) { finish(); return false; }
    /**
     * Steps:
     *  (1) create a script inventory item under Lost and Found
     *  (2) once we have the item's UUID we can upload the script
     *  (3) once the script is uploaded we move on to the next item
     */
    const LLUUID idFNF = gInventory.findCategoryUUIDForType(LLFolderType::FT_LOST_AND_FOUND);

    // Sanity check - if the associated UUID is non-null then this file is already being processed
    filename_queue_t::const_iterator itFile = m_FileQueue.begin();
    while ( (itFile != m_FileQueue.end()) && (itFile->second.has("item")) && (itFile->second["item"].asUUID().notNull()) )
        ++itFile;

    if (m_FileQueue.end() == itFile)
    {
        LLInventoryPanel* pInvPanel = LLInventoryPanel::getActiveInventoryPanel(true);
        LLFolderViewFolder* pFVF = dynamic_cast<LLFolderViewFolder*>(pInvPanel ? pInvPanel->getItemByID(idFNF) : NULL);
        if (pFVF)
        {
            pFVF->setOpenArrangeRecursively(true, LLFolderViewFolder::RECURSE_UP);
            pInvPanel->setSelection(idFNF, true);
        }

        finish();
        return false;
    }

    std::string strItemDescr;
    LLViewerAssetType::generateDescriptionFor(LLAssetType::AT_LSL_TEXT, strItemDescr);

    // Ansariel: Need to work around a bug preventing us from creating a
    //           script with the default name for new scripts
    std::string strScriptName = fixNewScriptDefaultName(itFile->second["name"].asString());

    mPendingPath = itFile->first;
    create_inventory_item(gAgent.getID(), gAgent.getSessionID(), idFNF, LLTransactionID::tnull,
                          strScriptName, strItemDescr, LLAssetType::AT_LSL_TEXT, LLInventoryType::IT_LSL,
                          gDirUtilp->getExtension(itFile->first) == "luabackup" ? SST_LUA : SST_LSL,
                          PERM_MOVE | PERM_TRANSFER, new LLCreateRecoverScriptCallback(weak_from_this(), mPendingPath));
    return true;
}

void LLScriptRecoverQueue::onCreateScript(const LLUUID& idItem, const std::string& path)
{
    const auto keepAlive = shared_from_this();
    if (!current()) { finish(); return; }
    auto found = m_FileQueue.find(path);
    if (path != mPendingPath || found == m_FileQueue.end()) return;
    auto item = gInventory.getItem(idItem);
    if (!item || item->getType() != LLAssetType::AT_LSL_TEXT) { failed(path); return; }
    found->second["item"] = idItem;
    const std::string cap = gAgent.getRegionCapability("UpdateScriptAgent");
    llstat info;
    if (cap.empty() || LLFile::stat(path, &info) != 0 || info.st_size < 0)
    { failed(path); return; }
    std::string buffer;
    try { buffer.resize(static_cast<size_t>(info.st_size)); }
    catch (const std::exception&) { failed(path); return; }
    LLFILE* file = LLFile::fopen(path, "rb");
    if (!file) { failed(path); return; }
    const size_t read = fread(buffer.data(), 1, buffer.size(), file);
    LLFile::close(file);
    if (read != buffer.size()) { failed(path); return; }
    const std::weak_ptr<LLScriptRecoverQueue> weak = shared_from_this();
    LLBufferedAssetUploadInfo::invnUploadFinish_f complete =
        [weak, path](LLUUID item, LLUUID asset, LLUUID replacement, LLSD result)
        {
            if (auto queue = weak.lock(); queue && queue->current() && queue->mPendingPath == path)
                queue->onSavedScript(item, asset, replacement, result);
        };
    LLBufferedAssetUploadInfo::uploadFailed_f failure =
        [weak, path](LLUUID, LLUUID, LLSD, std::string)
        {
            if (auto queue = weak.lock()) queue->failed(path);
            return false; // Preserve the normal upload error notification.
        };
    const std::string target = gDirUtilp->getExtension(path) == "luabackup" ? "luau" : "mono";
    LLResourceUploadInfo::ptr_t upload(new LLScriptAssetUpload(idItem, target, buffer, complete, failure));
    if (!current()) { finish(); return; }
    LLViewerAssetUpload::EnqueueInventoryUpload(cap, upload);
}

void LLScriptRecoverQueue::onSavedScript(LLUUID itemId, LLUUID newAssetId, LLUUID newItemId, LLSD response)
{
    const auto keepAlive = shared_from_this();
    if (!current()) { finish(); return; }
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD( response[ LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS ] );

    filename_queue_t::iterator itFile = m_FileQueue.begin();
    while ( (itFile != m_FileQueue.end()) && ((!itFile->second.has("item")) || (itFile->second["item"].asUUID() != itemId)) )
        ++itFile;

    if (itFile == m_FileQueue.end())
    {
        LL_WARNS() << "Unknown file" << LL_ENDL;
        return;
    }

    if (itFile->first != mPendingPath) return;
    if (status && response["state"].asString() == "complete")
    {
        LL_INFOS() << "Recovered script uploaded successfully" << LL_ENDL;

        LLPointer<LLViewerInventoryItem> pItem = gInventory.getItem(itemId);
        if (pItem.notNull())
        {
            // Ansariel: Rename back scripts with default name
            std::string strScriptName = itFile->second["name"].asString();
            if (strScriptName == NEW_LSL_NAME)
            {
                LLPointer<LLViewerInventoryItem> pNewItem = new LLViewerInventoryItem(pItem);
                pNewItem->rename(strScriptName);
                pNewItem->updateServer(false);
                if (!current()) { finish(); return; }
                gInventory.updateItem(pNewItem);
                if (!current()) { finish(); return; }
                gInventory.notifyObservers();
                if (!current()) { finish(); return; }
            }

            if (!current()) { finish(); return; }
            LLFile::remove(itFile->first);
            m_FileQueue.erase(itFile);
        }
        else m_FileQueue.erase(itFile);
    }
    else
    {
        LL_WARNS() << "Recovered script upload FAILED" << LL_ENDL;

        LLViewerInventoryItem* pItem = gInventory.getItem( itemId );
        if (pItem)
            gInventory.changeItemParent(pItem, gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH), false);
        if (!current()) { finish(); return; }
        m_FileQueue.erase(itFile);
    }
    if (!current()) { finish(); return; }
    mPendingPath.clear();
    recoverNext();
}

// ============================================================================
