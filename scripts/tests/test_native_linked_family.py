"""Real linked selection/deselection traversal against original viewer kernels."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class LinkedFamily(unittest.TestCase):
 def test_original_kernel_order_avatar_boundaries_and_current_identity(self):
  folder=(ROOT/'indra/newview')
  index=(folder/'vsobjectindex.h').read_text();index='\n'.join(line for line in index.splitlines()if not line.startswith('#'))
  scene=(folder/'vsworldscene.cpp').read_text();reference=(ROOT/'indra/newview/llviewerobject.cpp').read_text()
  gold='\n'.join(method(reference,sig).replace('LLViewerObject','Reference')for sig in ['void LLViewerObject::addChild(', 'void LLViewerObject::addThisAndAllChildren(', 'void LLViewerObject::addThisAndNonJointChildren('])
  source=(HERE/'native_linked_family_fixture.cpp').read_text().replace('INDEX',index).replace('METHOD',method(scene,'std::optional<std::vector<VSWorldScene::ObjectHandle>> VSWorldScene::linkedFamily(')).replace('GOLD',gold).replace('EXTRA_FUNCTIONS','\n'.join(method(scene,sig)for sig in ['bool VSWorldScene::freezeObject(', 'bool VSWorldScene::setSelectionState(', 'bool VSWorldScene::initializeSelectionMotion(', 'bool VSWorldScene::initializeDeselectionMotion(']))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'family.cpp';exe=Path(tmp)/'family.exe';cpp.write_text(source);built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr);run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
 def test_original_motion_initialization_and_rollback_state_are_distinct(self):
  import ast
  tree=ast.parse((ROOT/'scripts/tests/test_world_motion_contracts.py').read_text());models={node.targets[0].id:ast.literal_eval(node.value)for node in ast.walk(tree)if isinstance(node,ast.Assign)and len(node.targets)==1 and isinstance(node.targets[0],ast.Name)and node.targets[0].id in ('vector','quaternion')}
  folder=(ROOT/'indra/newview');motion=(folder/'vsworldmotion.h').read_text()
  selector=(ROOT/'indra/newview/llselectmgr.cpp').read_text();goldSelect=method(selector,'LLObjectSelectionHandle LLSelectMgr::selectObjectOnly(');goldSelect=goldSelect[goldSelect.index('    object->setVelocity(LLVector3::zero);'):]
  goldSelect=goldSelect[:goldSelect.index('    object->resetRot();')+len('    object->resetRot();')].replace('object->','reference.').replace('LLVector3::zero','LLVector3()')
  goldDeselect=method(selector,'void LLSelectMgr::deselectObjectOnly(');goldDeselect=goldDeselect[goldDeselect.index('    object->setAngularVelocity( 0,0,0 );'):goldDeselect.index('    if (send_to_sim)')].replace('object->','reference.')
  goldReset=method((ROOT/'indra/newview/llviewerobject.cpp').read_text(),'void LLViewerObject::resetRot(').replace('LLViewerObject','ReferenceMotion')
  cpp='''#include <algorithm>
#include <cmath>
#include <cassert>
#include "v3math.h"
#include "llquaternion.h"
#define private public
#include "vsworldmotion.h"
#undef private
struct ReferenceMotion{LLVector3 velocity,acceleration,angular;LLQuaternion mAngularVelocityRot,rotation;float mRotTime=8;void resetRotTime(){mRotTime=0;}void resetRot();void setVelocity(const LLVector3&v){velocity=v;}void setAcceleration(const LLVector3&v){acceleration=v;}void setAngularVelocity(float x,float y,float z){angular={x,y,z};}void setVelocity(float x,float y,float z){velocity={x,y,z};}};
GOLD_RESET
int main(){VSWorldMotion native;VSWorldMotion::Policy policy;native.accept({}, {}, {10,0,0},{2,0,0},{0,0,1},0);native.update(.1,1,true,false,policy);ReferenceMotion reference;reference.velocity=native.mVelocity;reference.acceleration=native.mAcceleration;reference.angular=native.mAngular;reference.rotation=native.mRotation;reference.mAngularVelocityRot=native.mAngularDelta;
GOLD_SELECT
native.initializeSelection();assert(!(native.mVelocity!=reference.velocity)&&!(native.mAcceleration!=reference.acceleration)&&!(native.mAngular!=reference.angular)&&!(native.mRotation!=reference.rotation)&&!(native.mAngularDelta!=reference.mAngularVelocityRot));assert(!(native.mBaseRotation!=native.mRotation));
// Rollback gates never mutate velocity/rotation accumulation.
native.mVelocity={2,0,0};const auto delta=native.mAngularDelta;native.freeze(true);native.freeze(false);assert(native.mVelocity.mV[0]==2&&!(delta!=native.mAngularDelta));
// SetSelected on every node resets rotation accumulation without zeroing motion.
native.setSelectionState(true);assert(native.selected()&&native.mVelocity.mV[0]==2);assert(!native.update(.2,1,true,false,policy));
native.accept({}, {}, {9,0,0},{7,0,0},{0,0,3},.3);reference.velocity=native.mVelocity;reference.acceleration=native.mAcceleration;reference.angular=native.mAngular;
GOLD_DESELECT
native.initializeDeselection();assert(!(native.mVelocity!=reference.velocity)&&!(native.mAngular!=reference.angular)&&!(native.mAcceleration!=reference.acceleration)&&native.mAcceleration.mV[0]==7);native.setSelectionState(false);assert(!native.selected());}
'''.replace('GOLD_RESET',goldReset).replace('GOLD_SELECT',goldSelect).replace('GOLD_DESELECT',goldDeselect)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   path=Path(tmp);(path/'v3math.h').write_text(models['vector']);(path/'llquaternion.h').write_text(models['quaternion']);(path/'vsworldmotion.h').write_text(motion);(path/'motion.cpp').write_text(cpp);exe=path/'motion.exe';built=subprocess.run([compiler,'-std=c++17',str(path/'motion.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr);run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)

if __name__=='__main__':unittest.main()
