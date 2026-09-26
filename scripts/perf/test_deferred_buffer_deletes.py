"""Compile the production retirement code with a recording GL stub.

Run from a Visual Studio developer prompt (cl on PATH). No viewer/GPU needed.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "indra/llrender/llvertexbuffer.cpp").read_text()
start = source.index("static std::vector<GLuint> sDeferredBufferDeletes")
end = source.index("#define ANALYZE_VBO_POOL", start)
production = source[start:end]
shutdown = source[source.index("void LLVertexBuffer::cleanupClass()") :]
shutdown = shutdown[:shutdown.index("//----------------------------------------------------------------------------")]
flush_start = shutdown.index("    for (U32 slot = 0; slot < 4; ++slot)")
flush = shutdown[flush_start:shutdown.rfind("}")]
app = (ROOT / "indra/newview/llappviewer.cpp").read_text()
assert "LLImageGL::updateClass();\n    LLVertexBuffer::updateClass();" in app
assert shutdown.index("delete sVBOPool") < flush_start

test = r'''
#include <cassert>
#include <cstdint>
#include <vector>
using U32 = uint32_t;
using S32 = int32_t;
using GLuint = unsigned;
using GLsizei = int;
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX
struct { bool mInited = true; } gGLManager;
struct LLImageGL { inline static U32 sFrameCount = 0; };
struct LLVertexBuffer { static void updateClass(); };
static std::vector<GLuint> deleted;
static void glDeleteBuffers(GLsizei n, const GLuint* names)
{
    assert(gGLManager.mInited);
    deleted.insert(deleted.end(), names, names+n);
}
''' + production + '\nstatic void shutdown_queue() {\n' + flush + r'''
}
int main()
{
    // A single retirement must finish without another retirement request.
    for (U32 start : {0u, 1u, 2u, 3u, UINT32_MAX})
    {
        LLImageGL::sFrameCount = start;
        GLuint name = 42;
        delete_buffers(1, &name);
        LLVertexBuffer::updateClass();
        assert(deleted.empty()); // never retire the current frame's bucket
        ++LLImageGL::sFrameCount;
        LLVertexBuffer::updateClass();
        assert(deleted == std::vector<GLuint>{42});
        for (int i=0; i<12; ++i)
        {
            ++LLImageGL::sFrameCount;
            LLVertexBuffer::updateClass();
        }
        assert(deleted.size()==1); // empty frames and wrap do not double-delete
        deleted.clear();
    }
    // Multiple requests in one frame are retained, then deleted exactly once.
    GLuint first[] = {1,2}, second[] = {3,4};
    delete_buffers(2, first);
    delete_buffers(2, second);
    assert(deleted.empty());
    ++LLImageGL::sFrameCount;
    LLVertexBuffer::updateClass();
    assert((deleted == std::vector<GLuint>{1,2,3,4}));
    deleted.clear();
    // Shutdown flushes every bucket, including the current frame's names.
    for (U32 i=0; i<4; ++i)
    {
        LLImageGL::sFrameCount=i;
        delete_buffers(1, &first[0]);
    }
    shutdown_queue();
    assert(deleted.size()==4);
    shutdown_queue();
    assert(deleted.size()==4);
    deleted.clear();
    // Lost context: discard stale names without issuing any GL calls.
    delete_buffers(1, &first[0]);
    gGLManager.mInited=false;
    shutdown_queue();
    delete_buffers(1, &second[0]);
    gGLManager.mInited=true;
    shutdown_queue();
    assert(deleted.empty());
}
'''
with tempfile.TemporaryDirectory(prefix="buffer-retirement-") as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(test)
    subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++17", "/W4", "/WX",
                    "test.cpp", "/Fe:test.exe"], cwd=path, check=True)
    subprocess.run([str(path / "test.exe")], cwd=path, check=True)
print("Deferred buffer deletion regression checks passed.")
