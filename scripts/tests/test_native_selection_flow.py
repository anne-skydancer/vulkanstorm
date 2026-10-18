"""Compile frozen original branch body and production shared kernel, compare traces."""
from pathlib import Path
import re,shutil,subprocess,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]

def reference():
 s=(HERE/'native_selection_flow_reference.cpp').read_text();s=s[s.index('LLObjectSelectionHandle'):]
 s=s.replace(s[:s.index('{')],'template<class Peer,class Pick> auto reference(Peer& peer,const Pick& pick,bool ignore_group,bool temp_select,bool select_root)\n')
 start=s.index('            LLVector3d selection_center')
 end=s.index('\n        }\n\n        if (temp_select)',start)
 s=s[:start]+'            peer.turnToSelection(object);'+s[end:]
 start=s.index('        if (temp_select)\n');end=s.index('        } //if(temp_select)',start)+len('        } //if(temp_select)')
 s=s[:start]+'''        if(temp_select){if(!already_selected)peer.transientRootAndChildren(object);}'''+s[end:]
 replacements={
 'LLViewerObject* object = pick.getObject();':'auto object = peer.object(pick);',
 'object = object->getRootEdit();':'object = peer.rootEdit(object);',
 'RlvActions::isRlvEnabled()':'peer.rlvEnabled()',
 'RlvActions::canEdit(object)':'peer.canEdit(object)',
 'LLToolMgr::instance().inBuildMode()':'peer.inBuildMode()',
 'LLToolMgr::instance().leaveBuildMode()':'peer.leaveBuildMode()',
 'RlvActions::hasBehaviour(RLV_BHVR_FARTOUCH)':'peer.fartouch()',
 'object->isAttachment()':'peer.attachment(object)',
 'object->permYouOwner()':'peer.youOwner(object)',
 'static RlvCachedBehaviourModifier<float> s_nFartouchDist(RLV_MODIFIER_FARTOUCHDIST);':'',
 's_nFartouchDist * s_nFartouchDist':'peer.fartouchDistanceSquared()',
 'dist_vec_squared(gAgent.getPositionAgent(), object->getPositionRegion())':'peer.distanceToObjectSquared(object)',
 'dist_vec_squared(gAgent.getPositionAgent(), pick.mIntersection)':'peer.distanceToSurfaceSquared(pick)',
 'LLFloaterReg::instanceVisible("build")':'peer.buildVisible()',
 'pick.mKeyMask':'peer.mask(pick)',
 'LLSelectMgr::getInstance()->getSelection()->contains(object,SELECT_ALL_TES)':'peer.containsAllFaces(object)',
 'LLSelectMgr::getInstance()->getSelection()':'peer.selection()',
 'gSavedSettings.getBOOL(':'peer.setting(',
 'gSavedSettings.setBOOL(':'peer.setting(',
 'LLSelectMgr::getInstance()->mAllowSelectAvatar':'peer.allowSelectAvatar()',
 'LLSelectMgr::getInstance()->setForceSelection(':'peer.forceSelection(',
 'LLHUDIcon* last_hit_hud_icon = pick.mHUDIcon;':'auto last_hit_hud_icon = peer.scriptIcon(pick);',
 'last_hit_hud_icon && last_hit_hud_icon->getSourceObject()':'last_hit_hud_icon',
 'LLFloaterScriptDebug::show(last_hit_hud_icon->getSourceObject()->getID())':'peer.showScriptDebug(pick)',
 'object->isSelected()':'peer.selected(object)',
 'object->getNumTEs()':'peer.faceCount(object)',
 'const LLTextureEntry* tep = object->getTE(pick.mObjectFace);':'auto tep = peer.faceSelection(object,pick);',
 '!tep->isSelected()':'!*tep',
 '!LLViewerMediaFocus::getInstance()->getFocusedObjectID().isNull()':'peer.hasMediaFocus()',
 'LLViewerMediaFocus::getInstance()->clearFocus()':'peer.clearMediaFocus()',
 'LLSelectMgr::getInstance()->deselectObjectOnly(object)':'peer.deselectOnly(object)',
 'LLSelectMgr::getInstance()->deselectObjectAndFamily(object, true, true)':'peer.deselectFamily(object)',
 'LLSelectMgr::getInstance()->deselectAll()':'peer.deselectAll()',
 'LLSelectMgr::getInstance()->selectObjectOnly(object, SELECT_ALL_TES, pick.mGLTFNodeIndex, pick.mGLTFPrimitiveIndex)':'peer.selectOnly(object,-1,peer.gltfNode(pick),peer.gltfPrimitive(pick))',
 'LLSelectMgr::getInstance()->selectObjectOnly(object, SELECT_ALL_TES)':'peer.selectOnly(object,-1,-1,-1)',
 'LLSelectMgr::getInstance()->selectObjectAndFamily(object)':'peer.selectFamily(object)',
 '''F32 target_zoom;
            F32 current_zoom;
            LLSelectMgr::getInstance()->getAgentHUDZoom(target_zoom, current_zoom);''':'auto zoom=peer.hudZoom();',
 'LLSelectMgr::getInstance()->setAgentHUDZoom(target_zoom, current_zoom)':'peer.hudZoom(zoom)',
 'gAgentCamera.getFocusOnAvatar()':'peer.focusOnAvatar()',
 'LLVOAvatar::findAvatarFromAttachment(object) != gAgentAvatarp':'!peer.selfAttachment(object)',
 'object != gAgentAvatarp':'!peer.self(object)',
 }
 for old,new in replacements.items():
  if old not in s:raise AssertionError(old)
  s=s.replace(old,new)
 # Strip only comments; leave actual conditions and statements untouched.
 return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/','',s)

class Flow(unittest.TestCase):
 def test_original_branch_order_settings_face_zoom_and_lazy_calls(self):
  kernel=(ROOT/'indra/newview/vsselectionflow.h').read_text();kernel='\n'.join(x for x in kernel.splitlines()if not x.startswith('#'))
  source=(HERE/'native_selection_flow_fixture.cpp').read_text().replace('KERNEL',kernel).replace('REFERENCE',reference())
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   path=Path(tmp);cpp=path/'flow.cpp';exe=path/'flow.exe';cpp.write_text(source)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
