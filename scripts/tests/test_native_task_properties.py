"""Actual Task Properties shared controller and origin-scoped confirmation checks."""
from pathlib import Path
import os,unittest
from test_native_material_selection import body,run
ROOT=Path(os.environ.get('VS_TASK_PROPERTIES_ROOT',Path(__file__).resolve().parents[2]));OVERLAY=Path(os.environ['VS_TASK_PROPERTIES_PROPOSED']) if 'VS_TASK_PROPERTIES_PROPOSED' in os.environ else None

def source(path):
 q=OVERLAY/path if OVERLAY else ROOT/path
 return(q if q.exists()else ROOT/path).read_text()
class NativeTaskPropertiesTests(unittest.TestCase):
 def test_actual_permission_controller_refuses_missing_root_and_keeps_wire_fields(self):
  cpp=source('indra/newview/llsidepaneltaskinfo.cpp');actual=body(cpp,'void LLSidepanelTaskInfo::onCommitPerm(')
  run(self,r"""
#include <cassert>
#include <utility>
using U8=unsigned char;using U32=unsigned;
struct LLUICtrl{bool value=false;};struct LLCheckBoxCtrl:LLUICtrl{bool get(){return value;}};
struct Object{};Object object;struct Selection{bool hasRoot=true;int changes=0;U8 field=0;U32 perm=0;bool state=false;Selection*getSelection(){return this;}Object*getFirstRootObject(){return hasRoot?&object:nullptr;}void selectionSetObjectPermissions(U8 f,bool s,U32 p){++changes;field=f;state=s;perm=p;}}selection;
template<class F>decltype(auto)taskInfoSelection(F&&f){return f(&selection);}
struct LLSidepanelTaskInfo{int disabled=0;void disablePermissions(){++disabled;}static void onCommitPerm(LLUICtrl*ctrl,void*data,U8 field,U32 perm)ACTUAL};
int main(){LLSidepanelTaskInfo view;LLCheckBoxCtrl checkbox;checkbox.value=true;LLSidepanelTaskInfo::onCommitPerm(&checkbox,&view,3,1024);assert(selection.changes==1&&selection.field==3&&selection.perm==1024&&selection.state&&view.disabled==1);selection.hasRoot=false;LLSidepanelTaskInfo::onCommitPerm(&checkbox,&view,2,2048);assert(selection.changes==1&&view.disabled==1);selection.hasRoot=true;checkbox.value=false;LLSidepanelTaskInfo::onCommitPerm(&checkbox,nullptr,1,256);assert(selection.changes==2&&!selection.state&&selection.field==1&&selection.perm==256);}
""".replace('ACTUAL',actual))
 def test_actual_deed_confirmation_preserves_original_group_power_and_origin(self):
  cpp=source('indra/newview/llsidepaneltaskinfo.cpp');method=body(cpp,'void LLSidepanelTaskInfo::onClickDeedToGroup(');callback=body(method,'[weak, selected, current, lease]')
  run(self,r"""
#include <cassert>
#include <functional>
#include <memory>
using LLSD=int;struct LLUUID{int id=0;bool notNull(){return id!=0;}static LLUUID null;};LLUUID LLUUID::null;
struct LLNotificationsUtil{static int getSelectedOption(int,int response){return response;}};
constexpr int GP_OBJECT_DEED=7;struct Agent{bool allowed=true;int checked=0;bool hasPowerInGroup(LLUUID group,int power){assert(power==7);checked=group.id;return allowed;}}gAgent;
struct View{virtual~View()=default;bool active=true;bool mAccountCurrent(){return active;}};using LLSidepanelTaskInfo=View;
struct Handle{View*value;View*get()const{return value;}};
struct Selection{bool identical=true;int group=8,sent=0,target=0;bool selectGetGroup(LLUUID&id){id={group};return identical;}void sendOwner(LLUUID owner,LLUUID group,bool override){assert(owner.id==0&&!override);++sent;target=group.id;}};
int main(){View view;Handle weak{&view};auto selected=std::make_shared<Selection>();bool active=true;std::function<bool()>current=[&]{return active;};auto respond=[&](const LLSD&notification,const LLSD&response)CALLBACK;respond(0,1);assert(selected->sent==0);respond(0,0);assert(selected->sent==1&&selected->target==8&&gAgent.checked==8);for(int mode=0;mode<5;++mode){selected->group=8;selected->identical=true;active=true;view.active=true;weak.value=&view;gAgent.allowed=true;if(mode==0)selected->group=0;if(mode==1)selected->identical=false;if(mode==2)active=false;if(mode==3)view.active=false;if(mode==4)gAgent.allowed=false;respond(0,0);assert(selected->sent==1);}active=true;view.active=true;weak.value=nullptr;respond(0,0);assert(selected->sent==1);}
""".replace('CALLBACK',callback))
 def test_actual_invoker_uses_real_task_properties_graph_and_teardown(self):
  menu=source('indra/newview/llviewermenu.cpp');ui=source('indra/newview/vsstartupui.cpp');reset=source('indra/newview/vsnativeim.cpp');host=source('indra/newview/llfloatermarketplacelistings.cpp')
  invoker=body(menu,'void handle_object_inspect()')
  self.assertIn('if (selection.getFirstRootObject()) LLFloaterReg::showInstance("task_properties")',invoker)
  self.assertIn('task_panel->refreshSelection()',host)
  for exact in ('t == typeid(LLSidepanelTaskInfo)','name == "task_properties"','name == "sidepanel_task_info"','name == "Object.EnableInspect"'):self.assertIn(exact,ui)
  self.assertIn('LLFloaterReg::destroyInstance("task_properties")',reset)
if __name__=='__main__':unittest.main()
