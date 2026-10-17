"""Actual plugin evidence must retain every immutable GPU generation."""
import copy
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from run_vulkan_session_replay import MEDIA_PAGE,media_contracts_passed,shared_settings_contracts_passed,append_llsd
import xml.etree.ElementTree as ET
class MediaEvidence(unittest.TestCase):
    def test_publication_pixels_lifecycle_and_missing_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);(root/'r3').mkdir()
            data={key:True for key in ('actual_plugin','shared_manager','shared_consumer_retained','immutable_generations','retired','account_retired')}
            for index,(name,w,h,colors) in enumerate((('initial',64,64,((17,34,51),(68,85,102),(119,136,153),(170,187,204))),('updated',64,64,((221,34,17),(51,204,85),(136,17,187),(68,153,238))),('resized',78,46,((221,34,17),(51,204,85),(136,17,187),(68,153,238)))),1):
                file=f'r3/media-{name}.rgba';(root/file).write_bytes(b''.join(bytes((*colors[(y>=h//2)*2+(x>=w//2)],255)) for y in range(h) for x in range(w)))
                data[name]=dict(width=w,height=h,bits_width=128,bits_height=h,origin_opengl=True,generation=index,file=file)
            report={'r3_media':data};requests=[dict(path='/r3-media',bytes=len(MEDIA_PAGE),sha256=hashlib.sha256(MEDIA_PAGE).hexdigest()),dict(path='/r3-media-phase',phase=1),dict(path='/r3-media-phase',phase=2)]
            self.assertTrue(media_contracts_passed(report,requests,root))
            for key in ('actual_plugin','shared_manager','shared_consumer_retained','immutable_generations','retired','account_retired'):
                wrong=copy.deepcopy(report);wrong['r3_media'][key]=False;self.assertFalse(media_contracts_passed(wrong,requests,root))
            self.assertFalse(media_contracts_passed(report,requests[:2],root))
            wrong=copy.deepcopy(report);wrong['r3_media']['resized']['generation']=2;self.assertFalse(media_contracts_passed(wrong,requests,root))
            path=root/data['updated']['file'];original=path.read_bytes();path.write_bytes(original[::-1]);self.assertFalse(media_contracts_passed(report,requests,root))
            path.write_bytes(original);path.unlink();self.assertFalse(media_contracts_passed(report,requests,root))
    def test_preferences_need_real_saved_values(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);(root/'r3').mkdir()
            row=dict(navbar_callback=True,debug_callback=True,persisted=True,font_callbacks=2,file='r3/settings-roundtrip.xml',navigation=True,favorites=False,debug=True,font=2)
            report={'r3_shared_settings':row}
            self.assertFalse(shared_settings_contracts_passed(report,root))
            saved={setting:dict(Type='Boolean',Value=row[key]) for setting,key in (('ShowNavbarNavigationPanel','navigation'),('ShowNavbarFavoritesPanel','favorites'),('DebugViews','debug'))}
            saved['ChatFontSize']=dict(Type='S32',Value=2)
            def write():
                node=ET.Element('llsd');append_llsd(node,saved);(root/row['file']).write_bytes(ET.tostring(node))
            write();self.assertTrue(shared_settings_contracts_passed(report,root))
            saved['ChatFontSize']['Value']=1;write();self.assertFalse(shared_settings_contracts_passed(report,root))
            saved['ChatFontSize']['Value']=2;saved['ShowNavbarNavigationPanel']['Value']=False;write();self.assertFalse(shared_settings_contracts_passed(report,root))

if __name__=='__main__':unittest.main()
