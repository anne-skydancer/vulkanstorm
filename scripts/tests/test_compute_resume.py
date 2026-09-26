"""Compile advanceJob's production preflight and test changes between frames.

An optional source path permits running the same regression against an older
llcomputelod.cpp. No GL context or viewer login is needed.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
source = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'indra/newview/llcomputelod.cpp'
text = source.read_text()
start = text.index('BuildResult advanceJob(BuildJob& job)')
end = text.index('        U32 previous_levels = 0;', start)
# Keep the actual entry checks and initialization-time validation. Stop before
# mesh allocation: this fixture tests whether it is safe to reach that phase.
production = text[start:end] + '    }\n    return BuildResult::MORE;\n}\n'
fixture = r'''
#include <memory>
#include <cstdlib>
#include <vector>
#include <iostream>
using U32 = unsigned;
using U64 = unsigned long long;
using S32 = int;
using std::size_t;
struct LLVertexBuffer { enum { MAP_WEIGHT4 = 1 }; U32 mask = MAP_WEIGHT4; U32 getTypeMask() const { return mask; } };
struct LLGLTFMaterial { enum { ALPHA_MODE_OPAQUE = 0 }; int mAlphaMode = ALPHA_MODE_OPAQUE; };
struct TE {
    bool ready = true; float glow = 0; LLGLTFMaterial material;
    bool isGLTFRenderMaterialReady() const { return ready; }
    LLGLTFMaterial* getGLTFRenderMaterial() { return &material; }
    float getGlow() const { return glow; }
};
struct LLFace {
    enum { TEXTURE_ANIM = 1 };
    LLVertexBuffer* buffer = nullptr; bool animated = false, media = false;
    void* mAvatar = this; void* mSkinInfo = this;
    LLVertexBuffer* getVertexBuffer() { return buffer; }
    bool isState(int) { return animated; } bool hasMedia() { return media; }
};
struct LLDrawable {
    enum { RIGGED = 1, REBUILD_ALL = 2 }; unsigned state = 0;
    LLFace* face = nullptr;
    bool isState(unsigned flag) { return (state & flag) != 0; }
    int getNumFaces() { return 1; } LLFace* getFace(int) { return face; }
};
struct Volume { bool isMeshAssetLoaded() { return true; } int getNumVolumeFaces() { return 1; } };
namespace LLComputeMesh {
    enum { SKIN = 1, DRAWABLE = 2, MESH = 4, MATERIAL = 8 };
    struct Object { U64 dependency_epoch = 0; U32 requested_lod = 0; std::vector<bool> faces{true}; };
}
struct LLVOVolume {
    std::shared_ptr<LLComputeMesh::Object> mComputeLOD;
    LLDrawable* mDrawable = nullptr; TE te; Volume volume; bool dead = false;
    bool isDead() { return dead; } void* getSkinInfo() { return this; }
    Volume* getVolume() { return &volume; } TE* getTE(int) { return &te; }
};
bool eligible(LLVOVolume& object) { return object.mDrawable != nullptr; }
struct BuildJob {
    std::shared_ptr<LLVOVolume> object;
    std::weak_ptr<LLComputeMesh::Object> owner;
    bool initialized = false; unsigned waiting = 0;
    U64 prepared_epoch = 0; U32 target = 0, ready_mask = 0, failed_mask = 0;
};
enum class BuildResult { WAIT, MORE, DONE, DROP, RESOURCE_LIMIT };
'''
tests = r'''
int main() {
    auto object = std::make_shared<LLVOVolume>();
    auto owner = std::make_shared<LLComputeMesh::Object>();
    object->mComputeLOD = owner;
    LLVertexBuffer buffer; LLFace face; face.buffer = &buffer;
    LLDrawable drawable; drawable.face = &face; object->mDrawable = &drawable;
    BuildJob job; job.object = object; job.owner = owner;
    auto expect = [&](BuildResult result, unsigned dependency = 0) {
        job.waiting = 0;
        if (advanceJob(job) != result || job.waiting != dependency) {
            std::cerr << "Unexpected preflight result (initialized=" << job.initialized << ")\n";
            std::exit(1);
        }
    };
    expect(BuildResult::MORE);
    job.initialized = true; // the next frame must revalidate live dependencies
    face.buffer = nullptr;
    expect(BuildResult::WAIT, LLComputeMesh::DRAWABLE);
    face.buffer = &buffer;
    expect(BuildResult::MORE);
    drawable.face = nullptr;
    expect(BuildResult::WAIT, LLComputeMesh::DRAWABLE);
    drawable.face = &face;
    drawable.state = LLDrawable::REBUILD_ALL;
    expect(BuildResult::WAIT, LLComputeMesh::DRAWABLE);
    drawable.state = LLDrawable::RIGGED;
    face.mAvatar = nullptr;
    expect(BuildResult::WAIT, LLComputeMesh::SKIN | LLComputeMesh::DRAWABLE);
    face.mAvatar = &face; buffer.mask = 0;
    expect(BuildResult::WAIT, LLComputeMesh::SKIN | LLComputeMesh::DRAWABLE);
    buffer.mask = LLVertexBuffer::MAP_WEIGHT4;
    expect(BuildResult::MORE);
    drawable.state = 0; face.media = true;
    expect(BuildResult::DROP);
    face.media = false; object->te.ready = false;
    expect(BuildResult::WAIT, LLComputeMesh::MATERIAL);
    object->te.ready = true; owner->faces[0] = false;
    expect(BuildResult::DROP);
    owner->faces[0] = true; object->dead = true;
    expect(BuildResult::DROP);
    object->dead = false;
    expect(BuildResult::MORE);
    std::cout << "Resumed mesh preparation revalidates drawable, buffer, skin and material dependencies.\n";
}
'''
with tempfile.TemporaryDirectory(prefix='compute-resume-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(fixture + production + tests)
    (path / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(compute_resume LANGUAGES CXX)\n'
        'add_executable(compute_resume test.cpp)\ntarget_compile_features(compute_resume PRIVATE cxx_std_17)\n')
    subprocess.run(['cmake', '-S', str(path), '-B', str(path / 'build')], check=True)
    subprocess.run(['cmake', '--build', str(path / 'build'), '--config', 'Debug'], check=True)
    executable = path / 'build' / ('Debug/compute_resume.exe' if sys.platform == 'win32' else 'compute_resume')
    subprocess.run([str(executable)], check=True)
