from pathlib import Path
import re,shutil,subprocess
from native_source_fixture import method
root=Path(__file__).resolve().parents[2];here=root/'scripts/tests'
def clean(path):return '\n'.join(line for line in path.read_text(encoding='utf-8').splitlines() if not line.startswith(('#include','#pragma')))
sources={'PROPERTIES_HEADER':clean(root/'indra/newview/vsobjectproperties.h'),'PROPERTIES_SOURCE':clean(root/'indra/newview/vsobjectproperties.cpp'),'SELECTION_HEADER':clean(root/'indra/newview/vsobjectselection.h'),'SELECTION_SOURCE':clean(root/'indra/newview/vsobjectselection.cpp')}
# This fixture executes the original transport/properties/failure-retention
# surface. Full admission and native UI node semantics are compiled separately
# by test_native_world_selection and test_native_selection_nodes respectively.
controller=sources['SELECTION_SOURCE']
prefix=controller[:controller.index('VSObjectSelection::VSObjectSelection(')]
signatures=['VSObjectSelection::VSObjectSelection(', 'VSObjectSelection::~VSObjectSelection(',
 'bool VSObjectSelection::select(', 'std::vector<VSWorldScene::ObjectHandle> VSObjectSelection::selected(',
 'std::optional<VSObjectSelection::Publication> VSObjectSelection::properties(',
 'std::function<bool()> VSObjectSelection::selectionGuard(', 'boost::signals2::connection VSObjectSelection::changed(',
 'bool VSObjectSelection::requestFamily(', 'bool VSObjectSelection::beginHighlightRequest(',
 'bool VSObjectSelection::highlight(', 'std::vector<VSWorldScene::ObjectHandle> VSObjectSelection::highlights(',
 'void VSObjectSelection::clearHighlights(', 'void VSObjectSelection::receive(',
 'void VSObjectSelection::tick(', 'void VSObjectSelection::reset(', 'void VSObjectSelection::requestHoverProperties(']
sources['SELECTION_SOURCE']=prefix+'\n'.join(method(controller,signature)for signature in signatures)
text=(here/'object_selection_fixture.cpp').read_text(encoding='utf-8')
for key,value in sources.items():text=text.replace(key,value)
names=sorted(set(re.findall(r'_PREHASH_\w+',text)))
text=text.replace('\nPREHASH\n','\n'.join('constexpr const char* '+name+'="'+name.removeprefix('_PREHASH_')+'";' for name in names))
import tempfile
with tempfile.TemporaryDirectory() as temporary:
 cpp=Path(temporary)/'selection_check.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(text,encoding='utf-8')
 subprocess.run([shutil.which('clang++') or shutil.which('g++'),'-std=c++20','-I',str(root/'indra/newview'),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe.resolve())],check=True)
