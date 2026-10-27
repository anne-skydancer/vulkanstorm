"""Actual shared collision decode, account retirement and menu intent regressions."""
from pathlib import Path
import os,unittest
from test_native_material_selection import body,run
ROOT=Path(os.environ.get('VS_COLLISIONS_ROOT',Path(__file__).resolve().parents[2]));OVERLAY=Path(os.environ['VS_COLLISIONS_PROPOSED'])if 'VS_COLLISIONS_PROPOSED'in os.environ else None
def source(path):
 p=OVERLAY/path if OVERLAY else ROOT/path
 return(p if p.exists()else ROOT/path).read_text()
class NativeCollisionTests(unittest.TestCase):
 def test_actual_wire_reader_uses_each_collision_block(self):
  cpp=body(source('indra/newview/llviewermessage.cpp'),'void process_mean_collision_alert_message(')
  start=cpp.index('        msgsystem->getUUIDFast(');end=cpp.index('        type = ',start);actual=cpp[start:end]
  run(self,r'''#include <cassert>
#include <vector>
using LLUUID=int;using U32=unsigned;using U8=unsigned char;using F32=float;
#define _PREHASH_MeanCollision 1
#define _PREHASH_Perp 2
#define _PREHASH_Time 3
#define _PREHASH_Mag 4
#define _PREHASH_Type 5
struct Message{void getUUIDFast(int,int,int&v,int index=0){v=100+index;}void getU32Fast(int,int,unsigned&v,int index=0){v=200+index;}void getF32Fast(int,int,float&v,int index=0){v=300.f+index;}void getU8Fast(int,int,unsigned char&v,int index=0){v=index;}};
int main(){Message msg;auto*msgsystem=&msg;for(int i=0;i<3;++i){LLUUID perp;U32 time;F32 mag;U8 u8type;ACTUAL assert(perp==100+i&&time==200u+i&&mag==300.f+i&&u8type==i);}}
'''.replace('ACTUAL',actual))
 def test_actual_account_reset_releases_history_and_does_not_publish_to_next_account(self):
  actual=body(source('indra/newview/llviewermessage.cpp'),'void reset_mean_collision_account(')
  run(self,r'''#include <cassert>
#include <list>
struct Entry{static inline int live=0;Entry(){++live;}~Entry(){--live;}};std::list<Entry*>gMeanCollisionList;int gLastDisplayedTime=8;
void reset()ACTUAL
int main(){gMeanCollisionList.push_back(new Entry);gMeanCollisionList.push_back(new Entry);reset();assert(Entry::live==0&&gMeanCollisionList.empty()&&gLastDisplayedTime==0);reset();}
'''.replace('ACTUAL',actual))
  cpp=source('indra/newview/llviewermessage.cpp');self.assertIn('if (current()) mean_name_callback(id, name)',cpp)
  menu=source('indra/newview/llfloaterbump.cpp')
  for method in ('void FSBumpListMenu::onContextMenuItemClick(', 'bool FSBumpListMenu::onContextMenuItemEnable('):self.assertIn('mAccountCurrent && !mAccountCurrent()',body(menu,method));self.assertIn('mUUIDs.empty()',body(menu,method))
  session=source('indra/newview/vsnativesession.cpp');self.assertIn('msg.setHandlerFunc("MeanCollisionAlert", process_mean_collision_alert_message)',session)
if __name__=='__main__':unittest.main()
