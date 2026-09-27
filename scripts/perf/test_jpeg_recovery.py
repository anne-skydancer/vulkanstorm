"""Compile the actual viewer JPEG recovery callbacks with libjpeg, no viewer run.

Run in a VS developer prompt with --packages pointing to autobuild dependencies.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--packages', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = (root / 'indra/llimage/llimagejpeg.cpp').read_text()

def function(name):
    start = source.index('void LLImageJPEG::' + name)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

start = source.index('struct JPEGErrorManager')
end = source.index('};', start) + 2
test = r'''
#include <cstdio>
#include <csetjmp>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
extern "C" {
#include <jpeglib.h>
#include <jerror.h>
}
#define LL_ARM64 0
class LLImageJPEG { public:
    static void errorExit(j_common_ptr);
    static void decodeSkipInputData(j_decompress_ptr,long);
};
''' + source[start:end] + '\n' + function('errorExit') + '\n' + function('decodeSkipInputData') + r'''
std::atomic<unsigned> failures{0},caught{0};
void run()
{
    for(int i=0;i<1000;++i)
    {
        auto c=std::make_unique<jpeg_decompress_struct>();
        auto error=std::make_unique<JPEGErrorManager>();
        c->err=jpeg_std_error(error.get());
        error->error_exit=LLImageJPEG::errorExit;
        error->output_message=[](j_common_ptr){};
        if(setjmp(error->recovery))
        {
            ++caught;
            jpeg_destroy_decompress(c.get());
            continue;
        }
        jpeg_create_decompress(c.get());
        unsigned char input[]={0xff,0xd8,0xff,0xe0,0xff,0xff,0};
        jpeg_mem_src(c.get(),input,sizeof(input));
        c->src->skip_input_data=LLImageJPEG::decodeSkipInputData;
        // Make another worker overwrite a global jump buffer, if one existed.
        std::this_thread::yield();
        if(i%2) LLImageJPEG::decodeSkipInputData(c.get(),1024);
        else LLImageJPEG::errorExit(reinterpret_cast<j_common_ptr>(c.get()));
        ++failures;
        jpeg_destroy_decompress(c.get());
    }
}
int main()
{
    std::vector<std::thread> threads;
    for(int i=0;i<8;++i) threads.emplace_back(run);
    for(auto& t:threads) t.join();
    return failures || caught!=8000;
}
'''
with tempfile.TemporaryDirectory(prefix='jpeg-recovery-') as temporary:
    directory = Path(temporary)
    (directory / 'test.cpp').write_text(test)
    subprocess.run(['cl', '/nologo', '/EHsc', '/std:c++17', '/O2', '/MD',
                    '/I' + str(args.packages / 'include/jpeglib'), 'test.cpp',
                    str(args.packages / 'lib/release/jpeg.lib'), '/Fe:test.exe'], cwd=directory, check=True)
    subprocess.run([str(directory / 'test.exe')], check=True, timeout=30)
print('8,000 concurrent viewer JPEG recovery and truncated-skip checks passed.')
