"""Fault-inject the actual Windows clipboard read/write implementation off hardware."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

class ClipboardTest(unittest.TestCase):
    def test_bounded_contention_and_memory_ownership(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required for clipboard fault injection')
        source = (Path(__file__).resolve().parents[2] / 'indra/llwindow/llwindowwin32.cpp').read_text()
        start = source.index('namespace\n{\nbool openClipboardWithRetry')
        end = source.index('// Constrains the mouse', start)
        implementation = source[start:end]
        fixture = r'''
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>
#define LL_WARNS(tag) std::cerr
#define LL_ENDL std::endl
using HWND = void*; using HGLOBAL = void*; using DWORD = unsigned long;
using WCHAR = wchar_t; using LLWString = std::wstring;
constexpr DWORD ERROR_SUCCESS=0, ERROR_ACCESS_DENIED=5, ERROR_BUSY=170;
constexpr unsigned GMEM_MOVEABLE=2, CF_UNICODETEXT=13;
unsigned opens, waits, waited, closes, frees, emptyCalls; int busy; DWORD error;
bool allocFail, lockFail, setFail, emptyFail, formatAvailable, getFail; unsigned reads, unlocks; std::wstring readText; void* allocated; void* transferred;
void reset() {
 if(transferred) std::free(transferred);
 opens=waits=waited=closes=frees=emptyCalls=0; busy=0; error=5;
 allocFail=lockFail=setFail=emptyFail=false; allocated=transferred=nullptr; formatAvailable=true; getFail=false; reads=unlocks=0; readText=L"Unicode \u03a9\r\nclipboard";
}
bool OpenClipboard(HWND) { ++opens; return opens > unsigned(busy); }
DWORD GetLastError() { return error; }
void Sleep(unsigned ms) { ++waits; waited+=ms; }
bool CloseClipboard() { ++closes; return true; }
bool EmptyClipboard() { ++emptyCalls; return !emptyFail; }
HGLOBAL GlobalAlloc(unsigned,size_t bytes) { return allocated=allocFail ? nullptr : std::malloc(bytes); }
void* GlobalLock(HGLOBAL data) { return lockFail ? nullptr : data; }
bool GlobalUnlock(HGLOBAL) { ++unlocks; return true; }
bool IsClipboardFormatAvailable(unsigned) { return formatAvailable; }
HGLOBAL GetClipboardData(unsigned) { ++reads; return getFail ? nullptr : static_cast<void*>(readText.data()); }
void* GlobalFree(HGLOBAL data) { ++frees; std::free(data); allocated=nullptr; return nullptr; }
void* SetClipboardData(unsigned,HGLOBAL data) { if(setFail) return nullptr; transferred=data; return data; }
struct LLWStringUtil { static void addCRLF(LLWString&) {} static void removeWindowsCR(LLWString& text) { for(size_t i=0;i<text.size();) { if(text[i]==L'\r') text.erase(i,1); else ++i; } } };
template<class T> T ll_convert(const std::wstring& text) { return T(text); }
struct LLWindowWin32 { HWND mWindowHandle=nullptr; bool copyTextToClipboard(const LLWString&); bool pasteTextFromClipboard(LLWString&); };
''' + implementation + r'''
int main() {
 LLWindowWin32 window;
 reset(); busy=3; assert(window.copyTextToClipboard(L"selected favorite"));
 assert(opens==4 && waits==3 && waited==15 && closes==1 && frees==0);
 assert(std::wstring(static_cast<wchar_t*>(transferred))==L"selected favorite");
 reset(); busy=100; assert(!window.copyTextToClipboard(L"busy"));
 assert(opens==8 && waits==7 && waited==35 && closes==0 && emptyCalls==0 && frees==1);
 reset(); busy=100; error=87; assert(!window.copyTextToClipboard(L"invalid"));
 assert(opens==1 && waits==0 && emptyCalls==0 && frees==1);
 reset(); allocFail=true; assert(!window.copyTextToClipboard(L"allocation"));
 assert(opens==0 && frees==0 && emptyCalls==0);
 reset(); lockFail=true; assert(!window.copyTextToClipboard(L"lock"));
 assert(opens==0 && frees==1 && emptyCalls==0);
 reset(); setFail=true; assert(!window.copyTextToClipboard(L"set"));
 assert(closes==1 && frees==1 && transferred==nullptr);
 reset(); emptyFail=true; assert(!window.copyTextToClipboard(L"empty"));
 assert(closes==1 && frees==1 && transferred==nullptr);
 reset(); LLWString pasted=L"unchanged"; busy=3; error=ERROR_BUSY;
 assert(window.pasteTextFromClipboard(pasted));
 assert(pasted==L"Unicode \u03a9\nclipboard" && opens==4 && waits==3 && waited==15);
 assert(reads==1 && unlocks==1 && closes==1 && frees==0 && emptyCalls==0);
 reset(); pasted=L"unchanged"; busy=100;
 assert(!window.pasteTextFromClipboard(pasted) && pasted==L"unchanged");
 assert(opens==8 && waited==35 && reads==0 && closes==0 && frees==0);
 reset(); busy=100; error=87;
 assert(!window.pasteTextFromClipboard(pasted));
 assert(opens==1 && waits==0 && reads==0);
 reset(); formatAvailable=false;
 assert(!window.pasteTextFromClipboard(pasted) && opens==0 && reads==0);
 reset(); getFail=true;
 assert(!window.pasteTextFromClipboard(pasted) && pasted==L"unchanged");
 assert(reads==1 && closes==1 && frees==0 && unlocks==0);
 reset(); lockFail=true;
 assert(!window.pasteTextFromClipboard(pasted) && pasted==L"unchanged");
 assert(reads==1 && closes==1 && frees==0 && unlocks==0);
 reset(); readText.clear();
 assert(window.pasteTextFromClipboard(pasted) && pasted.empty());
 assert(closes==1 && unlocks==1 && frees==0);
 reset();
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'clipboard.cpp'
            executable = Path(directory) / 'clipboard.exe'
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

if __name__ == '__main__':
    unittest.main()
