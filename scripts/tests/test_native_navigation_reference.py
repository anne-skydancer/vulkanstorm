"""Execute original and transposed algorithms with identical renderer-peer CPU values."""
from pathlib import Path
import sys,tempfile,subprocess,shutil
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent;sys.path.insert(0,str(ROOT/'scripts/tests'))
from native_source_fixture import method
new=(ROOT/'indra/newview/llagent.cpp').read_text(encoding='utf-8')
names=['void LLAgent::startAutoPilotGlobal(', 'void LLAgent::setAutoPilotTargetGlobal(', 'void LLAgent::startFollowPilot(', 'void LLAgent::autoPilot(', 'void LLAgent::stopAutoPilot(']
reference=(HERE/'navigation_gl_reference.cpp').read_text(encoding='utf-8')
current='\n'.join(method(new,name)for name in names)+'\n'+method(new,'void LLAgent::retireNativeAutoPilot(')
adapter=r'''
bool nativeMode=false,accountAlive=true;
struct VSAgentNavigation{
 bool native()const{return nativeMode;}bool valid()const{return isAgentAvatarValid();}bool inAir()const{return gAgentAvatarp->mInAir;}
 float pelvisToFoot()const{return gAgentAvatarp->getPelvisToFoot();}LLVector3d avatarPosition()const{return gAgentAvatarp->getPositionGlobal();}
 std::optional<LLVector3d>objectPosition(LLUUID id)const{const auto*p=gObjectList.findObject(id);return p?std::optional<LLVector3d>(p->getPositionGlobal()):std::nullopt;}
 float stepHeight(const LLVector3d&a,const LLVector3d&b,LLVector3d&intersection)const{LLVector3 normal;LLViewerObject*object;return LLWorld::getInstance()->resolveStepHeightGlobal(nullptr,a,b,intersection,normal,&object);}
 std::function<bool()>accountGuard()const{return []{return accountAlive;};}void face(LLVector3)const{}
};
'''
source=(HERE/'native_navigation_reference_fixture.cpp').read_text(encoding='utf-8').replace('COMMON',(HERE/'native_navigation_common.cpp').read_text(encoding='utf-8')).replace('REFERENCE_METHODS',reference).replace('CURRENT_METHODS',current).replace('ADAPTER',adapter)
source=source.replace(' for(int caseNo=0;caseNo<1200;++caseNo){', ' for(bool nativeMode:{false,true})for(int caseNo=0;caseNo<1200;++caseNo){ CURRENT::nativeMode=nativeMode;')
with tempfile.TemporaryDirectory()as tmp:
 p=Path(tmp)/'navigation.cpp';p.write_text(source,encoding='utf-8');exe=p.with_suffix('.exe');r=subprocess.run([shutil.which('clang++')or shutil.which('g++'),'-std=c++17',str(p),'-o',str(exe)],capture_output=True,text=True);print(r.stdout+r.stderr);r.check_returncode();subprocess.run([str(exe)],check=True)
print('PASS: 2400 original/transposed native+GL autopilot trajectories, controls and lazy reference getter counts')
