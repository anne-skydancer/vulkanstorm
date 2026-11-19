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
  LLViewerTextureList::updateImageDecodePriority takes the area of the avatar a worn face's object is drawn with only
  while that avatar is drawn (isVisible, not isVisuallyMuted), scales it by the factors of the face's own size and keeps
  the largest (mWornVirtualSize), clamped and capped where mMaxVirtualSize is; LLViewerFetchedTexture::updateFetch hands
  the raised priority, which stays under a boosted texture's (the size setBoostLevel gives it), to the fetcher
  (createRequest, updateRequestPriority) and decides everything else on mMaxVirtualSize, as before.
- The OpenJPEG wrapper never asks for a coarser level than the codestream has: the header read keeps the fewest
  resolutions of any component (for a size that fits the image's 16-bit sides), and the decode clamps its reduce
  factor below them when the coarsest level is no larger than the requested level of a MAX_IMAGE_SIZE image
  (llj2creduce.h; the rule is checked by indra/llimagej2coj/tests/llj2creduce_test.cpp, and against OpenJPEG by
  llj2creduce_openjpeg_test.cpp), with the calls in the same order as before.
- The OpenJPEG wrapper refuses a header before opj_decode allocates for it when, after the reduce factor, a side
  exceeds 16384, the components are not 1 to MAX_IMAGE_COMPONENTS or the samples (4 bytes each) exceed 1 GiB
  (llj2cdecodeguard.h, checked against OpenJPEG by indra/llimagej2coj/tests/llj2cdecodeguard_openjpeg_test.cpp).
  Its header read refuses a component count above MAX_IMAGE_COMPONENTS before LLImageBase stores it in a signed
  byte, where 260 components read as 4.

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
POLICY = (ROOT / 'indra/newview/lltexturefetchpolicy.h').read_text(encoding='utf-8', errors='replace')
FETCH = (ROOT / 'indra/newview/lltexturefetch.cpp').read_text(encoding='utf-8', errors='replace')
FACE = (ROOT / 'indra/newview/llface.cpp').read_text(encoding='utf-8', errors='replace')
FACE_H = (ROOT / 'indra/newview/llface.h').read_text(encoding='utf-8', errors='replace')
JOINT_H = (ROOT / 'indra/llcharacter/lljoint.h').read_text(encoding='utf-8', errors='replace')
PIXEL_AREA_H = (ROOT / 'indra/newview/llfacepixelarea.h').read_text(encoding='utf-8', errors='replace')
J2COJ = (ROOT / 'indra/llimagej2coj/llimagej2coj.cpp').read_text(encoding='utf-8', errors='replace')
J2C_H = (ROOT / 'indra/llimage/llimagej2c.h').read_text(encoding='utf-8', errors='replace')
J2C_GUARD = (ROOT / 'indra/llimagej2coj/llj2cdecodeguard.h').read_text(encoding='utf-8', errors='replace')


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
    """calcPixelArea is computePixelArea (the values, written to a Result) and applyPixelArea (the writes to the face)
    since multi-core C8; the throttle's outputs are kept and handed back by applyPixelArea."""
    RETURN = re.compile(r'\breturn\s+([\w.]+)\s*;')

    def setUp(self):
        self.calc = code(body(FACE, 'bool LLFace::calcPixelArea('))
        self.current = code(body(FACE, 'bool LLFace::pixelAreaIsCurrent('))
        self.compute = code(body(FACE, 'void LLFace::computePixelArea('))
        self.apply = code(body(FACE, 'bool LLFace::applyPixelArea('))
        self.arm = self.apply.index('mLastPixelAreaUpdate = gFrameTimeSeconds')

    def case(self, status):
        start = self.apply.index('case LLFacePixelArea::%s:' % status)
        return self.apply[start:self.apply.index('case LLFacePixelArea::', start + 1)]

    def test_throttled_call_returns_the_last_update(self):
        self.assertIn('return frame_time - mLastPixelAreaUpdate < PIXEL_AREA_UPDATE_PERIOD;', self.current)
        # the serial call and the computation ask the same question, each of its own clock
        self.assertRegex(self.calc, r'if \(pixelAreaIsCurrent\(gFrameTimeSeconds\)\)\s*\{\s*return lastPixelArea\(cos_angle_to_view_dir, radius\);')
        self.assertRegex(self.compute, r'if \(pixelAreaIsCurrent\(view\.frame_time\)\)\s*\{\s*result\.clear\(LLFacePixelArea::CACHED\);\s*return;')
        self.assertEqual(self.RETURN.findall(self.case('CACHED')), [])
        self.assertIn('return lastPixelArea(cos_angle_to_view_dir, radius);', self.case('CACHED'))
        throttled = code(body(FACE, 'bool LLFace::lastPixelArea('))
        self.assertIn('cos_angle_to_view_dir = mLastCosAngleToViewDir;', throttled)
        self.assertIn('radius = mLastPixelRadius;', throttled)
        self.assertEqual(self.RETURN.findall(throttled), ['mLastInFrustum'])

    def test_every_update_that_arms_the_throttle_keeps_its_outputs(self):
        # before the throttle is armed, only a rigged face without extents returns, with false: the next call
        # updates again, and neither caller reads the outputs of a false return
        self.assertRegex(self.compute, r'if \(!hasRiggedExtents\)\s*\{\s*result\.clear\(LLFacePixelArea::NO_EXTENTS\);\s*return;')
        no_extents = self.case('NO_EXTENTS')
        self.assertEqual(self.RETURN.findall(no_extents), ['false'])
        self.assertNotIn('mLastPixelAreaUpdate', no_extents)
        self.assertNotIn('ll_frand', no_extents)
        self.assertLess(self.apply.index('case LLFacePixelArea::NO_EXTENTS:'), self.arm)
        # after it, the one return keeps the outputs it hands back and its result, as they are at the return: a media
        # face out of the frustum (in_frustum false) and the rest
        after = self.apply[self.arm:]
        self.assertEqual(self.RETURN.findall(after), ['result.in_frustum'])
        store = after.index('mLastCosAngleToViewDir = cos_angle_to_view_dir;')
        kept = after[store:]
        self.assertIn('mLastPixelRadius = radius;', kept)
        self.assertIn('mLastInFrustum = result.in_frustum;', kept)
        self.assertNotRegex(kept, r'\b(cos_angle_to_view_dir|radius)\s*=[^=]', 'an output changes after it is kept')
        self.assertLess(after.index('cos_angle_to_view_dir = result.cos_angle_to_view_dir;'), store)
        self.assertLess(after.index('radius = result.radius;'), store)

    def test_a_media_face_out_of_the_frustum_is_not_in_it(self):
        project = code(member(PIXEL_AREA_H, 'inline void project('))
        out = project[project.index('if (!view.camera->AABBInFrustum(center, size))'):]
        out = out[:out.index('return;')]
        self.assertIn('result.importance = 0.f;', out)
        self.assertIn('result.in_frustum = false;', out)
        self.assertEqual(project.count('result.in_frustum = true;'), 1)

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
    FETCH_PRIORITY = 'const F32 fetch_priority = LLTextureFetchPolicy::fetchPriority(decode_priority, mWornVirtualSize, F_ALMOST_ZERO);'
    AVATAR = 'LLVOAvatar* avatar = (vsize > 0.f && !objp->isAvatar()) ? objp->getRootEdit()->getAvatar() : nullptr;'
    DRAWN = 'const bool drawn = avatar->isVisible() && !avatar->isVisuallyMuted();'
    DRAWN_AREA = 'const F32 area = drawn ? avatar->getPixelArea() : 0.f;'
    WORN_AREA = 'F32 worn_vsize = LLTextureFetchPolicy::wornFetchArea(vsize, avatar ? wornAvatarArea(avatar) : 0.f);'
    STORE = 'imagep->mWornVirtualSize = llmin(max_worn_vsize, LLViewerFetchedTexture::sMaxVirtualSize);'

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
        # the avatar the object is drawn with, counted only while it is drawn (out of view, hidden or a jelly doll: no area)
        self.assertLess(area, loop.index(self.AVATAR))
        self.assertLess(loop.index(self.AVATAR), loop.index(self.WORN_AREA))
        self.assertEqual(loop.count('getPixelArea()'), 1)
        # (wornAvatarArea: once an avatar a pass since multi-core C8, the same tests in the same order)
        worn = code(body(TEXTURE_LIST, 'F32 LLViewerTextureList::wornAvatarArea('))
        self.assertLess(worn.index(self.DRAWN), worn.index(self.DRAWN_AREA))
        self.assertEqual(worn.count('getPixelArea()'), 1)
        self.assertEqual(worn.count('isVisible()'), 1)
        self.assertEqual(worn.count('isVisuallyMuted()'), 1)
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
        self.assertEqual(decode.count('mWornVirtualSize'), 1)

    def test_capped_as_mMaxVirtualSize(self):
        for signature, cap in (('void LLViewerFetchedTexture::processTextureStats()', '(F32)(max_tex_res * max_tex_res)'),
                               ('void LLViewerLODTexture::processTextureStats()', 'max_tex_res * max_tex_res')):
            stats = code(body(TEXTURE, signature))
            self.assertRegex(stats, r'mMaxVirtualSize = llmin\(mMaxVirtualSize, %s\);\s*mWornVirtualSize = llmin\(mWornVirtualSize, %s\);'
                             % (re.escape(cap), re.escape(cap)))

    def test_zeroed_with_mMaxVirtualSize(self):
        for signature in ('void LLViewerTexture::init(bool firstinit)', 'void LLViewerTexture::resetTextureStats()'):
            self.assertRegex(code(body(TEXTURE, signature)), r'mMaxVirtualSize = 0\.0?f;\s*mWornVirtualSize = 0\.f;')

    def test_written_nowhere_else(self):
        # init, resetTextureStats and the two caps, and the store in updateImageDecodePriority
        self.assertEqual(len(re.findall(r'\bmWornVirtualSize = ', code(TEXTURE))), 4)
        self.assertEqual(len(re.findall(r'\bmWornVirtualSize = ', code(TEXTURE_LIST))), 1)
        self.assertEqual(len(re.findall(r'\bF32 mWornVirtualSize = 0\.f;', TEXTURE_H)), 1)
        self.assertNotIn('mFetchVirtualSize', TEXTURE + TEXTURE_H + TEXTURE_LIST)

    def test_boosted_priority_is_the_boost_size(self):
        # the worn rank stays under the size setBoostLevel gives a boosted texture, which updateImageDecodePriority
        # leaves alone (it skips the faces of a boosted texture)
        boost = code(body(TEXTURE, 'void LLViewerTexture::setBoostLevel(S32 level)'))
        self.assertRegex(boost, r'if \(mBoostLevel >= LLViewerTexture::BOOST_HIGH\)\s*\{\s*mMaxVirtualSize = 2048\.f \* 2048\.f;')
        self.assertIn('constexpr float BOOSTED_PRIORITY = 2048.f * 2048.f;', POLICY)
        self.assertIn('constexpr float WORN_PRIORITY_MAX = BOOSTED_PRIORITY - 0.25f;', POLICY)
        self.assertIn('std::max(decode_priority, std::min(worn_size, WORN_PRIORITY_MAX))', code(POLICY))


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


class J2CDecodeGuard(unittest.TestCase):
    def test_decode_refuses_before_opj_decode(self):
        decode = code(member(J2COJ, '    bool decode(U8* data, U32 dataSize, U32* channels, U8 discard_level)'))
        guard = decode.index('if (!LLJ2CDecodeGuard::decodeAllowed(ref_w, ref_h, image->numcomps, discard_level, '
                             'MAX_IMAGE_COMPONENTS))')
        self.assertLess(decode.index('opj_read_header(stream, decoder, &image)'), guard)
        self.assertLess(guard, decode.index('opj_decode(decoder, stream, image)'))
        self.assertEqual(decode.count('opj_decode('), 1)

    def test_header_read_refuses_components_before_storing_them(self):
        # LLImageBase keeps the count in a signed byte (260 read as 4): refused before anything reaches the image
        metadata = code(body(J2COJ, 'bool LLImageJ2COJ::getMetadata('))
        refuse = metadata.index('if (!LLJ2CDecodeGuard::componentsSupported(components, MAX_IMAGE_COMPONENTS))')
        self.assertLess(metadata.index('decode.readHeader('), refuse)
        self.assertLess(refuse, metadata.index('base.mDiscardLevel = discard_level;'))
        self.assertLess(refuse, metadata.index('base.setSize(width, height, components);'))
        self.assertRegex(metadata[refuse:], r'^[^}]*return false;\s*\}')

    def test_the_bounds(self):
        guard = code(J2C_GUARD)
        self.assertIn('constexpr unsigned long long MAX_DECODE_DIMENSION = 16384;', guard)
        self.assertIn('constexpr unsigned long long MAX_DECODE_BYTES = 1ull << 30;', guard)
        self.assertIn('constexpr unsigned long long BYTES_PER_SAMPLE = 4;', guard)


class PixelAreaJobs(unittest.TestCase):
    """Multi-core C8: the texture list computes its faces' pixel areas ahead on the frame job helpers. What a job may
    run writes nothing but its result, and the serial loop stores each value at the face's turn."""

    def setUp(self):
        self.compute = code(body(FACE, 'void LLFace::computePixelArea('))
        self.apply = code(body(FACE, 'bool LLFace::applyPixelArea('))
        self.ahead = code(body(TEXTURE_LIST, 'size_t LLViewerTextureList::precomputePixelAreas('))
        self.turn = code(body(TEXTURE_LIST, 'bool LLViewerTextureList::calcFacePixelArea('))
        self.decode = code(body(TEXTURE_LIST, 'void LLViewerTextureList::updateImageDecodePriority('))
        self.fetch = code(body(TEXTURE_LIST, 'F32 LLViewerTextureList::updateImagesFetchTextures('))

    def test_the_jitter_is_drawn_when_a_value_is_applied(self):
        # ll_frand has a generator per thread: one draw, on the main thread, where calcPixelArea drew it
        self.assertEqual(code(FACE).count('ll_frand()'), 1)
        self.assertEqual(self.apply.count('ll_frand()'), 1)
        self.assertNotIn('ll_frand', code(PIXEL_AREA_H))
        self.assertNotIn('ll_rand', code(PIXEL_AREA_H))

    def test_a_job_writes_nothing_but_its_result(self):
        # no member of the face is assigned in the computation
        self.assertNotRegex(self.compute, r'(?<![\w.>])m[A-Z]\w*(\[\d\])?\s*=[^=]')
        self.assertNotIn('const_cast', self.compute)
        # each lazy write of the serial path is behind a test that hands the face back to the main thread
        update = self.compute.index('LLSkinningUtil::updateRiggingInfo(')
        needs = self.compute.index('if (rigInfo.needsUpdate())')
        self.assertRegex(self.compute[needs:update], r'if \(read_only\)\s*\{\s*result\.clear\(LLFacePixelArea::NEEDS_SERIAL\);\s*return;')
        self.assertRegex(self.compute, r'if \(read_only && joint->isWorldMatrixDirty\(\)\)\s*\{\s*result\.clear\(LLFacePixelArea::NEEDS_SERIAL\);\s*return;')
        self.assertIn('read_only ? joint->getWorldMatrixNoUpdate() : joint->getWorldMatrix()', self.compute)
        self.assertEqual(self.compute.count('getWorldMatrix()'), 1)
        # the volume face's range test (F1), without the log line of te_has_volume_face
        guard = self.compute.index('if (read_only && volume && !(mTEOffset >= 0 && mTEOffset < volume->getNumVolumeFaces()))')
        self.assertLess(guard, self.compute.index('te_has_volume_face(volume, mTEOffset)'))
        self.assertNotRegex(self.compute, r'LL_(WARNS|INFOS|DEBUGS|ERRS)')
        self.assertNotIn('LLCachedControl', self.compute)
        self.assertNotIn('LLPointer<', self.compute)
        self.assertNotIn('LLViewerCamera::getInstance', self.compute)
        self.assertNotIn('gFrameTimeSeconds', self.compute)
        # the joint accessors read only
        self.assertIn('bool isWorldMatrixDirty() const { return (mDirtyFlags & MATRIX_DIRTY) != 0; }', JOINT_H)
        self.assertIn('const LLMatrix4& getWorldMatrixNoUpdate() const { return mXform.getWorldMatrix(); }', JOINT_H)

    def test_the_job_body_is_the_read_only_computation(self):
        batch = self.ahead[self.ahead.index('LL::FrameJobs::parallelFor('):]
        self.assertIn('faces[i]->computePixelArea(view, true, results[i]);', batch)
        self.assertNotIn('applyPixelArea', self.ahead)
        self.assertNotIn('calcPixelArea', self.ahead)
        self.assertNotRegex(batch, r'LLPointer|LL_(WARNS|INFOS)|gSavedSettings|delete ')

    def test_the_face_set_follows_the_loop(self):
        # the boost skip, the cumulative face cap and the frame gate, shared with updateImageDecodePriority
        self.assertIn('imagep->getNumRefs() <= 1 || imagep->getBoostLevel() >= LLViewerFetchedTexture::BOOST_HIGH', self.ahead)
        cap = 'S32 faces_to_check = (face_count > %s) ? 0 : imagep->getNumFaces(i);'
        self.assertIn(cap % 'MAX_FACES_TO_CHECK', self.ahead)
        self.assertIn(cap % 'max_faces_to_check', self.decode)
        self.assertIn('U32 max_faces_to_check = MAX_FACES_TO_CHECK;', self.decode)
        gate = '(gFrameCount - face->mLastTextureUpdate) > FACE_UPDATE_FRAMES'
        self.assertIn(gate, self.ahead)
        self.assertIn('if (%s)' % gate, self.decode)
        self.assertIn('constexpr U32 FACE_UPDATE_FRAMES = 10;', TEXTURE_LIST)
        self.assertIn('constexpr U32 MAX_FACES_TO_CHECK = 1024;', TEXTURE_LIST)
        # a face under several textures is listed once
        self.assertIn('face->mPixelAreaPass != mPixelAreaPass', self.ahead)

    def test_a_value_is_stored_at_the_face_turn_in_the_loop(self):
        self.assertIn('face->mInFrustum = calcFacePixelArea(face, cos_angle_to_view_dir, radius);', self.decode)
        self.assertNotIn('->calcPixelArea(', self.decode)
        # only this pass's value for this very face, never one that had to write, and only while the face is where the
        # job saw it; everything else is the serial call
        self.assertIn('mPixelAreaPass != 0 && face->mPixelAreaPass == mPixelAreaPass', self.turn)
        self.assertIn('slot < mPixelAreaFaces.size() && mPixelAreaFaces[slot] == face', self.turn)
        self.assertIn('result.status != LLFacePixelArea::NEEDS_SERIAL', self.turn)
        self.assertIn('(result.status == LLFacePixelArea::CACHED) == face->pixelAreaIsCurrent(gFrameTimeSeconds)', self.turn)
        self.assertEqual(self.turn.count('return face->applyPixelArea(result, cos_angle_to_view_dir, radius);'), 1)
        self.assertEqual(self.turn.count('return face->calcPixelArea(cos_angle_to_view_dir, radius);'), 1)

    def test_the_time_box_bounds_the_work_done_ahead(self):
        # a batch at a time, asked for inside the loop when it gets to the batch's first entry: the loop's time test
        # (after every entry) comes before every batch but the first
        self.assertIn('constexpr size_t PIXEL_AREA_BATCH_FACES = 1024;', TEXTURE_LIST)
        self.assertIn('for (; next < entries.size() && mPixelAreaFaces.size() - batch_begin < PIXEL_AREA_BATCH_FACES; ++next)', self.ahead)
        # only the batch's own faces are computed; the earlier ones stay listed for calcFacePixelArea
        self.assertNotIn('mPixelAreaFaces.clear()', self.ahead)
        self.assertIn('LLFace* const* faces = mPixelAreaFaces.data() + batch_begin;', self.ahead)
        self.assertIn('LLFacePixelArea::Result* results = mPixelAreaResults.data() + batch_begin;', self.ahead)
        self.assertIn('LL::FrameJobs::parallelFor(count, PIXEL_AREA_GRAIN,', self.ahead)
        # nothing ahead where the batch would run on the main thread anyway (no helpers, serial mode)
        self.assertIn('if ((LL::FrameJobs::helperCount() == 0 || LL::FrameJobs::isSerial()) && !compare)', self.ahead)
        loop = self.fetch.index('for (auto& imagep : entries)')
        ahead = self.fetch.index('ahead_end = precomputePixelAreas(entries, ahead_end, mPixelAreaCompare);')
        self.assertEqual(self.fetch.count('precomputePixelAreas('), 1)
        self.assertLess(loop, self.fetch.index('if (entry_index++ == ahead_end)'))
        self.assertLess(self.fetch.index('if (entry_index++ == ahead_end)'), ahead)
        self.assertLess(ahead, self.fetch.index('updateImageDecodePriority(imagep);'))
        self.assertLess(self.fetch.index('updateImageDecodePriority(imagep);'), self.fetch.index('if (timer.getElapsedTimeF32() > max_time)'))
        # switched off: no batch is ever asked for
        self.assertIn('size_t ahead_end = mPixelAreaPass != 0 ? 0 : entries.size();', self.fetch)

    def test_the_pass_ends_with_the_loop(self):
        # outside updateImagesFetchTextures (the low-memory purge of LLViewerTexture::updateClass) faces run calcPixelArea
        ahead = self.fetch.index('ahead_end = precomputePixelAreas(entries, ahead_end, mPixelAreaCompare);')
        loop = self.fetch.index('for (auto& imagep : entries)')
        end = self.fetch.index('mPixelAreaPass = 0;')
        self.assertLess(self.fetch.index('LLTimer timer;'), ahead)  # inside the pass's time box
        self.assertLess(loop, ahead)
        self.assertLess(ahead, end)
        self.assertLess(end, self.fetch.index('return timer.getElapsedTimeF32();'))
        self.assertEqual(self.fetch.count('return '), 1)


if __name__ == '__main__':
    unittest.main()
