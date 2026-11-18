# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Text guards for the texture streaming fixes of 2026-11-18 (the rules themselves are checked by
indra/newview/tests/lltexturefetchpolicy_test.cpp).

- A refinement fetch that ends without data no longer holds its texture at its level at once: it is retried
  after 1, 2 and 4 s, and only then held (mMinDiscardLevel). The finished worker is deleted at once, so that the
  retry is not refused on it; good data and Refresh Textures (clearFetchedResults) lift the hold and the wait.
- A texture's first fetch that ends without data marks the asset missing at once only for a 404 or a failure that
  is not the network's (a failed decode, a blacklisted asset, a local file), and for server bakes and map tiles,
  which keep their own rules; any other failure gets the same retries first.
- The waiting requests are ordered by priorities read once under each worker's lock (the HTTP release and the
  OpenSim UDP list), never by the live mImagePriority; the UDP list reads them after releasing the queue lock.

    python3 scripts/tests/test_texture_streaming_fixes.py
    VKSTORM_TEST_ROOT=PATH python3 scripts/tests/test_texture_streaming_fixes.py   (another source tree)"""
import os
import re
import unittest
from pathlib import Path

ROOT = Path(os.environ.get('VKSTORM_TEST_ROOT') or Path(__file__).resolve().parents[2])
TEXTURE = (ROOT / 'indra/newview/llviewertexture.cpp').read_text(encoding='utf-8', errors='replace')
FETCH = (ROOT / 'indra/newview/lltexturefetch.cpp').read_text(encoding='utf-8', errors='replace')


def body(text, signature):
    """The text of the function whose definition starts with `signature`, to its closing brace at column 0."""
    start = text.index(signature)
    end = text.index('\n}\n', start)
    return text[start:end]


def code(text):
    """`text` without its // comments."""
    return re.sub(r'//[^\n]*', '', text)


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


class FirstFetch(unittest.TestCase):
    def first_fetch_branch(self):
        results = body(TEXTURE, 'bool LLViewerFetchedTexture::processFetchResults(')
        branch = results[results.index('// We finished but received no data'):]
        return branch[:branch.index('else if (!mFetchRetry.onFailure(sCurrentTime))')]

    def test_missing_at_once_only_for_final_failures(self):
        branch = self.first_fetch_branch()
        decision = branch[:branch.index('if (!missing && mFetchRetry.onFailure(sCurrentTime))')]
        for cause in ('mLastHttpGetStatus == http_not_found',
                      'getFTType() == FTT_LOCAL_FILE',
                      'getFTType() == FTT_SERVER_BAKE',
                      'getFTType() == FTT_MAP_TILE',
                      'FSAssetBlacklist::getInstance()->isBlacklisted(getID(), LLAssetType::AT_TEXTURE)',
                      'missing = decoded && decoded_discard < 0;'):
            self.assertIn(cause, decision)
        self.assertIn('static const LLCore::HttpStatus http_not_found(HTTP_NOT_FOUND);', decision)
        self.assertIn('getLastFetchState(getID(), requested_discard, decoded_discard, decoded);', decision)

    def test_other_failures_retry_first(self):
        branch = self.first_fetch_branch()
        retry = branch.index('if (!missing && mFetchRetry.onFailure(sCurrentTime))')
        missing = branch.index('setIsMissingAsset();')
        self.assertLess(retry, missing)
        self.assertEqual(branch.count('setIsMissingAsset();'), 1)
        self.assertRegex(branch[retry:missing], r'\}\n\s*else\n\s*\{')


class PrioritySnapshots(unittest.TestCase):
    SNAPSHOT = re.compile(r'(\w+)->lockWorkMutex\(\);[^\n]*\n\s*const F32 priority = \1->mImagePriority;')

    def test_no_ordering_reads_the_live_priority(self):
        self.assertNotIn('struct Compare', FETCH)
        self.assertNotIn('LLTextureFetchWorker::Compare', FETCH)
        self.assertIn('typedef LLTextureFetchPolicy::PrioritySnapshot<LLTextureFetchWorker> PrioritySnapshot;', FETCH)

    def test_http_release_sorts_snapshots(self):
        release = body(FETCH, 'void LLTextureFetch::releaseHttpWaiters()')
        self.assertIn('typedef std::vector<LLTextureFetchWorker::PrioritySnapshot> worker_list_t;', release)
        self.assertEqual(len(self.SNAPSHOT.findall(release)), 1)
        self.assertLess(self.SNAPSHOT.search(release).start(), release.index('std::partial_sort('))
        self.assertIn('std::partial_sort(tids2.begin(), tids2.begin() + needed, tids2.end());', release)
        self.assertEqual(code(release).count('mImagePriority'), 1)

    def test_udp_list_snapshots_outside_the_queue_lock(self):
        send = body(FETCH, 'void LLTextureFetch::sendRequestListToSimulators()')
        self.assertIn('typedef std::set<LLTextureFetchWorker::PrioritySnapshot> request_list_t;', send)
        queue_lock = send.index('LLMutexLock lock2(&mNetworkQueueMutex);')
        queue_unlock = send.index('}                                                                   // -Mfnq', queue_lock)
        snapshot = self.SNAPSHOT.search(send)
        self.assertIsNotNone(snapshot)
        self.assertLess(queue_unlock, snapshot.start(), 'the worker lock is taken under the queue lock (Mw < Mfnq)')
        self.assertNotIn('mImagePriority', code(send[queue_lock:queue_unlock]))
        # the request sent and the one remembered are the snapshot the list is ordered by
        self.assertIn('addF32Fast(_PREHASH_DownloadPriority, iter2->mPriority);', send)
        self.assertIn('req->mRequestedPriority = iter2->mPriority;', send)
        self.assertEqual(code(send).count('mImagePriority'), 1)


if __name__ == '__main__':
    unittest.main()
