"""The script icon gate requires exact complete files, not declared pass flags."""
from pathlib import Path
import base64
import copy
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import run_vulkan_session_replay as runner

class HudIconEvidence(unittest.TestCase):
    def test_independent_complete_rgba_and_metadata_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);folder=root/'script-icon';folder.mkdir()
            pixels=bytes(value for y in range(4) for x in range(4) for value in (x*30,y*40,20,255))
            row={'width':4,'height':4,'image_generation':base64.b64encode((9).to_bytes(8,'big')).decode(),
                 'mismatched_components':0,'pixels_verified':True,'actual':'script-icon/actual.rgba','expected':'script-icon/expected.rgba'}
            report={'script_icon_readback':row}
            def metadata(item=row):
                xml=ET.Element('llsd');runner.append_llsd(xml,item);ET.ElementTree(xml).write(folder/'readback.xml')
            metadata();(folder/'actual.rgba').write_bytes(pixels);(folder/'expected.rgba').write_bytes(pixels)
            self.assertTrue(runner.hud_icon_contracts_passed(report,root))
            # Every pixel matters, including the first and last border texels.
            for index in (0,len(pixels)-2):
                damaged=bytearray(pixels);damaged[index]^=127;(folder/'actual.rgba').write_bytes(damaged)
                self.assertFalse(runner.hud_icon_contracts_passed(report,root))
            (folder/'actual.rgba').write_bytes(pixels[:-1]);self.assertFalse(runner.hud_icon_contracts_passed(report,root))
            (folder/'actual.rgba').write_bytes(b''.join(pixels[y*16:(y+1)*16] for y in reversed(range(4))))
            self.assertFalse(runner.hud_icon_contracts_passed(report,root))
            (folder/'actual.rgba').write_bytes(pixels)
            for key,value in (('width',True),('height',1025),('image_generation','bad'),('image_generation',base64.b64encode(bytes(8)).decode()),('pixels_verified',False),('mismatched_components',1),('actual','../outside.rgba'),('expected','script-icon/actual.rgba')):
                changed=copy.deepcopy(report);changed['script_icon_readback'][key]=value;metadata(changed['script_icon_readback'])
                with self.subTest(key=key,value=value):self.assertFalse(runner.hud_icon_contracts_passed(changed,root))
            metadata();(folder/'readback.xml').unlink();self.assertFalse(runner.hud_icon_contracts_passed(report,root))
            metadata();(folder/'expected.rgba').unlink();self.assertFalse(runner.hud_icon_contracts_passed(report,root))
            clear=bytes([255,0,255,255])*16;(folder/'actual.rgba').write_bytes(clear);(folder/'expected.rgba').write_bytes(clear)
            self.assertFalse(runner.hud_icon_contracts_passed(report,root))
if __name__=='__main__':unittest.main()
