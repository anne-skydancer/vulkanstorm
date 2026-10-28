"""Actual immediate painter, original geometry and publication lifetime tests."""
from pathlib import Path
import shutil,subprocess,tempfile,sys,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
sys.path.insert(0,str(HERE));from native_source_fixture import method
class ImmediateGeometry(unittest.TestCase):
 def compile(self,source):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  packages=next((p for p in [ROOT/'build-vc180-64/packages/include',ROOT/'build-linux-x86_64/packages/include']if(p/'glm/glm.hpp').is_file()),None)
  self.assertIsNotNone(packages,'Autobuild GLM dependency is required')
  with tempfile.TemporaryDirectory()as temp:
   out=Path(temp);cpp=out/'source.cpp';exe=out/'source.exe';cpp.write_text(source)
   built=subprocess.run([compiler,'-std=c++17','-isystem',str(packages),'-I',str(ROOT/'indra/newview'),'-I',str(ROOT/'indra/llrender'),str(cpp),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
 def test_original_sphere_cone_matrix_state_clipping_and_charged_refusal(self):
  fixture=(HERE/'fixtures/native_immediate_geometry.cpp').read_text().replace('POLAR_REFERENCE',(HERE/'fixtures/native_immediate_sphere_original.cpp').read_text()).replace('CONE_REFERENCE',(HERE/'fixtures/native_immediate_cone_original.cpp').read_text()).replace('MATRIX_REFERENCE',(HERE/'fixtures/native_immediate_matrix_original.cpp').read_text()).replace('COLOR_REFERENCE',(HERE/'fixtures/native_immediate_color_original.cpp').read_text())
  self.compile(fixture)
 def test_actual_selected_node_visitor_no_copy_and_reentrant_retirement(self):
  source=(ROOT/'indra/newview/vsobjectselection.cpp').read_text()
  self.compile((HERE/'fixtures/native_immediate_selection_visit.cpp').read_text().replace('ACTUAL',method(source,'bool VSObjectSelection::visitSelected(')))
 def test_actual_packet_publisher_retains_only_current_complete_geometry(self):
  header=(ROOT/'indra/newview/vsworldpaint.h').read_text();cpp=(ROOT/'indra/newview/vsworldpaint.cpp').read_text();world=(ROOT/'indra/newview/vsworldrenderer.h').read_text()
  strip=lambda text:'\n'.join(l for l in text.splitlines()if not l.startswith('#include')and l!='#pragma once')
  vertex=method(world,'    struct Vertex')+';'
  begin=world.index('enum class DepthCompare');depth=world[begin:world.index(';',begin)+1]
  fixture=(HERE/'fixtures/native_immediate_publisher.cpp').read_text().replace('VERTEX',vertex).replace('DEPTH',depth).replace('HEADER',strip(header)).replace('CPP',strip(cpp))
  self.compile(fixture)
if __name__=='__main__':unittest.main()
