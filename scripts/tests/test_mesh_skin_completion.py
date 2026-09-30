"""Exercise production mesh/skin completion callbacks in both arrival orders.

Uses a small scheduler fixture, without a GL context or viewer login. An optional
llvovolume.cpp path allows verifying that the regression fails before the fix.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
source = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'indra/newview/llvovolume.cpp'
text = source.read_text()

def method(name):
    start = text.index('void LLVOVolume::' + name + '()')
    end = text.index('\n}\n', start) + 3
    return text[start:end]

fixture = r'''
#include <cstdlib>
#include <iostream>
using LLUUID = int;
struct LLDrawable { enum { REBUILD_GEOMETRY = 1 }; bool queued = false; };
struct Volume {
    Volume& getParams() { return *this; }
    LLUUID getSculptID() { return 1; }
};
struct LLVOAvatar {
    void addAttachmentOverridesForObject(void*) {}
    void notifyAttachmentMeshLoaded() {}
};
using LLControlAvatar = LLVOAvatar;
struct LLVOVolume {
    bool mSkinInfoUnavaliable = false, mSculptChanged = false;
    void* mSkinInfo = nullptr;
    bool geometryLoaded = false, visible = false;
    LLDrawable drawable;
    LLDrawable* mDrawable = &drawable;
    Volume volume;
    Volume* getVolume() { return &volume; }
    LLVOAvatar* getAvatar() { return nullptr; }
    LLControlAvatar* getControlAvatar() { return nullptr; }
    bool isAnimatedObject() { return false; }
    void updateVisualComplexity() {}
    void notifyMeshLoaded();
    void notifySkinInfoUnavailable();
    void drain() {
        if (!drawable.queued) return;
        drawable.queued = false;
        visible = geometryLoaded && (mSkinInfo || mSkinInfoUnavaliable);
    }
};
struct Pipeline {
    void markRebuild(LLDrawable* drawable, int) { if (drawable) drawable->queued = true; }
} gPipeline;
struct MeshRepo { bool hasSkinInfo(LLUUID) { return true; } } gMeshRepo;
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
'''
tests = r'''
int main() {
    LLVOVolume meshFirst;
    meshFirst.geometryLoaded = true;
    meshFirst.notifyMeshLoaded();
    meshFirst.drain();
    require(!meshFirst.visible, "Pending skin should defer drawing");
    meshFirst.notifySkinInfoUnavailable();
    meshFirst.drain();
    require(meshFirst.visible, "Skin completion must wake geometry skipped while waiting");

    LLVOVolume skinFirst;
    skinFirst.notifySkinInfoUnavailable();
    skinFirst.drain();
    require(!skinFirst.visible, "Missing geometry must still wait for mesh completion");
    skinFirst.geometryLoaded = true;
    skinFirst.notifyMeshLoaded();
    skinFirst.drain();
    require(skinFirst.visible, "Mesh arrival must render after earlier skin completion");

    LLVOVolume noDrawable;
    noDrawable.mDrawable = nullptr;
    noDrawable.notifySkinInfoUnavailable();
    require(noDrawable.mSkinInfoUnavaliable, "Completion must also resolve objects without drawables");
    std::cout << "Mesh/skin completion arrival-order regression passed.\n";
}
'''
with tempfile.TemporaryDirectory(prefix='mesh-skin-completion-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(fixture + method('notifyMeshLoaded') + method('notifySkinInfoUnavailable') + tests)
    (path / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(mesh_skin_completion LANGUAGES CXX)\n'
        'add_executable(mesh_skin_completion test.cpp)\n'
        'target_compile_features(mesh_skin_completion PRIVATE cxx_std_17)\n')
    subprocess.run(['cmake', '-S', str(path), '-B', str(path / 'build')], check=True)
    subprocess.run(['cmake', '--build', str(path / 'build'), '--config', 'Debug'], check=True)
    executable = path / 'build' / ('Debug/mesh_skin_completion.exe' if sys.platform == 'win32' else 'mesh_skin_completion')
    subprocess.run([str(executable)], check=True)
