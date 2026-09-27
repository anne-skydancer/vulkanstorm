"""Exercise production avatar retention with deterministic ownership, time and distance."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]

def method(path, signature):
    text = (ROOT/path).read_text()
    begin = text.index(signature)
    brace = text.index('{', begin)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[begin:end]
fixture = r'''
#include <algorithm>
#include <cassert>
#include <vector>
using U32=unsigned; using S32=int; using F32=float; using F64=double;
template<class T> T llmax(T a,T b) { return std::max(a,b); }
float testDrawDistance=128.f;
int gSavedSettings=0;
template<class T> struct LLCachedControl {
    LLCachedControl(int&,const char*,T) {}
    operator T() const { return T(testDrawDistance); }
};
struct Position {
    float x=0,y=0,z=0;
    Position operator-(const Position& other) const { return {x-other.x,y-other.y,z-other.z}; }
    float lengthSquared() const { return x*x+y*y+z*z; }
};
struct Agent { Position position; Position getPositionAgent() const { return position; } } gAgent;
struct LLTimer { static inline double now=0; static double getTotalSeconds() { return now; } };
namespace LLRender { constexpr U32 NUM_TEXTURE_CHANNELS=4; }
struct LLVOAvatar {
    bool self=false,dead=false;
    Position position{1000,0,0};
    bool isSelf() const { return self; }
    bool isDead() const { return dead; }
    Position getPositionAgent() const { return position; }
};
struct LLViewerObject {
    bool dead=false, attachment=false;
    LLViewerObject* root=this;
    LLVOAvatar* avatar=nullptr;
    LLVOAvatar* directAvatar=nullptr;
    LLVOAvatar* asAvatar() { return directAvatar; }
    bool isDead() const { return dead; }
    bool isAttachment() const { return attachment; }
    LLViewerObject* getRootEdit() { return root; }
    LLVOAvatar* getAvatarAncestor() { return avatar; }
};
struct LLFace { LLViewerObject* object=nullptr; LLViewerObject* getViewerObject() { return object; } };
struct LLViewerFetchedTexture {
    std::vector<LLFace*> faces[LLRender::NUM_TEXTURE_CHANNELS];
    bool lowMemory=false;
    bool isSystemMemoryLow() const { return lowMemory; }
    int getNumFaces(U32 channel) const { return int(faces[channel].size()); }
    const std::vector<LLFace*>* getFaceList(U32 channel) const { return &faces[channel]; }
    bool retainAvatarDetail();
    F64 mAvatarDetailRetentionStarted=-1.;
    struct { int mTexName=0; } image;
    bool hasGLTexture() const { return image.mTexName != 0; }
};
'''
production = method('indra/newview/llviewertexture.cpp', 'bool LLViewerFetchedTexture::retainAvatarDetail')
tests = r'''
int main() {
    LLVOAvatar self{true}, other{false};
    LLViewerObject attachment; attachment.attachment=true; attachment.avatar=&self;
    LLViewerObject child; child.root=&attachment; child.avatar=&self;
    LLFace face{&child};
    LLViewerFetchedTexture worn; worn.image.mTexName=10;
    worn.faces[3].push_back(&face); // child prim's material channel
    assert(worn.retainAvatarDetail());
    worn.lowMemory=true; assert(!worn.retainAvatarDetail()); worn.lowMemory=false;
    child.avatar=&other; assert(!worn.retainAvatarDetail()); child.avatar=&self;
    attachment.attachment=false; assert(!worn.retainAvatarDetail()); attachment.attachment=true;
    child.dead=true; assert(!worn.retainAvatarDetail()); child.dead=false;
    worn.image.mTexName=0; assert(!worn.retainAvatarDetail());
    worn.image.mTexName=10; child.avatar=&other;
    other.position={10,0,0}; LLTimer::now=100;
    assert(worn.retainAvatarDetail());
    LLTimer::now=159; assert(worn.retainAvatarDetail());
    LLTimer::now=160; assert(!worn.retainAvatarDetail()); // retries do not renew
    worn.mAvatarDetailRetentionStarted=-1.; other.position={15,0,0}; LLTimer::now=200;
    assert(worn.retainAvatarDetail()); LLTimer::now=230; assert(!worn.retainAvatarDetail());
    other.position={20,0,0}; assert(!worn.retainAvatarDetail());
    other.position={9,0,0}; LLTimer::now=300; assert(worn.retainAvatarDetail());
    other.position={15,0,0}; LLTimer::now=331; assert(!worn.retainAvatarDetail()); // band changes share start
    testDrawDistance=8; other.position={9,0,0}; assert(!worn.retainAvatarDetail());
    testDrawDistance=128; assert(worn.retainAvatarDetail());
    worn.lowMemory=true; assert(!worn.retainAvatarDetail()); worn.lowMemory=false;
    other.dead=true; assert(!worn.retainAvatarDetail()); other.dead=false;
    gAgent.position={100,0,0}; other.position={109,0,0}; assert(worn.retainAvatarDetail());
    gAgent.position={0,0,0}; assert(!worn.retainAvatarDetail()); // avatar-relative, not camera-relative
    LLViewerObject body; body.directAvatar=&other; LLFace bodyFace{&body};
    other.position={5,0,0}; worn.faces[0].push_back(&bodyFace);
    assert(worn.retainAvatarDetail()); // avatar body, not only attachments
    LLTimer::now+=45; assert(worn.retainAvatarDetail());
    worn.faces[0].clear(); other.position={15,0,0}; assert(!worn.retainAvatarDetail());
    LLVOAvatar nearAvatar; nearAvatar.position={5,0,0};
    body.directAvatar=&nearAvatar; worn.faces[0].push_back(&bodyFace);
    assert(worn.retainAvatarDetail()); // strongest owner wins across channels
    worn.faces[0].clear(); assert(!worn.retainAvatarDetail());
    worn.faces[3].clear(); worn.faces[0].push_back(nullptr);
    assert(!worn.retainAvatarDetail()); // removed owners and null faces
}
'''
with tempfile.TemporaryDirectory(prefix='avatar-retention-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(fixture + production + tests)
    (path / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(avatar_retention LANGUAGES CXX)\n'
        'add_executable(avatar_retention test.cpp)\ntarget_compile_features(avatar_retention PRIVATE cxx_std_17)\n')
    subprocess.run(['cmake', '-S', str(path), '-B', str(path / 'build')], check=True)
    subprocess.run(['cmake', '--build', str(path / 'build'), '--config', 'Debug'], check=True)
    executable = path / 'build' / ('Debug/avatar_retention.exe' if sys.platform == 'win32' else 'avatar_retention')
    subprocess.run([str(executable)], check=True)
print('Avatar distance-band retention passed.')
