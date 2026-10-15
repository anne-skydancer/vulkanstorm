"""Independent failures for capture evidence, image comparisons and timing data."""
from pathlib import Path
import json
import hashlib
import sys
import tempfile
import unittest
import urllib.request
import test_ci_allocation_bounds as bounds
from test_native_simulator_features import method
import xml.etree.ElementTree as ET
from test_gl_probe_binding import ProbeBindingTest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run_vulkan_parity_baseline as baseline
import vulkan_parity_inventory as inventory
import check_vulkan_parity_r0 as acceptance


class BaselineTest(unittest.TestCase):
    def test_login_page_is_pinned_loopback_input_with_receipts(self):
        contract=json.loads((baseline.ROOT/'doc/vulkan/parity/baseline-contract.json').read_text())
        with tempfile.TemporaryDirectory() as directory:
            with baseline.BaselineLoginPage(directory,contract) as page:
                opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
                with opener.open(page.url+'?lang=en',timeout=5) as response:
                    self.assertEqual(response.read(),page.content)
                self.assertIn('<title>'+contract['login_page_fixture']['title']+'</title>',page.content.decode())
                self.assertEqual(page.requests,['/login-page.html?lang=en'])
            self.assertFalse(page.thread.is_alive())
            receipt=json.loads((Path(directory)/'login-page-requests.json').read_text())
            self.assertEqual(receipt['sha256'],hashlib.sha256(page.content).hexdigest())
            contract['login_page_fixture']['sha256']='changed'
            with self.assertRaisesRegex(RuntimeError,'Changed baseline'):
                baseline.BaselineLoginPage(directory,contract)

    def test_blank_error_and_unpainted_pages_cannot_satisfy_startup(self):
        spec=json.loads((baseline.ROOT/'doc/vulkan/parity/baseline-contract.json').read_text())['login_page_fixture']
        with tempfile.TemporaryDirectory() as directory:
            for color in ([0,0,0],[255,255,255],spec['background']):
                with self.assertRaisesRegex(ValueError,'Missing painted'):
                    baseline.startup_page_pixels(self.image(directory,'blank',color*256*240,256,240),spec)
            pixels=[]
            for y in range(240):
                for x in range(256):
                    color=spec['upper'] if 80<=x<144 and 20<=y<52 else spec['lower'] if 160<=x<224 and 160<=y<192 else spec['background']
                    pixels.extend(color)
            counts=baseline.startup_page_pixels(self.image(directory,'painted',pixels,256,240),spec)
            self.assertEqual(counts['upper'],2048)
            self.assertEqual(counts['lower'],2048)

    def test_capture_readiness_requires_actual_cef_paint(self):
        source=(baseline.ROOT/'indra/newview/vsparitybaseline.cpp').read_text()
        ready=method(source,'bool loginPageReady(').replace('std::getenv(', 'fixtureEnv(')
        bounds.CIAllocationBoundsTest.compile(self,r'''
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <cassert>
constexpr int GL_RGBA=1,GL_BGRA=2;
const char* expected="http://127.0.0.1:123/login-page.html";
const char* fixtureEnv(const char*){return expected;}
struct Settings {std::string url=expected;std::string getString(const char*){return url;}} gSavedSettings;
struct Plugin {
 bool valid=true,dirty=false;int format=GL_BGRA,depth=4;std::string title="Vulkanstorm parity login fixture v1";
 std::vector<uint8_t> pixels=std::vector<uint8_t>(256*256*4);
 bool textureValid(){return valid;}bool getDirty(){return dirty;}int getWidth(){return 256;}int getHeight(){return 256;}
 int getBitsWidth(){return 256;}int getTextureDepth(){return depth;}int getTextureFormatPrimary(){return format;}
 std::string getMediaName(){return title;}uint8_t* getBitsData(){return pixels.data();}
} plugin;
struct LLMediaCtrl {Plugin* p=&plugin;Plugin* getMediaPlugin(){return p;}} browser;
struct Root {template<class T>T* findChild(const char*){return &browser;}} root;
struct Viewer {Root* getRootView(){return &root;}} viewer;
Viewer* gViewerWindow=&viewer;
READY
int main(){
 assert(!loginPageReady()); // Correct title without paint is insufficient.
 auto* pixel=plugin.pixels.data()+(128*256+128)*4;
 pixel[0]=77;pixel[1]=51;pixel[2]=25;pixel[3]=255;assert(loginPageReady());
 plugin.title="This site cannot be reached";assert(!loginPageReady());
 plugin.title="Vulkanstorm parity login fixture v1";
 plugin.valid=false;assert(!loginPageReady());plugin.valid=true;
 plugin.dirty=true;assert(!loginPageReady());plugin.dirty=false;
 plugin.depth=3;assert(!loginPageReady());plugin.depth=4;
 plugin.format=0;assert(!loginPageReady());plugin.format=GL_RGBA;
 pixel[0]=25;pixel[2]=77;assert(loginPageReady());
 gSavedSettings.url="http://external.invalid/";assert(!loginPageReady());gSavedSettings.url=expected;
 expected=nullptr;assert(!loginPageReady());expected="http://127.0.0.1:123/login-page.html";
 browser.p=nullptr;assert(!loginPageReady());browser.p=&plugin;
 gViewerWindow=nullptr;assert(!loginPageReady());
}
'''.replace('READY',ready))

    def test_device_name_cannot_substitute_for_the_pinned_loaded_icd(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); folder=root/'staged/icd'; folder.mkdir(parents=True)
            library=folder/'driver.bin'; library.write_bytes(b'locked driver')
            icd=folder/'driver.json'; icd.write_text(json.dumps({'ICD':{'library_path':'driver.bin'}}))
            runtime={'icd':str(icd),'staged_sha256':{'staged/icd/driver.bin':hashlib.sha256(library.read_bytes()).hexdigest()}}
            baseline.verify_vulkan_driver(runtime,str(library.resolve()))
            with self.assertRaisesRegex(RuntimeError,'did not load'):
                baseline.verify_vulkan_driver(runtime,'llvmpipe from another runtime')
            library.write_bytes(b'changed driver')
            with self.assertRaisesRegex(RuntimeError,'immutable runtime'):
                baseline.verify_vulkan_driver(runtime,str(library.resolve()))

    def test_actual_llsd_render_clock_real_values_are_numeric(self):
        clock=baseline.read_llsd(ET.fromstring('<llsd><map><key>elapsed_seconds</key><real>1000</real><key>epoch_seconds</key><real>1700000000</real><key>frame</key><integer>60</integer><key>motion_delta_seconds</key><real>0</real></map></llsd>'))
        self.assertEqual(clock,dict(elapsed_seconds=1000,epoch_seconds=1700000000,frame=60,motion_delta_seconds=0))
        self.assertIsInstance(clock['motion_delta_seconds'],float)

    def image(self, directory, name, pixels, width=2, height=1):
        path = Path(directory)/name
        path.write_bytes(f'P6\n{width} {height}\n255\n'.encode()+bytes(pixels))
        return path

    def test_independent_pixel_tolerance(self):
        with tempfile.TemporaryDirectory() as d:
            a = self.image(d,'a',[0,10,255,50,100,150])
            b = self.image(d,'b',[1,11,254,51,99,150])
            self.assertTrue(baseline.compare(a,b,1)['matched'])
            b = self.image(d,'b',[2,11,254,51,99,150])
            result = baseline.compare(a,b,1)
            self.assertFalse(result['matched'])
            self.assertEqual(result['channels_over_tolerance'],1)

    def test_orientation_is_not_silently_corrected(self):
        with tempfile.TemporaryDirectory() as d:
            a = self.image(d,'a',[255,0,0,0,0,255],1,2)
            b = self.image(d,'b',[0,0,255,255,0,0],1,2)
            self.assertFalse(baseline.compare(a,b,1)['matched'])

    def test_incomplete_readback_fails(self):
        with tempfile.TemporaryDirectory() as d:
            path = self.image(d,'bad',[0,0,0])
            with self.assertRaisesRegex(ValueError,'Incomplete'): baseline.read_ppm(path)

    def test_extent_mismatch_fails(self):
        with tempfile.TemporaryDirectory() as d:
            a = self.image(d,'a',[0]*6)
            b = self.image(d,'b',[0]*6,1,2)
            with self.assertRaisesRegex(ValueError,'extents'): baseline.compare(a,b,1)

    def test_finite_metrics_and_unavailable_gpu(self):
        frame = {n:2 for n in ('pre_render_cpu_ms','render_submit_cpu_ms','capture_wait_and_io_ms')}
        result = baseline.metrics([frame]*5)
        self.assertIsNone(result['gpu_ms'])
        self.assertEqual(result['render_submit_cpu_ms']['count'],5)
        for invalid in (-1,float('nan'),float('inf')):
            frame['pre_render_cpu_ms'] = invalid
            with self.assertRaises(ValueError): baseline.metrics([frame])

    def test_semantic_ids_preserve_variant_identity(self):
        self.assertNotEqual(inventory.stable_id('DRAW','PASS_SIMPLE'),
                            inventory.stable_id('DRAW','PASS_SIMPLE_RIGGED'))
        self.assertEqual(inventory.stable_id('UI','Avatar.Profile'),'UI.Avatar.Profile')

    def test_contract_has_no_hidden_masks_or_performance_claim(self):
        contract = json.loads((inventory.OUT/'baseline-contract.json').read_text())
        self.assertEqual(contract['comparison']['masked_regions'],[])
        self.assertIn('not a sustained',contract['metrics']['statistics'])
        self.assertIn('fixed production-world',contract['required_r0_witnesses'][3])

    def test_discovery_cannot_accept_features_without_evidence(self):
        ledger={'entries':[dict(id='UI.Sample',reference_sources=['sample.cpp'],native_owner='owner',
            phase='R9',dependencies=['R1'],implementation='partial',remaining_gap='finish owner',
            source_qualified=True,software_runtime_qualified=False,authenticated_runtime_qualified=False,evidence=[])]}
        errors=acceptance.validate(ledger,{}, {})
        self.assertIn('Feature accepted without evidence: UI.Sample',errors)
        self.assertIn('No production world replay witness',errors)

    def test_empty_or_failed_runtime_evidence_never_closes_r0(self):
        errors=acceptance.validate({'entries':[]},{'cases':[{'backend':'OpenGL','passed':False}]},{})
        self.assertTrue(errors)
        self.assertTrue(any('independent peer' in e for e in errors))
        self.assertTrue(any('source revisions' in e for e in errors))

    def test_survey_reference_symbols_are_preserved(self):
        ledger=json.loads((inventory.OUT/'acceptance-ledger.json').read_text())
        row=next(r for r in ledger['entries'] if r['id']=='SURVEY.Standard-primitives-linksets')
        self.assertIn('LLVolume::generate',row['reference_symbols'])
        self.assertIn('LLVOVolume::updateGeometry',row['reference_seams'])

    def test_app_routes_and_direct_ui_observers_are_inventoried(self):
        rows=json.loads((inventory.OUT/'acceptance-ledger.json').read_text())['entries']
        ids={r['id'] for r in rows}
        for route in ('chat','objectim','appearance','wear_folder','experience','gridmanager','keybinding'):
            self.assertIn('APP_COMMAND.'+route,ids)
        self.assertNotIn('APP_COMMAND.foo',ids) # Header comment, not a registration.
        observers=[r for r in rows if r['category']=='SETTING' and any('fsfloatercontacts.cpp' in ref for ref in r['reference_sources'])]
        self.assertGreater(len(observers),2)
        self.assertTrue(any(r['category']=='UI_SETTING' for r in rows))
        self.assertEqual(inventory.without_comments('"https://example.com" // omitted'), '"https://example.com" ')

    def test_computed_setting_registrations_are_not_omitted(self):
        rows=json.loads((inventory.OUT/'acceptance-ledger.json').read_text())['entries']
        ids={r['id'] for r in rows}
        for index in range(1,5):
            self.assertIn('SETTING.gSavedSettings.LocalTerrainAsset'+str(index),ids)
            for suffix in ('ScaleU','ScaleV','Rotation','OffsetU','OffsetV'):
                self.assertIn('SETTING.gSavedSettings.LocalTerrainTransform'+str(index)+suffix,ids)
        self.assertIn('SETTING.gSavedSettings.RestrainedLove',ids)

    def test_uniform_or_single_object_cannot_satisfy_world_oracle(self):
        with tempfile.TemporaryDirectory() as d:
            for color in ([100,100,100],[180,40,40],[40,180,40]):
                path=self.image(d,'empty',color*1024,32,32)
                with self.assertRaisesRegex(ValueError,'Missing visible'):
                    baseline.fixture_pixels(path)
            pixels=[]
            for y in range(32):
                for x in range(32): pixels.extend([40,180,40] if x<16 else [180,40,40])
            self.assertGreater(min(baseline.fixture_pixels(self.image(d,'both',pixels,32,32)).values()),32)
            # Object identity survives reference gamma/tone mapping; exact peer
            # color differences still belong to the unmasked pixel comparison.
            mapped=[channel for y in range(32) for x in range(32) for channel in ([109,198,133] if x<16 else [198,109,133])]
            self.assertGreater(min(baseline.fixture_pixels(self.image(d,'mapped',mapped,32,32)).values()),32)

    def test_stale_claimed_artifact_fails_verification(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'baseline-results.json'
            errors=acceptance.verify_files({'artifact_sha256':{'missing.ppm':'claimed'}},path,'source')
            self.assertTrue(any('Changed or missing' in e for e in errors))
            self.assertTrue(any('Build/inventory' in e for e in errors))

    def test_zero_or_negative_timing_is_not_gpu_measurement(self):
        frame={n:0 for n in ('pre_render_cpu_ms','render_submit_cpu_ms','capture_wait_and_io_ms')}
        self.assertIsNone(baseline.metrics([frame])['gpu_ms'])


if __name__ == '__main__': unittest.main()
