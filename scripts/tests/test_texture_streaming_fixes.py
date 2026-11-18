# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Text guards for the texture streaming fixes of 2026-11-18 (the rules themselves are checked by
indra/newview/tests/lltexturefetchpolicy_test.cpp).

- A refinement fetch that ends without data no longer holds its texture at its level at once: it is retried
  after 1, 2 and 4 s, and only then held (mMinDiscardLevel). Good data and Refresh Textures (clearFetchedResults)
  lift the hold and the wait.
- Every fetch that ends without data has its finished worker deleted at once, whatever the priority, so that a
  later request (a retry, a texture back on screen) is not refused on it.
- A failure a retry cannot fix (a 404 or a failure that is not the network's: a failed decode, a blacklisted
  asset, a local file; server bakes and map tiles keep their own rules) marks a texture's first fetch missing and
  holds a refinement at once; any other failure gets the retries first.
- A fetch worker whose request for more data failed decodes the data it holds only when that data reaches the
  requested level (LLTextureFetchPolicy::decodeAfterFailedRequest); otherwise the fetch ends without data
  ("fail harder"), so the texture's bounded retry applies, instead of the worker decoding that data again and
  sending the failed request again at once.
- The waiting requests are ordered by priorities read once under each worker's lock (the HTTP release and the
  OpenSim UDP list), never by the live mImagePriority; the UDP list reads them after releasing the queue lock.
- LLFace::calcPixelArea's 0.1 s throttle hands back the outputs and the result of the last update instead of
  returning true with both outputs unwritten (getTextureVirtualSize read them).
- A worn texture's request is queued by its avatar's area (LLTextureFetchPolicy::wornFetchArea, fetchPriority):
  LLViewerTextureList::updateImageDecodePriority scales a worn face's area by the factors of the face's own size and
  keeps the largest (mFetchVirtualSize), clamped and capped where mMaxVirtualSize is; LLViewerFetchedTexture::updateFetch
  hands the raised priority to the fetcher (createRequest, updateRequestPriority) and decides everything else on
  mMaxVirtualSize, as before.
