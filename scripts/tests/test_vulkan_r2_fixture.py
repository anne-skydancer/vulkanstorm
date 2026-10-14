"""Verify the connected R2 transport fixture cannot silently substitute success."""
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import vulkan_r2_fixture as fixture
from run_vulkan_session_replay import r2_contracts_passed


class Handler:
    def __init__(self, server):
        self.server = server
        self.response = None
        self.error = None
        self.wfile = io.BytesIO()
    def respond_llsd(self, response):
        self.response = response
    def send_error(self, status):
        self.error = status
    def send_response(self, status):
        self.status = status
    def send_header(self, *args):
        pass
    def end_headers(self):
        pass


class R2FixtureTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.server = SimpleNamespace(url='http://127.0.0.1', requests=[])
        fixture.configure(self.server, self.root)
        self.handler = Handler(self.server)
        rows = [dict(item_id=f'b1000000-0000-0000-0000-{i:012d}',
                     asset_id=f'b2000000-0000-0000-0000-{i:012d}',
                     parent_id='body', type='bodypart', name=str(i), desc='')
                for i in range(1, 8)]
        (self.root / 'inventory.json').write_text(json.dumps(dict(items=rows, agent='agent')))

    def test_real_link_creation_order_update_and_removal(self):
        initial, version = fixture.folder(self.server, fixture.COF)
        self.assertEqual(len(initial), 4)
        source = 'b1000000-0000-0000-0000-000000000006'
        self.assertTrue(fixture.post(self.handler, '/r2-ais/category/'+fixture.COF,
                                     dict(links=[dict(linked_id=source, desc='@400')])))
        created = self.handler.response['_created_items'][0]
        self.assertEqual(fixture.item(self.server, created)['linked_id'], source)
        self.assertTrue(fixture.modify(self.handler, 'PATCH', '/r2-ais/item/'+created, dict(desc='@401')))
        self.assertEqual(fixture.item(self.server, created)['desc'], '@401')
        self.assertTrue(fixture.modify(self.handler, 'DELETE', '/r2-ais/item/'+created))
        self.assertEqual(self.handler.response['_removed_items'], [created])
        remaining, after = fixture.folder(self.server, fixture.COF)
        self.assertEqual(len(remaining), 4)
        self.assertEqual(after, version+3)
        self.assertIsNone(fixture.item(self.server, created))

    def test_uploaded_bytes_are_archived_and_returned_exactly(self):
        # Transport proof only; viewer/codec tests establish JPEG2000 content.
        encoded = b'fixture codec bytes' * 200
        self.assertTrue(fixture.binary_post(self.handler, '/r2-bake-upload', encoded))
        identifier = self.handler.response['new_asset']
        self.assertEqual((self.root/(identifier+'.j2c')).read_bytes(), encoded)
        self.assertTrue(fixture.get(self.handler, '/texture', dict(texture_id=[identifier])))
        self.assertEqual(self.handler.wfile.getvalue(), encoded)
        rejected = Handler(self.server)
        self.assertTrue(fixture.binary_post(rejected, '/r2-bake-upload', b''))
        self.assertEqual(rejected.error, 400)
        self.assertEqual(len(self.server.r2_uploads), 1)

    def test_unknown_owner_and_missing_contracts_fail(self):
        self.assertTrue(fixture.modify(self.handler, 'DELETE', '/r2-ais/item/unknown'))
        self.assertEqual(self.handler.error, 404)
        self.assertFalse(r2_contracts_passed(dict(passed=True, r1_contracts={}), []))
        self.assertFalse(r2_contracts_passed(dict(r2_contracts=dict(ordinary_inventory_actions=True)), []))

    def test_ais_fetch_preserves_baseline_folders_and_reports_complete_cof(self):
        self.server.r2_categories = [dict(folder_id=fixture.COF,parent_id='root',
                                         type_default=46,name='Current Outfit')]
        self.assertTrue(fixture.get(self.handler,'/r2-ais/category/current/links',{}))
        response=self.handler.response
        self.assertEqual(response['category_id'],fixture.COF)
        self.assertEqual(response['type_default'],46)
        self.assertEqual(response['version'],10)
        self.assertEqual(len(response['_embedded']['links']),4)
        self.assertEqual(set(response['_embedded']),{'items','links','categories'})
        first_link=next(iter(response['_embedded']['links'].values()))
        self.assertEqual(first_link['_embedded']['item']['item_id'],first_link['linked_id'])
        self.assertIsNone(fixture.folder(self.server,'unrelated-baseline-folder'))

    def test_normal_auxiliary_folder_creation_is_not_rejected(self):
        root='70000000-0000-0000-0000-000000000001'
        self.server.r2_categories=[dict(folder_id=root,parent_id='null',type_default=8,name='Root')]
        self.assertTrue(fixture.post(self.handler,'/r2-ais/category/'+root,
            dict(categories=[dict(category_id='',name='#Firestorm',parent_id=root,type_default=-1)])))
        self.assertIsNone(self.handler.error)
        response=self.handler.response
        identifier=response['_created_categories'][0]
        self.assertEqual(response['_embedded']['categories'][identifier]['name'],'#Firestorm')
        self.assertEqual(response['_updated_category_versions'][root],2)
        self.assertTrue(fixture.get(self.handler,'/r2-ais/category/'+root+'/children',{}))
        self.assertEqual(self.handler.response['version'],2)
        self.assertIn(identifier,self.handler.response['_embedded']['categories'])

    def test_unconfigured_fixture_does_not_change_baseline(self):
        handler = Handler(SimpleNamespace())
        self.assertIsNone(fixture.folder(handler.server, fixture.COF))
        self.assertFalse(fixture.get(handler, '/texture', {}))
        self.assertFalse(fixture.post(handler, '/r2-bake', {}))

    def test_native_animation_asset_uses_viewer_asset_query_name(self):
        self.assertTrue(fixture.get(self.handler,'/asset',dict(animatn_id=[fixture.ANIMATION])))
        self.assertEqual(self.handler.wfile.getvalue(),fixture.animation_bytes())
        self.assertEqual(self.handler.wfile.getvalue()[:4],b'\x01\x00\x00\x00')


if __name__ == '__main__':
    unittest.main()
