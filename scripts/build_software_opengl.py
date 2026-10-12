"""Build a separate pinned llvmpipe reference; never a Vulkan fallback."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
OPTIONS=['--buildtype=debugoptimized','-Dvulkan-drivers=', '-Dgallium-drivers=llvmpipe',
         '-Dplatforms=x11','-Dglx=dri','-Degl=disabled','-Dglvnd=disabled',
         '-Dgles1=disabled','-Dgles2=disabled','-Dopengl=true','-Dllvm=enabled',
         '-Dshared-llvm=enabled','-Dvideo-codecs=','-Dbuild-tests=false']


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work-directory',type=Path,required=True)
    p.add_argument('--jobs',type=int,default=3)
    args=p.parse_args()
    if platform.system()!='Linux': raise SystemExit('Pinned GL reference is a Linux/Xvfb correctness job.')
    if not 1<=args.jobs<=64: raise ValueError('Invalid build parallelism')
    pin=json.loads((ROOT/'scripts/vulkan_ci_dependencies.json').read_text())['lavapipe']
    work=args.work_directory.resolve();work.mkdir(parents=True,exist_ok=True)
    archive=work/'mesa.tar.xz'
    if not archive.exists(): urllib.request.urlretrieve(pin['url'],archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=pin['sha256']:
        raise RuntimeError('Mesa reference archive checksum mismatch')
    source=work/'source/mesa-25.2.4'
    if not source.exists():
        with tarfile.open(archive) as package: package.extractall(work/'source',filter='data')
    source_hashes={str(f.relative_to(source)):hashlib.sha256(f.read_bytes()).hexdigest()
                   for f in source.rglob('*') if f.is_file()}
    source_manifest=work/'source-sha256.json'
    if source_manifest.exists() and json.loads(source_manifest.read_text())!=source_hashes:
        raise RuntimeError('Reference source tree changed since verified archive extraction')
    source_manifest.write_text(json.dumps(source_hashes,sort_keys=True)+'\n')
    build=work/'build';install=work/'install'
    option_manifest=work/'build-options.json'
    if option_manifest.exists() and json.loads(option_manifest.read_text())!=OPTIONS:
        raise RuntimeError('Reference build options changed; use a fresh dependency build directory')
    option_manifest.write_text(json.dumps(OPTIONS)+'\n')
    if not (build/'build.ninja').is_file():
        subprocess.run(['meson','setup',str(build),str(source),f'--prefix={install}',*OPTIONS],check=True)
    subprocess.run(['meson','compile','-C',str(build),'-j',str(args.jobs)],check=True)
    subprocess.run(['meson','install','-C',str(build)],check=True)
    gl=list(install.rglob('libGL.so.1'));dri=list(install.rglob('swrast_dri.so'))
    if len(gl)!=1 or len(dri)!=1: raise RuntimeError('Incomplete pinned OpenGL install')
    tools={name:subprocess.check_output([name,'--version'],text=True).splitlines()[0]
           for name in ('meson','ninja','llvm-config-18',os.environ.get('CC','gcc'))}
    libraries={str(f.relative_to(install)):hashlib.sha256(f.read_bytes()).hexdigest()
               for f in install.rglob('*') if f.is_file() and '.so' in f.name}
    licenses={str(f.relative_to(source)):hashlib.sha256(f.read_bytes()).hexdigest()
              for f in source.rglob('*') if f.is_file() and f.name.upper().startswith(('LICENSE','COPYING','NOTICE'))}
    for relative in licenses:
        target=install/'licenses'/relative;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source/relative,target)
    report=dict(schema=1,source=pin,options=OPTIONS,toolchain=tools,
        library_directory=str(gl[0].parent),dri_directory=str(dri[0].parent),
        install=str(install),libraries=libraries,licenses=licenses,
        mesa_version='25.2.4',scope='Separate software OpenGL reference only; no Vulkan fallback.')
    (work/'runtime.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: pinned independent software OpenGL runtime staged')


if __name__=='__main__': main()