- The OpenJPEG wrapper never asks for a coarser level than the codestream has: the header read keeps the fewest
  resolutions of any component (for a size that fits the image's 16-bit sides), and the decode clamps its reduce
  factor below them when the coarsest level is no larger than the requested level of a MAX_IMAGE_SIZE image
  (llj2creduce.h; the rule is checked by indra/llimagej2coj/tests/llj2creduce_test.cpp, and against OpenJPEG by
  llj2creduce_openjpeg_test.cpp), with the calls in the same order as before.

    python3 scripts/tests/test_texture_streaming_fixes.py
    VKSTORM_TEST_ROOT=PATH python3 scripts/tests/test_texture_streaming_fixes.py   (another source tree)"""
import os
import re
import unittest
from pathlib import Path

ROOT = Path(os.environ.get('VKSTORM_TEST_ROOT') or Path(__file__).resolve().parents[2])
TEXTURE = (ROOT / 'indra/newview/llviewertexture.cpp').read_text(encoding='utf-8', errors='replace')
TEXTURE_H = (ROOT / 'indra/newview/llviewertexture.h').read_text(encoding='utf-8', errors='replace')
TEXTURE_LIST = (ROOT / 'indra/newview/llviewertexturelist.cpp').read_text(encoding='utf-8', errors='replace')
FETCH =(ROOT / 'indra/newview/lltexturefetch.cpp').read_text(encoding='utf-8', errors='replace')
FACE = (ROOT / 'indra/newview/llface.cpp').read_text(encoding='utf-8', errors='replace')
FACE_H = (ROOT / 'indra/newview/llface.h').read_text(encoding='utf-8', errors='replace')
J2COJ = (ROOT / 'indra/llimagej2coj/llimagej2coj.cpp').read_text(encoding='utf-8', errors='replace')
J2C_H = (ROOT / 'indra/llimage/llimagej2c.h').read_text(encoding='utf-8', errors='replace')


def body(text, signature):
    """The text of the function whose definition starts with `signature`, to its closing brace at column 0."""
    start = text.index(signature)
    end = text.index('\n}\n', start)
    return text[start:end]


def member(text, signature):
    """The text of the member function defined in its class whose definition starts with `signature`, to its closing
    brace at column 4."""
    start = text.index(signature)
    end = text.index('\n    }\n', start)
    return text[start:end]


def code(text):
    """`text` without its // comments."""
    return re.sub(r'//[^\n]*', '', text)


def no_data_block():
    """processFetchResults from the test of a fetch that has just finished without data to its end."""
    results = body(TEXTURE, 'bool LLViewerFetchedTexture::processFetchResults(')
    return results[results.index('const bool no_data = '):]


HOLD = 'else if (final_failure || !mFetchRetry.onFailure(sCurrentTime))'


class FetchRetry(unittest.TestCase):
    def test_failed_refinement_retries_before_the_hold(self):
        block = no_data_block()
        branch = block[block.index('// We finished but received no data'):block.index('else if (mRawImage.notNull())')]
        # the hold is set only for a final failure or once the retries are spent
        hold = branch.index(HOLD)
        self.assertLess(hold, branch.index('mMinDiscardLevel = current_discard;'))
        self.assertLess(hold, branch.index('mMinDiscardLevel = dis_level;'))

    def test_every_finished_worker_without_data_is_deleted(self):
        block = no_data_block()
        self.assertRegex(block, r'^const bool no_data = \(mRawDiscardLevel < 0 \|\| mRawDiscardLevel == INVALID_DISCARD_LEVEL\)'
                                r'\s*&& mFetchState > 1;')
        self.assertIn('if ((decode_priority > 0) && no_data)', block)
        # after the priority test and its branches, so whatever the priority was
        branches_end = block.index('else if (mRawImage.notNull())')
        delete = block.index('LLAppViewer::getTextureFetch()->deleteRequest(getID(), true);')
        self.assertLess(branches_end, delete)
        guard = code(block[branches_end:delete])
        self.assertRegex(guard, r'if \(no_data && mHasFetcher\)\s*\{\s*$')
        self.assertIn('mHasFetcher = false;', block[delete:])
        self.assertEqual(block.count('deleteRequest('), 1)

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


class FinalFailures(unittest.TestCase):
    FIRST_FETCH_RETRY = 'if (!final_failure && mFetchRetry.onFailure(sCurrentTime))'

    def test_the_causes_a_retry_cannot_fix(self):
        block = no_data_block()
        start = block.index('// We finished but received no data')
        decision = block[start:block.index('if (getDiscardLevel() < 0)', start)]
        for cause in ('mLastHttpGetStatus == http_not_found',
                      'getFTType() == FTT_LOCAL_FILE',
                      'getFTType() == FTT_SERVER_BAKE',
                      'getFTType() == FTT_MAP_TILE',
                      'FSAssetBlacklist::getInstance()->isBlacklisted(getID(), LLAssetType::AT_TEXTURE)',
                      'final_failure = decoded && decoded_discard < 0;'):
            self.assertIn(cause, decision)
        self.assertIn('static const LLCore::HttpStatus http_not_found(HTTP_NOT_FOUND);', decision)
        self.assertIn('getLastFetchState(getID(), requested_discard, decoded_discard, decoded);', decision)

    def test_first_fetch_missing_at_once_only_for_final_failures(self):
        block = no_data_block()
        first = block[block.index('if (getDiscardLevel() < 0)'):block.index(HOLD)]
        retry = first.index(self.FIRST_FETCH_RETRY)
        missing = first.index('setIsMissingAsset();')
        self.assertLess(retry, missing)
        self.assertEqual(first.count('setIsMissingAsset();'), 1)
        self.assertRegex(first[retry:missing], r'\}\n\s*else\n\s*\{')

    def test_refinement_held_at_once_for_final_failures(self):
        block = no_data_block()
        # the refinement branch follows the first fetch's, and a final failure holds without a retry
        self.assertLess(block.index(self.FIRST_FETCH_RETRY), block.index(HOLD))
        self.assertEqual(block.count('mFetchRetry.onFailure('), 2)


class FailedRequest(unittest.TestCase):
    def test_data_at_hand_is_decoded_only_when_it_serves_the_request(self):
        work = FETCH[FETCH.index('bool LLTextureFetchWorker::doWork(S32 param)'):]
        use = work.index('// Use available data')
        guard = work.rindex('if (', 0, use)
        self.assertEqual(code(work[guard:use]).split(),
                         'if (cur_size > 0 && LLTextureFetchPolicy::decodeAfterFailedRequest(mFormattedImage->getDiscardLevel(), '
                         'mDesiredDiscard)) {'.split())
        self.assertEqual(FETCH.count('decodeAfterFailedRequest('), 1)
        self.assertEqual(FETCH.count('// Use available data'), 1)
        # the decode of the data at hand, and otherwise the end of the fetch without data
        decode = work[use:work.index('// Fail harder', use)]
        self.assertIn('mLoadedDiscard = mFormattedImage->getDiscardLevel();', decode)
        self.assertIn('setState(DECODE_IMAGE);', decode)
        self.assertRegex(decode, r'return doWork\(param\);\s*\}\s*$')
        fail = work[work.index('// Fail harder', use):]
        fail = fail[:fail.index('return true; // failed')]
        self.assertIn('resetFormattedData();', fail)
        self.assertIn('setState(DONE);', fail)
        self.assertIn('releaseHttpSemaphore();', fail)


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


class PixelAreaThrottle(unittest.TestCase):
    RETURN = re.compile(r'\breturn\s+(\w+)\s*;')

    def setUp(self):
        self.calc = code(body(FACE, 'bool LLFace::calcPixelArea('))
        self.throttle_start = self.calc.index('if (gFrameTimeSeconds - mLastPixelAreaUpdate < PIXEL_AREA_UPDATE_PERIOD)')
        self.throttle_end = self.calc.index('\n    }\n', self.throttle_start)
        self.arm = self.calc.index('mLastPixelAreaUpdate = gFrameTimeSeconds')

    def test_throttled_call_returns_the_last_update(self):
        throttled = self.calc[self.throttle_start:self.throttle_end]
        self.assertIn('cos_angle_to_view_dir = mLastCosAngleToViewDir;', throttled)
        self.assertIn('radius = mLastPixelRadius;', throttled)
        self.assertEqual(self.RETURN.findall(throttled), ['mLastInFrustum'])

    def test_every_update_that_arms_the_throttle_keeps_its_outputs(self):
        # before the throttle is armed, only a rigged face without extents returns, with false: the next call
        # updates again, and neither caller reads the outputs of a false return
        self.assertEqual(self.RETURN.findall(self.calc[self.throttle_end:self.arm]), ['false'])
        self.assertLess(self.calc.index('if (!hasRiggedExtents)'), self.calc.index('return false', self.throttle_end))
        # after it, every return keeps the outputs it hands back and its result, as they are at the return
        after = self.calc[self.arm:]
        returns = list(self.RETURN.finditer(after))
        self.assertEqual([r.group(1) for r in returns], ['false', 'true'])  # a media face out of the frustum, the rest
        last = 0
        for r in returns:
            segment = after[last:r.start()]
            store = segment.rfind('mLastCosAngleToViewDir = cos_angle_to_view_dir;')
            self.assertGreaterEqual(store, 0, 'the return of %s keeps no outputs' % r.group(1))
            kept = segment[store:]
            self.assertIn('mLastPixelRadius = radius;', kept)
            self.assertIn('mLastInFrustum = %s;' % r.group(1), kept)
            self.assertNotRegex(kept, r'\b(cos_angle_to_view_dir|radius)\s*=[^=]', 'an output changes after it is kept')
            last = r.end()

    def test_before_the_first_update(self):
        # no partial-overlap scaling (adjustPartialOverlapPixelArea gives 1 for cos 1, radius 0), in the frustum as before
        self.assertRegex(FACE_H, r'F32\s+mLastCosAngleToViewDir = 1\.f;')
        self.assertRegex(FACE_H, r'F32\s+mLastPixelRadius = 0\.f;')
        self.assertRegex(FACE_H, r'bool\s+mLastInFrustum = true;')

    def test_outputs_read_only_after_a_true_return(self):
        size = code(body(FACE, 'F32 LLFace::getTextureVirtualSize()'))
        call = size.index('bool in_frustum = calcPixelArea(cos_angle_to_view_dir, radius);')
        early = size.index('if (mPixelArea < F_ALMOST_ZERO || !in_frustum)')
        read = size.index('adjustPartialOverlapPixelArea(cos_angle_to_view_dir, radius')
        self.assertLess(call, early)
        self.assertLess(early, read)


class WornFetchRank(unittest.TestCase):
    FETCH_PRIORITY = 'const F32 fetch_priority = LLTextureFetchPolicy::fetchPriority(decode_priority, mFetchVirtualSize, F_ALMOST_ZERO);'
    WEARER = 'const LLVOAvatar* wearer = (vsize > 0.f && !objp->isAvatar()) ? objp->getRootEdit()->getAvatar() : nullptr;'
    WORN_AREA = 'F32 worn_vsize = LLTextureFetchPolicy::wornFetchArea(vsize, wearer ? wearer->getPixelArea() : 0.f);'
    STORE = 'imagep->mFetchVirtualSize = llmin(max_worn_vsize, LLViewerFetchedTexture::sMaxVirtualSize);'

    def decode_priority(self):
        return code(body(TEXTURE_LIST, 'void LLViewerTextureList::updateImageDecodePriority('))

    def test_only_the_requests_carry_the_worn_rank(self):
        fetch = code(body(TEXTURE, 'bool LLViewerFetchedTexture::updateFetch()'))
        self.assertLess(fetch.index('F32 decode_priority = mMaxVirtualSize;'), fetch.index(self.FETCH_PRIORITY))
        # the two calls that hand a priority to the fetcher, and nothing else
        self.assertEqual(fetch.count('fetch_priority'), 3)
        self.assertIn('LLAppViewer::getTextureFetch()->updateRequestPriority(mID, fetch_priority);', fetch)
        self.assertRegex(fetch, r'createRequest\(mFTType, mUrl, getID\(\), getTargetHost\(\), fetch_priority,')
        # every decision keeps the size the texture is fetched for: the 5 s hold, the request, the results
        self.assertIn('if (decode_priority > 0.0f || mStopFetchingTimer.getElapsedTimeF32() > MAX_HOLD_TIME)', fetch)
        self.assertIn('if (decode_priority <= 0)', fetch)
        self.assertIn('processFetchResults(desired_discard, current_discard, fetch_discard, decode_priority)', fetch)
        self.assertIn('processFetchResults(desired_discard, current_discard, decoded_discard, decode_priority);', fetch)
        self.assertEqual(code(TEXTURE).count('fetchPriority('), 1)

    def test_worn_area_scaled_as_the_face_area(self):
        decode = self.decode_priority()
        boosted = decode.index('if (imagep->getBoostLevel() < LLViewerFetchedTexture::BOOST_HIGH)')
        self.assertLess(decode.index('F32 max_worn_vsize = 0.f;'), boosted)
        loop = decode[boosted:decode.index('imagep->addTextureStats(max_vsize);')]
        area = loop.index('F32 vsize = face->getPixelArea();')
        self.assertLess(area, loop.index(self.WEARER))
        self.assertLess(loop.index(self.WEARER), loop.index(self.WORN_AREA))
        # each factor of the face's own size, in its order, and the same factor of its worn size right after it
        own = re.findall(r'(?<![\w.])vsize ([*/]=) (\w+);', loop)
        self.assertEqual(own, [('/=', 'min_scale'), ('/=', 'bias'), ('*=', 'camera_boost')])
        self.assertEqual(re.findall(r'\bworn_vsize ([*/]=) (\w+);', loop), own)
        for operator, factor in own:
            self.assertRegex(loop, r'(?<![\w.])vsize %s %s;\s*worn_vsize %s %s;'
                             % (re.escape(operator), factor, re.escape(operator), factor))
        self.assertIn('const F32 camera_boost = llmax(face->mImportanceToCamera*texture_camera_boost, 1.f);', loop)
        self.assertIn('max_worn_vsize = llmax(max_worn_vsize, worn_vsize);', loop)

    def test_stored_for_every_texture(self):
        decode = self.decode_priority()
        stats = decode.index('imagep->addTextureStats(max_vsize);')
        store = decode.index(self.STORE)
        # after the block of unboosted textures, so a boosted texture, which skips its faces, stores 0
        self.assertLess(decode.index('\n    }\n', stats), store)
        self.assertLess(store, decode.index('imagep->processTextureStats();'))
        self.assertEqual(decode.count('mFetchVirtualSize'), 1)

    def test_capped_as_mMaxVirtualSize(self):
        for signature, cap in (('void LLViewerFetchedTexture::processTextureStats()', '(F32)(max_tex_res * max_tex_res)'),
                               ('void LLViewerLODTexture::processTextureStats()', 'max_tex_res * max_tex_res')):
            stats = code(body(TEXTURE, signature))
            self.assertRegex(stats, r'mMaxVirtualSize = llmin\(mMaxVirtualSize, %s\);\s*mFetchVirtualSize = llmin\(mFetchVirtualSize, %s\);'
                             % (re.escape(cap), re.escape(cap)))

    def test_zeroed_with_mMaxVirtualSize(self):
        for signature in ('void LLViewerTexture::init(bool firstinit)', 'void LLViewerTexture::resetTextureStats()'):
            self.assertRegex(code(body(TEXTURE, signature)), r'mMaxVirtualSize = 0\.0?f;\s*mFetchVirtualSize = 0\.f;')

    def test_written_nowhere_else(self):
        # init, resetTextureStats and the two caps, and the store in updateImageDecodePriority
        self.assertEqual(len(re.findall(r'\bmFetchVirtualSize = ', code(TEXTURE))), 4)
        self.assertEqual(len(re.findall(r'\bmFetchVirtualSize = ', code(TEXTURE_LIST))), 1)
        self.assertEqual(len(re.findall(r'\bF32 mFetchVirtualSize = 0\.f;', TEXTURE_H)), 1)


class J2CReduceFactor(unittest.TestCase):
    def test_header_read_keeps_the_fewest_resolutions(self):
        header = code(member(J2COJ, '    bool readHeader('))
        self.assertIn('S32& resolutions)', header)
        info = header.index('codestream_info = opj_get_cstr_info(decoder);')
        kept = header.index('codestream_info->m_default_tile_info.tccp_info[comp].numresolutions')
        self.assertLess(info, kept)
        self.assertIn('resolutions = (comp == 0) ? comp_resolutions : llmin(resolutions, comp_resolutions);', header)
        # only for a size the image's 16-bit sides hold, computed in 64 bits: the rule bounds the decode by that size
        self.assertIn('const U64 grid_width = (U64)codestream_info->tw * codestream_info->tdx;', header)
        self.assertIn('const U64 grid_height = (U64)codestream_info->th * codestream_info->tdy;', header)
        self.assertIn('if (codestream_info->m_default_tile_info.tccp_info && grid_width <= 0xffff && grid_height <= 0xffff)',
                      header)
        self.assertLess(header.index('resolutions = 0;'), header.index('grid_width <= 0xffff'))
        metadata = code(body(J2COJ, 'bool LLImageJ2COJ::getMetadata('))
        self.assertIn('discard_level, resolutions);', metadata)
        self.assertIn('base.mNumResolutions = resolutions;', metadata)
        self.assertRegex(J2C_H, r'S32 mNumResolutions = 0;')

    def test_decode_clamps_its_reduce_factor(self):
        decode_impl = code(body(J2COJ, 'bool LLImageJ2COJ::decodeImpl('))
        self.assertRegex(decode_impl, r'const S8 reduce = \(S8\)LLJ2CReduce::reduceFactor\(base\.mDiscardLevel, base\.mNumResolutions, '
                                      r'base\.getWidth\(\),\s*base\.getHeight\(\), MAX_IMAGE_SIZE\);')
        self.assertIn('decoder.decode(base.getData(), max_bytes, &image_channels, reduce);', decode_impl)
        self.assertNotIn('&image_channels, base.mDiscardLevel)', decode_impl)
        # the level reported is the factor OpenJPEG decoded at
        self.assertIn('base.setDiscardLevel(f);', decode_impl)

    def test_decode_calls_in_the_same_order(self):
        # the factor is still set before the header is read: a request the codestream can meet goes through the
        # same calls as before the fix
        decode = code(member(J2COJ, '    bool decode(U8* data, U32 dataSize, U32* channels, U8 discard_level)'))
        factor = decode.index('opj_set_decoded_resolution_factor(decoder, discard_level);')
        self.assertLess(factor, decode.index('opj_read_header(stream, decoder, &image)'))


if __name__ == '__main__':
    unittest.main()
