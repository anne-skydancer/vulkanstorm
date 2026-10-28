import csv, hashlib, json, pathlib, re, subprocess
from inventory_helpers import mask_source, shader_interfaces, gl_candidates
root=pathlib.Path.cwd()
base='1a490c3cb7ed60124169bf4bf6ad61a6ae1eeec5'
if subprocess.check_output(['git','diff',base,'--','indra'],text=True):
    raise SystemExit('Tracked indra sources differ from pinned audit baseline; use its checkout or start a new revision audit.')
paths=subprocess.check_output(['git','ls-files','indra'],text=True).splitlines()
cpp={p:(root/p).read_text(encoding='utf-8',errors='replace') for p in paths if p.endswith(('.cpp','.h','.mm'))}
def strip(s):
    return mask_source(s)
clean={p:strip(s) for p,s in cpp.items()}
out=root/'doc/vulkan';out.mkdir(parents=True,exist_ok=True)
shaders=[]
for p in paths:
    if not p.endswith('.glsl'):continue
    s=(root/p).read_text(encoding='utf-8',errors='replace'); c=strip(s)
    rel=p.split('/shaders/')[1]; parts=rel.split('/'); name='/'.join(parts[1:]) if len(parts)>1 else rel
    refs=[]
    for q,t in clean.items():
        for m in re.finditer(re.escape('"'+name+'"'),t):
            refs.append(q+':'+str(t[:m.start()].count('\n')+1))
    family=parts[1] if len(parts)>2 else 'shared-header'
    shaders.append({'path':p,'sha256':hashlib.sha256((root/p).read_bytes()).hexdigest(),
        'class':parts[0] if len(parts)>1 else 'shared', 'family':family,
        'direct_cpp_literals':';'.join(refs),
        'interface_declarations':' | '.join(shader_interfaces(s)),
        'conditional_macros':';'.join(sorted(set(macro.strip() for macro in re.findall(r'^\s*#\s*(?:if|ifdef|ifndef|elif)\s+([^\n]*)',c,re.M)))),
        'coverage':'lexical global declarations; family contract in shader-contracts.md; permutation parity unmeasured'})
with (out/'shader-inventory.csv').open('w',encoding='utf-8',newline='') as f:
    w=csv.DictWriter(f,fieldnames=list(shaders[0]));w.writeheader();w.writerows(shaders)
glfiles=[]
commands=set((out/'gl-api-symbols.txt').read_text(encoding='utf-8').splitlines())
wrapper_headers=['indra/llrender/llgl.h','indra/llrender/llglstates.h','indra/llrender/llglslshader.h','indra/llrender/llgltexture.h']
wrappers=set(re.findall(r'\bclass\s+(LLGL\w+)\b','\n'.join(clean[p] for p in wrapper_headers)))
for p,c in cpp.items():
    calls,has_wrapper=gl_candidates(c,commands,wrappers)
    if not calls and not has_wrapper:continue
    glfiles.append({'path':p,'direct_gl_symbols':';'.join(calls),
       'coverage':'inventory candidate; see ledger/gaps; symbol presence does not prove execution'})
with (out/'gl-source-inventory.csv').open('w',encoding='utf-8',newline='') as f:
    w=csv.DictWriter(f,fieldnames=list(glfiles[0]));w.writeheader();w.writerows(glfiles)
routes=[]
p='indra/newview/llviewershadermgr.cpp';s=cpp[p]
for n,line in enumerate(s.splitlines(),1):
    if any(k in line for k in ['mShaderFiles.push_back','addPermutation(','addPermutations(','mFeatures.']):
        if line.lstrip().startswith('//'):continue
        routes.append({'path':p,'line':n,'registration_or_variant':line.strip()})
with (out/'shader-registration.csv').open('w',encoding='utf-8',newline='') as f:
    w=csv.DictWriter(f,fieldnames=list(routes[0]));w.writeheader();w.writerows(routes)
summary={'baseline':base,'shader_files':len(shaders),'shader_families':{k:sum(s['family']==k for s in shaders) for k in sorted(set(s['family'] for s in shaders))},'GL_candidate_source_files':len(glfiles),'shader_registration_rows':len(routes),'draw_pool_cpp':len([p for p in paths if re.match(r'indra/newview/lldrawpool[^/]*\.cpp$',p)]),'inventory_scope':'tracked indra; registry-validated GL candidates and global GLSL declarations; lexical only, not preprocessing or control-flow analysis'}
(out/'inventory-summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
print(json.dumps(summary,indent=2))
