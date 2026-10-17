"""Execute actual shared currency UI updates against both real host schemas."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest,xml.etree.ElementTree as ET
from native_source_fixture import ROOT,HERE,method

class CurrencyUIContract(unittest.TestCase):
 def test_real_hosts_zero_nonzero_international_and_hidden_states(self):
  source=(ROOT/'indra/newview/llcurrencyuimanager.cpp').read_text(encoding='utf-8-sig')
  methods='\n'.join(method(source,s) for s in [
   'void LLCurrencyUIManager::Impl::prepare()',
   'void LLCurrencyUIManager::Impl::updateUI()',
   'void LLCurrencyUIManager::setAmount('])
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  for host in ('buy_land','buy_currency'):
   root=ET.parse(ROOT/f'indra/newview/skins/default/xui/en/floater_{host}.xml').getroot()
   literal=lambda text:'R"contract('+text+')contract"'
   fields={node.get('name'):''.join(node.itertext()).strip() for node in root.iter() if node.get('name')}
   initialization='\n'.join(' panel.add('+literal(name)+','+literal(text)+');' for name,text in fields.items())
   initialization+='\n'+'\n'.join(' panel.strings['+literal(node.get('name'))+']='+literal(''.join(node.itertext()).strip())+';' for node in root if node.tag in ('floater.string','string'))
   fixture=(HERE/'native_currency_ui_contract_fixture.cpp').read_text().replace('METHODS',methods).replace('HOST_SCHEMA',initialization)
   with self.subTest(host=host), tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'ui.cpp';exe=Path(tmp)/'ui.exe';cpp.write_text(fixture,encoding='utf-8')
    result=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    result=subprocess.run([str(exe)],capture_output=True,text=True)
    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
