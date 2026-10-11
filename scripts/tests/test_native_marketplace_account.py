"""Compile real marketplace capability/reset and suspended-reply boundaries."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


class MarketplaceAccountTest(unittest.TestCase):
    def test_link_replace_retired_callbacks_and_scene_folder_boundary(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/newview/llfloaterlinkreplace.cpp').read_text()
        callback = source[source.index('void LLFloaterLinkReplace::itemRemovedCallback('):
                          source.index('void LLFloaterLinkReplace::decreaseOpenItemCount()')]
        condition = re.search(r'else if \((source_item->getParentUUID\(\) != cof_folder_id[^\{]+)\)\n\s*\{', source).group(1)
        fixture = r'''
#include <cassert>
#include <functional>
struct LLUUID {int id=0;bool notNull()const{return id!=0;}};
template<class T> struct LLHandle {T* p;bool isDead()const{return !p;}T* get()const{return p;}};
struct LLAppearanceMgr {int calls=0;static LLAppearanceMgr* getInstance(){static LLAppearanceMgr mgr;return &mgr;}
 void updateClothingOrderingInfo(const LLUUID&){++calls;}};
struct LLFloaterLinkReplace {std::function<bool()> mAccountCurrent;int completions=0;
 void decreaseOpenItemCount(){++completions;}
 static void itemRemovedCallback(LLHandle<LLFloaterLinkReplace>,const LLUUID&);};
struct Window {bool native=false;bool isNativeVulkan()const{return native;}} window,*gViewerWindow=&window;
struct Item {int parent;int getParentUUID()const{return parent;}};
struct Inventory {bool isObjectDescendentOf(int parent,int outfits){return parent==outfits;}}gInventory;
''' + callback + '\nbool allowed(Item* source_item){int cof_folder_id=1,outfit_folder_id=2;return ' + condition + ';}\n'
        fixture += r'''
int main(){LLFloaterLinkReplace panel;bool current=true;panel.mAccountCurrent=[&]{return current;};
 LLFloaterLinkReplace::itemRemovedCallback({nullptr},{3});assert(LLAppearanceMgr::getInstance()->calls==0);
 current=false;LLFloaterLinkReplace::itemRemovedCallback({&panel},{3});
 assert(panel.completions==0&&LLAppearanceMgr::getInstance()->calls==0);
 current=true;LLFloaterLinkReplace::itemRemovedCallback({&panel},{0});assert(panel.completions==1);
 Item ordinary{3},cof{1},outfits{2};window.native=true;
 assert(allowed(&ordinary)&&!allowed(&cof)&&!allowed(&outfits));
 window.native=false;assert(allowed(&ordinary)&&!allowed(&cof)&&allowed(&outfits));}
'''
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'link_account.cpp'; exe=Path(directory)/'link_account.exe'
            path.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++17',str(path),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)

    def test_capability_cache_reset_and_retired_http_replies(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/newview/llmarketplacefunctions.cpp').read_text()
        methods = source[source.index('void LLMarketplaceData::resetNativeAccount()'):
                         source.index('void LLMarketplaceData::setSLMStatus(')]
        # Extract every production post-suspension guard, including the retry
        # timer. An old account must return before cache/UI/inventory mutation.
        coros = source[source.index('void LLMarketplaceData::getMerchantStatusCoro()'):
                       source.index('void LLMarketplaceData::resetNativeAccount()')]
        guards = re.findall(r'(?:LLSD result = httpAdapter->[^\n]+AndSuspend[^\n]+;|llcoro::suspendUntilTimeout\([^\n]+;)\n(\s*if \(!currentAccount\(\)\) return;)', coros)
        self.assertEqual(len(guards), 8)
        fixture = r'''
#include <cassert>
#include <string>
#include <vector>
#include <functional>
namespace MarketplaceStatusCodes { enum {MARKET_PLACE_NOT_INITIALIZED=0}; }
namespace MarketplaceFetchCodes { enum {MARKET_FETCH_NOT_DONE=0}; }
struct Signal { int slots=4; void disconnect_all_slots(){slots=0;} };
struct Agent {std::string cap; std::string getRegionCapability(const char* name){
 assert(std::string(name)=="DirectDelivery");return cap;}} gAgent;
struct LLMarketplaceData {
 std::vector<int> mMarketplaceItems{1},mVersionFolders{2},mPendingUpdateSet{3},mValidationWaitingList{4};
 int mMarketPlaceStatus=3,mMarketPlaceDataFetched=4;
 std::string mMarketPlaceFailureReason="old account"; bool mDirtyCount=true;
 Signal status,data; Signal* mStatusUpdatedSignal=&status; Signal* mDataFetchedSignal=&data;
 void resetNativeAccount();std::string getSLMConnectURL(const std::string&);
};
''' + methods
        for index, guard in enumerate(guards):
            fixture += f'void reply{index}(bool valid,int& mutations){{auto currentAccount=[valid]{{return valid;}};{guard}\n++mutations;}}\n'
        fixture += r'''
int main(){LLMarketplaceData owner;gAgent.cap="https://current.example/cap";
 assert(owner.getSLMConnectURL("/listings")=="https://current.example/cap/listings");
 gAgent.cap.clear();assert(owner.getSLMConnectURL("/merchant").empty());
 owner.resetNativeAccount();assert(owner.mMarketplaceItems.empty()&&owner.mVersionFolders.empty());
 assert(owner.mPendingUpdateSet.empty()&&owner.mValidationWaitingList.empty());
 assert(owner.mMarketPlaceStatus==0&&owner.mMarketPlaceDataFetched==0);
 assert(owner.mMarketPlaceFailureReason.empty()&&!owner.mDirtyCount);
 assert(owner.status.slots==0&&owner.data.slots==0);
 owner.mStatusUpdatedSignal=nullptr;owner.mDataFetchedSignal=nullptr;owner.resetNativeAccount();
 int mutations=0;
'''
        for index in range(len(guards)):
            fixture += f'reply{index}(false,mutations);assert(mutations=={index});reply{index}(true,mutations);assert(mutations=={index+1});\n'
        fixture += '}'
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'marketplace_account.cpp'
            exe = Path(directory) / 'marketplace_account.exe'
            path.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout+ran.stderr)


if __name__ == '__main__':
    unittest.main()
