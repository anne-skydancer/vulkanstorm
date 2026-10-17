"""Compile shared currency ownership and transaction controllers without money/network operations."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def body(filename, signature):
    source = (ROOT / "indra/newview" / filename).read_text(encoding="utf-8-sig")
    start = source.index("{", source.index(signature))
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{": depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0: return source[start:index + 1]
    raise AssertionError(signature)


class CurrencyLifecycleTest(unittest.TestCase):
    def test_actual_confirmation_profile_and_transaction_controllers(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler, "A C++ compiler is required")
        buy = body("llcurrencyuimanager.cpp", "void LLCurrencyUIManager::buy(")
        profile = body("llfloaterbuycurrency.cpp", "void LLFetchAvatarPaymentInfo::processProperties(")
        reset = body("llfloaterbuycurrency.cpp", "void LLFloaterBuyCurrency::resetAccountRequests()")
        money = body("llviewermessage.cpp", "void process_money_balance_reply(")
        fixture = r'''
#include <cassert>
#include <deque>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <sstream>
using S32=int; using U32=unsigned;
struct LLUUID { int value=0; bool operator==(const LLUUID& v) const{return value==v.value;} bool operator!=(const LLUUID& v) const{return !(*this==v);} };
struct LLUIString {
    LLUIString(const std::string&){} void setArg(const char*,const std::string&){} operator std::string()const{return "purchase";}
};
std::string llformat(const char*,int){return "amount";}
struct LLConfirmationManager {
    struct ListenerBase {virtual ~ListenerBase(){} virtual void confirmed(const std::string&)=0;};
    inline static std::unique_ptr<ListenerBase> pending;
    static void confirm(const std::string&, const std::string&, ListenerBase* p){pending.reset(p);}
};
int purchases=0;
struct LLCurrencyUIManager {
    struct Impl {
        std::shared_ptr<int> mLifetime=std::make_shared<int>(0);
        std::function<bool()> mCurrent;
        int mUserCurrencyBuy=20; std::string mSiteConfirm="click";
        bool hasEstimate(){return true;} std::string getLocalEstimate(){return "estimate";}
        void startCurrencyBuy(const std::string&){++purchases;}
    };
    Impl& impl;
    bool canBuy(){return impl.mCurrent();} std::string getLocalEstimate(){return "estimate";}
    void buy(const std::string& buy_msg) BUY
};
enum EAvatarProcessorType {APT_PROPERTIES,APT_PROPERTIES_LEGACY};
struct LLAvatarLegacyData { LLUUID avatar_id; bool payment=true; };
struct LLAvatarData {
    LLUUID avatar_id; bool payment=true;
    LLAvatarData()=default;
    LLAvatarData(const LLAvatarLegacyData& d):avatar_id(d.avatar_id),payment(d.payment){}
};
struct LLAvatarPropertiesProcessor { static bool hasPaymentInfoOnFile(const LLAvatarData* p){return p->payment;} };
struct LLFetchAvatarPaymentInfo;
int profile_opens=0;
struct LLFloaterBuyCurrency {
    inline static LLFetchAvatarPaymentInfo* sPropertiesRequest=nullptr;
    static void handleBuyCurrency(bool,bool,const std::string&,int){++profile_opens;}
    static void resetAccountRequests();
};
struct LLFetchAvatarPaymentInfo {
    std::function<bool()> mCurrent; LLUUID mAvatarID{1}; bool mHasTarget=false; std::string mName; int mPrice=0;
    void processProperties(void* data,EAvatarProcessorType type) PROFILE
};
void LLFloaterBuyCurrency::resetAccountRequests() RESET
struct LLSD { std::map<std::string,std::string> values; std::string& operator[](const std::string& k){return values[k];} };
struct Settings { bool notify=true; bool getBOOL(const char*)const{return notify;} } gSavedSettings;
struct Status {
    int balance=0,credit=0,committed=0;
    void setBalance(int n){balance=n;} void setLandCredit(int n){credit=n;} void setLandCommitted(int n){committed=n;}
} status;
Status* gStatusBar=&status;
LLUUID gAgentID{1},gAgentSessionID{1};
std::ostringstream log_sink;
#define LL_INFOS(x) log_sink
#define LL_DEBUGS(x) log_sink
#define LL_ENDL ""
#define _PREHASH_MoneyData "MoneyData"
#define _PREHASH_Description "Description"
namespace Tea { std::string wrapCurrency(const char* p){return p;} }
struct LLMessageSystem {
    int balance=50,credit=0,committed=0; LLUUID tid{10}; std::string desc="payment"; bool extended=true;
    void getUUID(const char*,const char*,LLUUID& out){out=tid;}
    void getS32(const char*,const char* key,int& out){out=std::string(key)=="MoneyBalance"?balance:0;}
    void getStringFast(const char*,const char*,std::string& out){out=desc;}
    bool has(const char*)const{return extended;}
};
int extended_events=0,legacy_events=0;
void process_money_balance_reply_extended(LLMessageSystem*){++extended_events;}
namespace LLNotificationsUtil {void add(const char*,const LLSD&){++legacy_events;} }
void process_money_balance_reply(LLMessageSystem* msg,void**) MONEY
int main(){
    bool current=true;
    auto owner=std::make_unique<LLCurrencyUIManager::Impl>();owner->mCurrent=[&]{return current;};
    LLCurrencyUIManager manager{*owner};manager.buy("buy");
    current=false;LLConfirmationManager::pending->confirmed("");assert(purchases==0);
    current=true;manager.buy("buy");owner.reset();LLConfirmationManager::pending->confirmed("");assert(purchases==0);
    owner=std::make_unique<LLCurrencyUIManager::Impl>();owner->mCurrent=[&]{return current;};
    LLCurrencyUIManager next{*owner};next.buy("buy");
    ++owner->mUserCurrencyBuy;LLConfirmationManager::pending->confirmed("");assert(purchases==0);
    next.buy("buy");LLConfirmationManager::pending->confirmed("");assert(purchases==1);
    auto* request=new LLFetchAvatarPaymentInfo;request->mCurrent=[&]{return current;};LLFloaterBuyCurrency::sPropertiesRequest=request;
    LLAvatarData data;data.avatar_id={1};current=false;request->processProperties(&data,APT_PROPERTIES);assert(profile_opens==0);
    current=true;data.avatar_id={2};request->processProperties(&data,APT_PROPERTIES);assert(profile_opens==0);
    data.avatar_id={1};request->processProperties(&data,APT_PROPERTIES);assert(profile_opens==1);
    LLFloaterBuyCurrency::sPropertiesRequest=nullptr;request->processProperties(&data,APT_PROPERTIES);assert(profile_opens==1);
    delete request;LLFloaterBuyCurrency::sPropertiesRequest=new LLFetchAvatarPaymentInfo;LLFloaterBuyCurrency::resetAccountRequests();assert(!LLFloaterBuyCurrency::sPropertiesRequest);
    LLMessageSystem message;process_money_balance_reply(&message,nullptr);assert(status.balance==50&&extended_events==1);
    message.balance=60;process_money_balance_reply(&message,nullptr);assert(status.balance==60&&extended_events==1);
    ++gAgentSessionID.value;process_money_balance_reply(&message,nullptr);assert(extended_events==2);
    message.tid={11};message.extended=false;process_money_balance_reply(&message,nullptr);assert(legacy_events==1);
    message.tid={12};message.desc.clear();process_money_balance_reply(&message,nullptr);assert(legacy_events==1);
    message.tid={13};message.desc="payment";gSavedSettings.notify=false;process_money_balance_reply(&message,nullptr);assert(legacy_events==1);
}
'''
        for marker, code in (("BUY", buy), ("PROFILE", profile), ("RESET", reset), ("MONEY", money)):
            fixture = fixture.replace(marker, code)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "currency.cpp"
            binary = Path(directory) / "currency.exe"
            source.write_text(fixture, encoding="utf-8")
            built = subprocess.run([compiler, "-std=c++17", str(source), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
