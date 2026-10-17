"""Exercise the production native subscriber's shared texture-cache bridge."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NativeTextureCacheTest(unittest.TestCase):
    def test_complete_partial_corrupt_timeout_and_shutdown_reads(self):
        source = (ROOT / 'indra/newview/vsuiimageprovider.cpp').read_text()
        bridge = source[source.index('struct NativeCacheResult'):source.index('struct Image : LLInitParam')]
        fixture = r'''
#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <utility>
using S32 = int;
using LLUUID = int;
template<class T> using LLPointer = std::shared_ptr<T>;
struct LLSD {};
LLSD LLSDMap(const char*, bool) { return {}; }
struct LLEventMailDrop { LLEventMailDrop(const char*, bool) {} void post(LLSD) {} };
namespace LL { struct WorkQueue { static WorkQueue* getInstance(const char*) { static WorkQueue q; return &q; }
 template<class F> void post(F f) { f(); } }; }
struct LLWorkerThread { static int nullHandle() { return 0; } };
constexpr int IMG_CODEC_J2C = 2;
struct LLImageFormatted { int size = 100, codec = IMG_CODEC_J2C;
 int getDataSize() const { return size; } int getCodec() const { return codec; } };
namespace VSResourceBudget { struct Reservation {}; }
struct LLTextureCache {
 using handle_t = int;
 struct ReadResponder { LLPointer<LLImageFormatted> mFormattedImage; int mImageSize = 0;
  virtual ~ReadResponder() = default; virtual void completed(bool) = 0; };
 struct WriteResponder { virtual ~WriteResponder() = default; virtual void completed(bool) = 0; };
 bool success = true, respond = true;
 int totalSize = 100, dataSize = 100, codec = IMG_CODEC_J2C, completions = 0;
 std::unique_ptr<ReadResponder> reader;
 int readFromCache(int, int offset, int limit, ReadResponder* r) {
  assert(offset == 0 && limit == 16 * 1024 * 1024); reader.reset(r);
  r->mFormattedImage = std::make_shared<LLImageFormatted>();
  r->mFormattedImage->size = dataSize; r->mFormattedImage->codec = codec; r->mImageSize = totalSize;
  if (respond) r->completed(success);
  return 1;
 }
 bool readComplete(int handle, bool abort) { assert(handle == 1 && abort); ++completions; return true; }
};
struct LLAppViewer { static LLTextureCache* cache; static LLTextureCache* getTextureCache() { return cache; } };
LLTextureCache* LLAppViewer::cache = nullptr;
std::function<void()> waiting;
namespace llcoro { void suspendUntilEventOnWithTimeout(LLEventMailDrop&, int seconds, LLSD) {
 assert(seconds == 15); if (waiting) waiting(); } }
''' + bridge + r'''
int main() {
 LLTextureCache cache; LLAppViewer::cache = &cache;
 assert(readNativeTexture(1) && cache.completions == 1);
 cache.dataSize = 90; assert(!readNativeTexture(1)); // partial: full-resolution subscriber must fetch
 cache.dataSize = 101; assert(!readNativeTexture(1)); // inconsistent header
 cache.dataSize = 100; cache.codec = 1; assert(!readNativeTexture(1));
 cache.codec = IMG_CODEC_J2C; cache.totalSize = 0; assert(!readNativeTexture(1));
 cache.totalSize = 16 * 1024 * 1024 + 1; assert(!readNativeTexture(1));
 cache.totalSize = 100; cache.success = false; assert(!readNativeTexture(1));
 cache.success = true; cache.respond = false; assert(!readNativeTexture(1));
 const int beforeShutdown = cache.completions;
 waiting = [] { LLAppViewer::cache = nullptr; };
 assert(!readNativeTexture(1) && cache.completions == beforeShutdown);
 // A cache completion arriving after timeout retains only its own mailbox.
 cache.reader->completed(true);
 waiting = {}; assert(!readNativeTexture(1));
 auto encoded = std::make_shared<LLImageFormatted>();
 auto residency = std::make_shared<VSResourceBudget::Reservation>();
 std::weak_ptr<LLImageFormatted> oldImage = encoded;
 std::weak_ptr<VSResourceBudget::Reservation> oldBudget = residency;
 auto writer = std::make_unique<NativeCacheWrite>(encoded, residency);
 encoded.reset(); residency.reset();
 assert(!oldImage.expired() && !oldBudget.expired());
 writer->completed(true); assert(!oldImage.expired());
 writer.reset(); assert(oldImage.expired() && oldBudget.expired());
}
'''
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'cache.cpp'
            exe = Path(directory) / 'cache.exe'
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)],
                                   text=True, capture_output=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=15)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
