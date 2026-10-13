"""A cleared document cannot resume layout at an old line's text offset."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class LayoutResetTest(unittest.TestCase):
    def test_segment_replacement_discards_cached_lines_before_reflow(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/llui/lltextbase.cpp').read_text(encoding='utf-8')
        start = source.index('void LLTextBase::clearSegments()')
        end = source.index('\nS32 LLTextBase::getLineStart', start)
        chars_start = source.index('S32 LLNormalTextSegment::getNumChars(')
        chars_end = source.index('\nvoid LLNormalTextSegment::updateLayout', chars_start)
        fixture = r'''
#include <algorithm>
#include <cassert>
#include <limits>
#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>
using S32=int;using F32=float;using LLWString=std::wstring;
template<class T> T llmin(T a,T b) { return std::min(a,b); }
template<class T> T llmax(T a,T b) { return std::max(a,b); }
#define LL_INFOS() std::cerr
#define LL_ENDL std::endl
struct LLUIImagePtr {
    bool notNull() const { return false; }
    const LLUIImagePtr* operator->() const { return this; }
    int getWidth() const { return 0; }
};
struct LLFontGL {
    enum EWordWrapStyle { WORD_BOUNDARY_IF_POSSIBLE,ONLY_WORD_BOUNDARIES };
    int maxDrawableChars(const wchar_t* text,float,int limit,EWordWrapStyle) const {
        if(limit<0) throw std::logic_error("Negative font character count");
        return std::min(limit,int(std::wstring(text).size()));
    }
};
struct Style {
    LLFontGL font;
    LLUIImagePtr getImage() const { return {}; }
    const LLFontGL* getFont() const { return &font; }
};
struct LLTextBase {
    std::vector<int> mSegments{0, 4000};
    std::vector<int> mLineInfoList{0, 4000, 6399};
    int mReflowIndex = 6399;
    void createDefaultSegment() { mSegments.push_back(0); }
    void needsReflow(int index = 0) { mReflowIndex = std::min(mReflowIndex, index); }
    void clearSegments();
    LLWString text;
};
struct LLNormalTextSegment {
    LLTextBase& mEditor;int mStart=0,mEnd=1;Style style;Style* mStyle=&style;
    const LLWString& getWText() const { return mEditor.text; }
    int getLength() const { return int(mEditor.text.size()); }
    int getNumChars(int,int,int,int,int) const;
};
''' + source[start:end] + source[chars_start:chars_end] + r'''
int layout(LLTextBase& editor) {
    // A partial reflow resumes from its cached line, just as the real reflow
    // does. The real segment/font path must consume the empty EOF, never a
    // character limit or text pointer from the previous document.
    const int offset=editor.mLineInfoList.empty()?0:editor.mLineInfoList.back();
    if(offset>int(editor.text.size())) throw std::logic_error("Stale text pointer");
    LLNormalTextSegment segment{editor};
    return segment.getNumChars(100,offset,0,std::numeric_limits<int>::max(),0);
}
int main() {
    LLTextBase editor;
    editor.clearSegments();
    assert(layout(editor)==1); // Exactly one EOF, no stale 6399 offset.
    // Repeat after a completed layout, as when clearing on logout/relogin.
    editor.mLineInfoList = {0, 10};
    editor.mReflowIndex = std::numeric_limits<int>::max();
    editor.clearSegments();
    assert(layout(editor)==1);
    // Prove that the fixture detects the original cached-line failure.
    editor.mLineInfoList={6399};
    bool rejected=false;
    try { layout(editor); } catch(const std::logic_error&) { rejected=true; }
    assert(rejected);
}
'''
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'text_layout.cpp'
            executable = Path(directory) / 'text_layout.exe'
            path.write_text(fixture, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
