"""Compare native land permissions with the actual reference policy bodies."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from test_native_camera_transport import method

ROOT = Path(__file__).resolve().parents[2]


class ParcelPermissionsTest(unittest.TestCase):
    def test_reference_policy_for_all_permission_combinations(self):
        header = (ROOT/'indra/newview/vsparcelpermissions.h').read_text()
        header = re.sub(r'^#.*$', '', header, flags=re.M)
        source = (ROOT/'indra/newview/llviewerparcelmgr.cpp').read_text()
        names = ['Voice', 'Fly', 'Push', 'Scripts', 'Damage']
        helpers='\n'.join('template<class Region>\n'+method(source, 'bool vs_allow_parcel_'+name.lower()+'(') for name in names)
        methods = helpers+'\n'+'\n'.join(method(source, 'bool LLViewerParcelMgr::allowAgent'+name+'(const LLViewerRegion*') for name in names)
        methods += '\n'+method(source, 'bool LLViewerParcelMgr::allowAgentBuild(const LLParcel*')
        flags = '\n'.join(re.findall(r'constexpr U64 REGION_FLAGS_.*?;', (ROOT/'indra/llmessage/llregionflags.h').read_text(), re.S))
        parcel_flags = '\n'.join(re.findall(r'const U32 PF_.*?;', (ROOT/'indra/llinventory/llparcelflags.h').read_text(), re.S))
        parcel_source = (ROOT/'indra/llinventory/llparcel.h').read_text()
        getters = '\n'.join(method(parcel_source, 'bool    '+name+'() const') for name in (
            'getAllowModify', 'getAllowDamage', 'getAllowFly', 'getAllowOtherScripts',
            'getParcelFlagAllowVoice', 'getRestrictPushObject'))
        fixture = r'''
#include <cassert>
#include <cstdint>
using U32=uint32_t;using U64=uint64_t;
@FLAGS@
@PARCEL_FLAGS@
struct LLParcel {U32 bits=0;void setAllParcelFlags(U32 value){bits=value;}
 bool getParcelFlag(U32 value)const{return bits&value;}
 GETTERS
};
struct LLViewerRegion {U64 flags=0;
 bool getRegionFlag(U64 bit)const{return flags&bit;}
 bool isVoiceEnabled()const{return getRegionFlag(REGION_FLAGS_ALLOW_VOICE);}
 bool getBlockFly()const{return getRegionFlag(REGION_FLAGS_BLOCK_FLY);}
 bool getRestrictPushObject()const{return getRegionFlag(REGION_FLAGS_RESTRICT_PUSHOBJECT);}
 bool getAllowDamage()const{return getRegionFlag(REGION_FLAGS_ALLOW_DAMAGE);}
};
struct LLViewerParcelMgr {
 DECLARATIONS
 bool allowAgentBuild(const LLParcel*)const;
};
METHODS
HEADER
int main(){
 assert(!VSParcelPermissions{}.known);
 const U64 regions[]={REGION_FLAGS_ALLOW_VOICE,REGION_FLAGS_BLOCK_FLY,REGION_FLAGS_RESTRICT_PUSHOBJECT,
 REGION_FLAGS_SKIP_SCRIPTS,REGION_FLAGS_ESTATE_SKIP_SCRIPTS,REGION_FLAGS_ALLOW_DAMAGE};
 const U32 parcels[]={PF_ALLOW_VOICE_CHAT,PF_ALLOW_FLY,PF_RESTRICT_PUSHOBJECT,PF_CREATE_OBJECTS,PF_ALLOW_OTHER_SCRIPTS,PF_ALLOW_DAMAGE};
 LLViewerParcelMgr mgr;
 for(unsigned r=0;r<64;++r)for(unsigned p=0;p<64;++p)for(bool see:{false,true}){
  LLViewerRegion region;LLParcel parcel;
  for(unsigned i=0;i<6;++i){if(r&(1u<<i))region.flags|=regions[i];if(p&(1u<<i))parcel.bits|=parcels[i];}
  auto actual=VSParcelPermissions::from(region.flags,parcel.bits,see);
  assert(actual.known&&actual.seeAvatars==see);
  assert(actual.voice==mgr.allowAgentVoice(&region,&parcel));
  assert(actual.fly==mgr.allowAgentFly(&region,&parcel));
  assert(actual.push==mgr.allowAgentPush(&region,&parcel));
  assert(actual.build==mgr.allowAgentBuild(&parcel));
  assert(actual.scripts==mgr.allowAgentScripts(&region,&parcel));
  assert(actual.damage==mgr.allowAgentDamage(&region,&parcel));
 }
}
'''.replace('@PARCEL_FLAGS@', parcel_flags).replace('@FLAGS@', flags).replace('GETTERS', getters)
        fixture = fixture.replace('DECLARATIONS', '\n'.join('bool allowAgent'+name+'(const LLViewerRegion*,const LLParcel*)const;' for name in names))
        fixture = fixture.replace('METHODS', methods).replace('HEADER', header)
        fixture = '#include <initializer_list>\n'+fixture
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'permissions.cpp';exe=cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)


if __name__ == '__main__':
    unittest.main()
