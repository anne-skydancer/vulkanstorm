"""Production Land Holdings query intent and real dispatcher checks."""
from pathlib import Path
import os,unittest
from test_native_material_selection import body,run
ROOT=Path(os.environ.get('VS_HOLDINGS_ROOT',Path(__file__).resolve().parents[2]));OVERLAY=Path(os.environ['VS_HOLDINGS_PROPOSED'])if 'VS_HOLDINGS_PROPOSED'in os.environ else None
def source(path):
 p=OVERLAY/path if OVERLAY else ROOT/path
 return(p if p.exists()else ROOT/path).read_text()
class NativeLandHoldingsTests(unittest.TestCase):
 def test_actual_reply_gate_preserves_account_query_transaction_and_source(self):
  actual=body(source('indra/newview/llfloaterlandholdings.cpp'),'bool LLFloaterLandHoldings::acceptsReply(')
  run(self,r'''
#define VS_NATIVE_VULKAN 1
#include <cassert>
#include <functional>
#include <string>
struct LLUUID{int value=0;bool isNull()const{return !value;}bool operator==(const LLUUID&)const=default;};
struct Agent{LLUUID getID(){return{7};}}gAgent;
struct VSNativeSession{enum class Phase{Connected,Login};static VSNativeSession*active(){static VSNativeSession value;return&value;}Phase state=Phase::Connected;int host(){return 5;}Phase phase(){return state;}};
struct LLMessageSystem{LLUUID agent{7},query{},transaction{9};int sender=5;int getSender(){return sender;}void getUUID(std::string block,std::string field,LLUUID&out){out=field=="AgentID"?agent:field=="QueryID"?query:transaction;}};
struct LLFloaterLandHoldings{std::function<bool()>mAccountCurrent=[]{return true;};LLUUID mTransaction{9};bool acceptsReply(LLMessageSystem*msg)const ACTUAL};
int main(){LLFloaterLandHoldings self;LLMessageSystem msg;assert(self.acceptsReply(&msg));msg.query={8};assert(!self.acceptsReply(&msg));msg.query={};msg.transaction={8};assert(!self.acceptsReply(&msg));msg.transaction={9};msg.agent={8};assert(!self.acceptsReply(&msg));msg.agent={7};msg.sender=6;assert(!self.acceptsReply(&msg));msg.sender=5;VSNativeSession::active()->state=VSNativeSession::Phase::Login;assert(!self.acceptsReply(&msg));VSNativeSession::active()->state=VSNativeSession::Phase::Connected;self.mAccountCurrent=[]{return false;};assert(!self.acceptsReply(&msg));}
'''.replace('ACTUAL',actual))
 def test_actual_open_issues_fresh_transaction_and_clears_only_own_account_rows(self):
  actual=body(source('indra/newview/llfloaterlandholdings.cpp'),'void LLFloaterLandHoldings::onOpen(')
  run(self,r'''
#define VS_NATIVE_VULKAN 1
#include <cassert>
#include <functional>
#include <string>
using U32=unsigned;using LLSD=int;struct LLUUID{int value=0;static const LLUUID null;void generate(){static int next=9;value=++next;}};const LLUUID LLUUID::null{};
struct VSNativeSession{static bool active(){return true;}};bool live=true;std::function<bool()>vs_native_im_guard(){return[]{return live;};}
struct LLScrollListCtrl{int clears=0;void clearRows(){++clears;}};struct LLParcel{static const int C_ANY=0;};constexpr U32 DFQ_AGENT_OWNED=32;int sends=0,transaction=0;void send_places_query(LLUUID query,LLUUID trans,std::string,U32 flags,int,std::string){assert(!query.value&&flags==DFQ_AGENT_OWNED);transaction=trans.value;++sends;}
struct LLFloaterLandHoldings{std::function<bool()>mAccountCurrent;LLUUID mTransaction;int mActualArea=88,mBillableArea=99;bool mFirstPacketReceived=true;LLScrollListCtrl rows;template<class T>T*getChild(std::string){return&rows;}void onOpen(const LLSD&key)ACTUAL};
int main(){LLFloaterLandHoldings self;self.onOpen(0);assert(sends==1&&transaction==10&&!self.mActualArea&&!self.mBillableArea&&!self.mFirstPacketReceived&&self.rows.clears==1);self.onOpen(0);assert(sends==2&&transaction==11&&self.rows.clears==2);live=false;self.onOpen(0);assert(sends==2&&self.rows.clears==2);}
'''.replace('ACTUAL',actual))
 def test_actual_native_transport_uses_original_null_and_group_dispatch(self):
  self.assertIn('msg.setHandlerFunc("PlacesReply", process_places_reply)',source('indra/newview/vsnativesession.cpp'))
  self.assertIn('LLFloaterLandHoldings::processPlacesReply(msg, data)',source('indra/newview/llviewermessage.cpp'));self.assertIn('name == "land_holdings"',source('indra/newview/vsstartupui.cpp'));self.assertIn('LLFloaterLandHoldings::sHasLindenHome = false;',source('indra/newview/vsnativeim.cpp'))
if __name__=='__main__':unittest.main()
