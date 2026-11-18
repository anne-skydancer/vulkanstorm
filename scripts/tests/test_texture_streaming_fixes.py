# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Text guards for the texture streaming fixes of 2026-11-18 (the rules themselves are checked by
indra/newview/tests/lltexturefetchpolicy_test.cpp).

- A refinement fetch that ends without data no longer holds its texture at its level at once: it is retried
  after 1, 2 and 4 s, and only then held (mMinDiscardLevel). The finished worker is deleted at once, so that the
  retry is not refused on it; good data and Refresh Textures (clearFetchedResults) lift the hold and the wait.

    python3 scripts/tests/test_texture_streaming_fixes.py
    VKSTORM_TEST_ROOT=PATH python3 scripts/tests/test_texture_streaming_fixes.py   (another source tree)"""
import os
import re
import unittest
from pathlib import Path

ROOT = Path(os.environ.get('VKSTORM_TEST_ROOT') or Path(__file__).resolve().parents[2])
TEXTURE = (ROOT / 'indra/newview/llviewertexture.cpp').read_text(encoding='utf-8', errors='replace')


def body(text, signature):
    """The text of the function whose definition starts with `signature`, to its closing brace at column 0."""
    start = text.index(signature)
    end = text.index('\n}\n', start)
    return text[start:end]


class FetchRetry(unittest.TestCase):
    def test_failed_refinement_retries_before_the_hold(self):
        results = body(TEXTURE, 'bool LLViewerFetchedTexture::processFetchResults(')
        branch = results[results.index('// We finished but received no data'):]
        branch = branch[:branch.index('else if (mRawImage.notNull())')]
        # the hold is set only once the retries are spent
        retry = branch.index('else if (!mFetchRetry.onFailure(sCurrentTime))')
        self.assertLess(retry, branch.index('mMinDiscardLevel = current_discard;'))
        self.assertLess(retry, branch.index('mMinDiscardLevel = dis_level;'))
        # the finished worker is deleted at once, after the raw image
        delete = branch.index('LLAppViewer::getTextureFetch()->deleteRequest(getID(), true);')
        self.assertLess(branch.index('destroyRawImage();'), delete)
        self.assertIn('mHasFetcher = false;', branch[delete:])

    def test_good_data_lifts_the_hold(self):
        results = body(TEXTURE, 'bool LLViewerFetchedTexture::processFetchResults(')
        good = results[results.index('"vftuf - data good"'):results.index('mIsRawImageValid = true;')]
        self.assertIn('mFetchRetry.reset();', good)
        self.assertIn('mMinDiscardLevel = 0;', good)

    def test_refresh_lifts_the_hold(self):
        clear = body(TEXTURE, 'void LLViewerFetchedTexture::clearFetchedResults()')
        early = clear.index('if(mNeedsCreateTexture || mIsFetching)')
        self.assertLess(clear.index('mMinDiscardLevel = 0;'), early)
        self.assertLess(clear.index('mFetchRetry.reset();'), early)

    def test_no_request_while_a_retry_waits(self):
        fetch = body(TEXTURE, 'bool LLViewerFetchedTexture::updateFetch()')
        self.assertRegex(fetch, r'else if \(!mFetchRetry\.mayRequest\(sCurrentTime\)\)\n\s*\{\n[^}]*make_request = false;')

    def test_the_hold_is_set_nowhere_else(self):
        # init, the good data, Refresh Textures (all 0) and the spent retries (the level)
        assignments = re.findall(r'mMinDiscardLevel = ([^;]+);', TEXTURE)
        self.assertEqual(sorted(assignments), ['0', '0', '0', 'current_discard', 'dis_level'])


if __name__ == '__main__':
    unittest.main()
