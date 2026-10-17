from pathlib import Path
import re,shutil,subprocess
root=Path(__file__).resolve().parents[2];here=root/'scripts/tests'
def clean(path):return '\n'.join(line for line in path.read_text(encoding='utf-8').splitlines() if not line.startswith(('#include','#pragma')))
sources={'PROPERTIES_HEADER':clean(root/'indra/newview/vsobjectproperties.h'),'PROPERTIES_SOURCE':clean(root/'indra/newview/vsobjectproperties.cpp'),'SELECTION_HEADER':clean(root/'indra/newview/vsobjectselection.h'),'SELECTION_SOURCE':clean(root/'indra/newview/vsobjectselection.cpp')}
text=(here/'object_selection_fixture.cpp').read_text(encoding='utf-8')
for key,value in sources.items():text=text.replace(key,value)
names=sorted(set(re.findall(r'_PREHASH_\w+',text)))
text=text.replace('\nPREHASH\n','\n'.join('constexpr const char* '+name+'="'+name.removeprefix('_PREHASH_')+'";' for name in names))
import tempfile
with tempfile.TemporaryDirectory() as temporary:
 cpp=Path(temporary)/'selection_check.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(text,encoding='utf-8')
 subprocess.run([shutil.which('clang++') or shutil.which('g++'),'-std=c++20','-I',str(root/'indra/newview'),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
