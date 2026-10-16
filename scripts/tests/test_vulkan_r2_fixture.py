"""Verify the connected R2 transport fixture cannot silently substitute success."""
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
import copy
import shutil
import subprocess
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import vulkan_r2_fixture as fixture
from run_vulkan_session_replay import r2_contracts_passed, baseline_inventory_item, body_lods_passed


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


class R2ReadinessTest(unittest.TestCase):
    def test_shape_replacement_waits_for_current_outfit_fetch(self):
        source = (Path(__file__).resolve().parents[2] / 'indra/newview/vsr2replay.cpp').read_text()
        stage = source.split('        if (step == 1)\n', 1)[1].split('        if (step == 2)\n', 1)[0]
        fixture_source = r"""
#include <cassert>
#include <initializer_list>
#include <stdexcept>
struct Value { int value = 0; int asInteger() const { return value; }
 Value& operator[](const char*) { return *this; } Value& operator=(Value*) { return *this; } };
struct Size {};
Value ll_sd_from_vector3(const Size&) { return {}; }
struct Avatar { int wearables = 4; bool baked = true; Size mBodySize;
 Value evidence() { return {wearables}; }
 struct Bakes { bool ready; bool empty() const { return !ready; } };
 Bakes localBakes() const { return {baked}; }
 Avatar& appearanceOwner() { return *this; } } avatarOwner;
struct Inventory { bool complete = false; int pending = 0;
 bool isCategoryComplete(int folder) const { return complete && folder != pending; } } gInventory;
struct Appearance { bool admit = true;
 bool canWear(std::initializer_list<int>, bool replace) const { assert(replace); return admit; } } appearance;
struct Session { Appearance* appearance() { return &::appearance; } } session;
struct LLAppearanceMgr { int submissions = 0;
 static LLAppearanceMgr& instance() { static LLAppearanceMgr owner; return owner; }
 int getCOF() const { return 17; }
 void wearItemOnAvatar(int id, bool update, bool replace) {
  assert(gInventory.complete && id == 6 && update && replace); ++submissions;
 } };
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Replay { int step = 1; int cof = 17, bodyFolder = 10, clothingFolder = 7, objectsFolder = 8; Size initialSize; Value evidence; Avatar* avatar = &avatarOwner;
 int item(int id) const { return id; }
 bool tick() { if (step == 1) STAGE return true; }
};
int main() {
 Replay replay;
 // The exact failed order: bakes ready, current outfit still fetching.
 assert(!replay.tick() && replay.step == 1 && LLAppearanceMgr::instance().submissions == 0);
 gInventory.complete = true;
 for (const auto pending : {17, 10, 7, 8}) {
  gInventory.pending = pending;
  assert(!replay.tick() && replay.step == 1 && LLAppearanceMgr::instance().submissions == 0);
 }
 gInventory.pending = 0;
 avatarOwner.baked = false;
 assert(!replay.tick() && replay.step == 1 && LLAppearanceMgr::instance().submissions == 0);
 avatarOwner.baked = true;
 appearance.admit = false;
 bool failed = false;
 try { replay.tick(); } catch (const std::runtime_error&) { failed = true; }
 assert(failed && replay.step == 1 && LLAppearanceMgr::instance().submissions == 0);
 appearance.admit = true;
 assert(!replay.tick() && replay.step == 2 && LLAppearanceMgr::instance().submissions == 1);
 assert(replay.tick() && LLAppearanceMgr::instance().submissions == 1);
}
""".replace('STAGE', stage)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'readiness.cpp'
            executable = Path(directory) / 'readiness.exe'
            cpp.write_text(fixture_source)
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(executable)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


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

    def test_remote_bake_fetch_returns_actual_legacy_xfer_bytes(self):
        identifier = 'c4000000-0000-0000-0000-000000000001'
        encoded = b'actual archived legacy xfer fixture bytes'
        (self.root/(identifier+'.legacy.j2c')).write_bytes(encoded)
        self.assertTrue(fixture.get(self.handler, '/texture', dict(texture_id=[identifier])))
        self.assertEqual(self.handler.wfile.getvalue(), encoded)
        self.assertEqual(self.server.requests[-1]['asset'], identifier)

    def test_standard_motion_assets_are_explicit_not_a_wildcard(self):
        identifier = 'c7000000-0000-0000-0000-000000000001'
        (self.root/'motion-catalog.json').write_text(json.dumps(dict(assets=[identifier])))
        self.assertTrue(fixture.get(self.handler, '/asset/', dict(animatn_id=[identifier])))
        self.assertEqual(self.handler.wfile.getvalue(), fixture.animation_bytes(locomotion=True))
        unknown = Handler(self.server)
        self.assertFalse(fixture.get(unknown, '/asset/', dict(animatn_id=['c7000000-0000-0000-0000-000000000002'])))

    def test_unknown_owner_and_missing_contracts_fail(self):
        self.assertTrue(fixture.modify(self.handler, 'DELETE', '/r2-ais/item/unknown'))
        self.assertEqual(self.handler.error, 404)
        self.assertFalse(r2_contracts_passed(dict(passed=True, r1_contracts={}), []))
        self.assertFalse(r2_contracts_passed(dict(r2_contracts=dict(ordinary_inventory_actions=True)), []))

    def test_ais_item_fetch_keeps_connected_inventory_offer_and_script_routes(self):
        self.server.r2_baseline_item = baseline_inventory_item
        for suffix, kind in ((1, 'texture'), (4, 'lsltext')):
            identifier = f'71000000-0000-0000-0000-{suffix:012d}'
            self.assertTrue(fixture.get(self.handler, '/r2-ais/item/'+identifier, {}))
            self.assertEqual(self.handler.response, baseline_inventory_item(identifier))
            self.assertEqual(self.handler.response['type'], kind)
            self.assertEqual(self.handler.response['item_id'], identifier)
            self.assertEqual(self.server.requests[-1]['found'], True)
        unknown = Handler(self.server)
        self.assertTrue(fixture.get(unknown, '/r2-ais/item/unknown', {}))
        self.assertEqual(unknown.error, 404)
        self.assertFalse(self.server.requests[-1]['found'])

    def test_completed_upload_remains_editable_through_real_ais_item_route(self):
        identifier = '71000000-0000-0000-0000-000000000002'
        asset = '60000000-0000-0000-0000-000000000007'
        metadata = dict(folder_id='textures', asset_type='texture', inventory_type='texture',
                        name='Native uploaded texture', description='Connected native upload qualification',
                        next_owner_mask=2147483647, group_mask=0, everyone_mask=0)
        fixture.uploaded_item(self.server, identifier, asset, metadata)
        cof_version = fixture.folder(self.server, fixture.COF)[1]
        self.assertEqual(fixture.folder(self.server, 'textures')[1], 2)
        self.assertEqual(fixture.item(self.server, identifier)['asset_id'], asset)
        for description in ('Native legacy metadata edit', 'Native modern metadata edit'):
            self.assertTrue(fixture.modify(self.handler, 'PATCH', '/r2-ais/item/'+identifier, dict(desc=description)))
            self.assertEqual(self.handler.response['desc'], description)
            self.assertEqual(fixture.item(self.server, identifier)['desc'], description)
            self.assertEqual(self.server.requests[-1]['request'], dict(desc=description))
        self.assertEqual(fixture.item(self.server, identifier)['name'], metadata['name'])
        self.assertEqual(fixture.folder(self.server, 'textures')[1], 4)
        self.assertEqual(fixture.folder(self.server, fixture.COF)[1], cof_version)

    def test_body_lod_evidence_requires_every_shipped_mesh_and_case(self):
        catalog = Path(__file__).resolve().parents[2]/'indra/newview/character/avatar_lad.xml'
        parts = {}
        for mesh in ET.parse(catalog).getroot().findall('mesh'):
            parts.setdefault(mesh.attrib['type'], []).append(mesh)
        record = dict(all_parts_and_lods=True, unchanged_pose_reuses_geometry=True, cases=[])
        for name, lods in parts.items():
            for power in range(16):
                mesh = lods[power % len(lods)]
                record['cases'].append(dict(part_name=name, lod=int(mesh.attrib['lod']), height=1 << power,
                    reference_area=float(mesh.attrib['min_pixel_width'])**2, vertices=100, indices=300))
        self.assertTrue(body_lods_passed(record))
        missing = copy.deepcopy(record)
        missing['cases'] = [row for row in missing['cases'] if row['part_name'] != 'skirtMesh']
        self.assertFalse(body_lods_passed(missing))
        truncated = copy.deepcopy(record)
        truncated['cases'].pop()
        self.assertFalse(body_lods_passed(truncated))
        invalid = copy.deepcopy(record)
        invalid['cases'][0]['reference_area'] += 1
        self.assertFalse(body_lods_passed(invalid))
        duplicate = copy.deepcopy(record)
        duplicate['cases'].append(copy.deepcopy(duplicate['cases'][0]))
        self.assertFalse(body_lods_passed(duplicate))
        self.assertFalse(body_lods_passed(dict(all_parts_and_lods=True, unchanged_pose_reuses_geometry=True, cases=[])))

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

    def test_ao_categories_and_links_remain_visible_without_changing_cof(self):
        root='70000000-0000-0000-0000-000000000001'
        self.server.r2_categories=[dict(folder_id=root,parent_id='null',type_default=8,name='Root')]
        self.assertTrue(fixture.post(self.handler,'/r2-ais/category/'+root,
            dict(categories=[dict(name='#AO',type_default=-1)])))
        ao=self.handler.response['_created_categories'][0]
        self.assertTrue(fixture.post(self.handler,'/r2-ais/category/'+ao,
            dict(categories=[dict(name='Standing',type_default=-1)])))
        standing=self.handler.response['_created_categories'][0]
        source='b1000000-0000-0000-0000-000000000006'
        before=fixture.folder(self.server,fixture.COF)
        self.assertTrue(fixture.post(self.handler,'/r2-ais/category/'+standing,
            dict(links=[dict(linked_id=source,desc='0')])))
        link=self.handler.response['_created_items'][0]
        rows,version=fixture.folder(self.server,standing)
        self.assertEqual([row['item_id'] for row in rows],[link])
        self.assertEqual(version,2)
        self.assertEqual(fixture.folder(self.server,fixture.COF),before)
        self.assertTrue(fixture.modify(self.handler,'PATCH','/r2-ais/category/'+ao,dict(name='#AO:**')))
        self.assertEqual(self.handler.response['name'],'#AO:**')
        self.assertTrue(fixture.modify(self.handler,'DELETE','/r2-ais/item/'+link))
        self.assertEqual(fixture.folder(self.server,standing),([],3))
        self.assertEqual(fixture.folder(self.server,fixture.COF),before)


if __name__ == '__main__':
    unittest.main()
