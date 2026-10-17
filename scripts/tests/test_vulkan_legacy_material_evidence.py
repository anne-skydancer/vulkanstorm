"""Legacy material evidence requires independent UV/factor values and real archived bytes."""
import base64
import copy
import hashlib
import math
from pathlib import Path
import sys
import tempfile
import unittest
from uuid import UUID
import xml.etree.ElementTree as ET
import zlib

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from run_vulkan_session_replay import legacy_material_contracts_passed


class LegacyMaterialEvidenceTests(unittest.TestCase):
    def test_uv_factors_and_missing_or_changed_artifacts_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);(root/'r3').mkdir()
            sources=['b3600000-0000-0000-0000-000000000001','b3600000-0000-0000-0000-000000000002']
            # Isolate archive/receipt checks here. The viewer uses its real
            # binary LLSD decoder and LLMaterial constructor in the runtime case.
            raw=b'archive-witness'+UUID('b3400000-0000-0000-0000-000000000001').bytes+b''.join(UUID(value).bytes for value in sources)
            document=ET.Element('llsd');mapping=ET.SubElement(document,'map');ET.SubElement(mapping,'key').text='Zipped'
            ET.SubElement(mapping,'binary').text=base64.b64encode(zlib.compress(raw)).decode()
            payload=ET.tostring(document)
            (root/'r3/legacy-materials.xml').write_bytes(payload);(root/'r3/legacy-materials.bin').write_bytes(raw)
            def uv(s,t,x,y,a):
                c,r=math.cos(a),math.sin(a);return [s*c,-t*r,s*r,t*c,x+.5-.5*(s*c+s*r),y+.5-.5*(-t*r+t*c)]
            data=dict(faces=6,fullbright=True,object_retired=True,account_retired=True,
                      normal_source=sources[0],specular_source=sources[1],normal_uv=uv(2,3,.125,-.25,.25),
                      specular_uv=uv(.75,1.25,-.125,.375,-.5),specular=[64/255,128/255,192/255,32/255],
                      roughness=1-128/255,cutoff=64/255,readbacks=['r3/legacy-0.rgba','r3/legacy-1.rgba'])
            data['variants']=[dict(fullbright=index in (2,5),emissive_alpha=index==3,shiny=[1,2,0,3,0,0][index],
                alpha=['opaque','opaque','mask','opaque','blend','opaque'][index],base_alpha=128/255 if index==4 else 1) for index in range(6)]
            for file,color in zip(data['readbacks'],((128,128,255,255),(11,22,33,255))):(root/file).write_bytes(bytes(color)*256)
            report=dict(r3_legacy_material=data)
            requests=[dict(path='/r3-legacy-materials',bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest())]
            self.assertTrue(legacy_material_contracts_passed(report,requests,root))
            for field in ('normal_uv','specular_uv','specular'):
                failed=copy.deepcopy(report);failed['r3_legacy_material'][field][0]+=.1
                self.assertFalse(legacy_material_contracts_passed(failed,requests,root))
            self.assertFalse(legacy_material_contracts_passed(report,[],root))
            failed=copy.deepcopy(report);failed['r3_legacy_material']['variants'][3]['emissive_alpha']=False
            self.assertFalse(legacy_material_contracts_passed(failed,requests,root))
            (root/'r3/legacy-materials.bin').write_bytes(raw[:-1])
            self.assertFalse(legacy_material_contracts_passed(report,requests,root))
            (root/'r3/legacy-materials.bin').write_bytes(raw)
            (root/data['readbacks'][0]).unlink()
            self.assertFalse(legacy_material_contracts_passed(report,requests,root))


if __name__=='__main__':unittest.main()
