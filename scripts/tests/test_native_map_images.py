"""Compile map URL/cache ownership and mipmap controllers without network or a GPU."""
from pathlib import Path
import importlib.util
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent

def sources():
    real=(ROOT/"indra/newview/vsuiimageprovider.h").read_text(encoding="utf-8-sig")
    if "VSMapImage requestMapImage" in real:
        return {f"indra/newview/{name}":(ROOT/"indra/newview"/name).read_text(encoding="utf-8-sig") for name in ("vsuiimageprovider.cpp","llworldmipmap.cpp","llworldmapview.cpp")}
    spec=importlib.util.spec_from_file_location("map_proposal",HERE/"prepare_map_frontends.py")
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module.plan()

def full_method(source,signature):
    begin=source.index(signature);start=source.index("{",begin);depth=0
    for end in range(start,len(source)):
        if source[end]=="{":depth+=1
        elif source[end]=="}":
            depth-=1
            if depth==0:return source[begin:end+1]
    raise AssertionError(signature)

class MapImagesTest(unittest.TestCase):
    def test_actual_url_subscription_and_mipmap_methods(self):
        compiler=shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler,"A C++ compiler is required")
        changed=sources();provider=changed["indra/newview/vsuiimageprovider.cpp"];mipmap=changed["indra/newview/llworldmipmap.cpp"]
        fixture=(HERE/"native_map_images_fixture.cpp").read_text(encoding="utf-8-sig")
        fixture=fixture.replace("PROVIDER_METHODS","\n".join(full_method(provider,sig) for sig in ("VSMapImage VSUIImageProvider::requestMapImage(","void VSUIImageProvider::retryMapImage(")))
        fixture=fixture.replace("MIPMAP_METHODS","\n".join(full_method(mipmap,sig) for sig in ("std::string LLWorldMipmap::objectsTileURL(","VSMapImage LLWorldMipmap::getNativeObjectsTile(")))
        begin=provider.index("    const S32 queued=fetch->createRequest(");end=provider.index(";",begin)+1
        fixture=fixture.replace("QUEUE_EXPRESSION",provider[begin:end])
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/"map.cpp";binary=Path(directory)/"map.exe";source.write_text(fixture,encoding="utf-8")
            built=subprocess.run([compiler,"-std=c++17",str(source),"-o",str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_actual_shared_tile_grid_and_native_draw_geometry(self):
        compiler=shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler,"A C++ compiler is required")
        changed=sources()
        fixture=(HERE/"native_map_geometry_fixture.cpp").read_text(encoding="utf-8-sig")
        fixture=fixture.replace("MIPMAP_GRID_METHOD",full_method(changed["indra/newview/llworldmipmap.cpp"],"void LLWorldMipmap::globalToMipmap("))
        fixture=fixture.replace("DRAW_LEVEL_METHOD",full_method(changed["indra/newview/llworldmapview.cpp"],"bool LLWorldMapView::drawMipmapLevel("))
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/"geometry.cpp";binary=Path(directory)/"geometry.exe";source.write_text(fixture,encoding="utf-8")
            built=subprocess.run([compiler,"-std=c++17",str(source),"-o",str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

if __name__=="__main__":unittest.main()
