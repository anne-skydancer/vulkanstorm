// SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { createHash } = require('node:crypto');
const publish = require('../ci_publish_latest.cjs');

// The package each platform's build uploads, named as build.yaml names it.
const packages = {
    windows: 'new_Setup.exe',
    'linux-lts': 'new-ubuntu24.04.tar.xz',
    linux: 'new-ubuntu-latest.tar.xz',
};

async function fixture(t, options = {}) {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'vulkanstorm-publish-'));
    t.after(() => fs.rm(directory, { recursive: true, force: true }));
    for (const [platform, filename] of Object.entries(packages)) {
        // Match actions/download-artifact's layout for the CI upload paths.
        const folder = path.join(directory, `${platform}-installer`, platform === 'windows' ? 'Release' : '');
        await fs.mkdir(folder, { recursive: true });
        if (options.missing !== platform) {
            await fs.writeFile(path.join(folder, filename), platform);
        }
    }
    const calls = [];
    const record = name => async args => {
        calls.push({ name, args });
        return { data: { id: calls.length } };
    };
    const repos = Object.fromEntries([
        'deleteReleaseAsset', 'updateReleaseAsset', 'updateRelease',
    ].map(name => [name, record(name)]));
    repos.createRelease = async args => {
        calls.push({ name: 'createRelease', args });
        return { data: { id: calls.length, draft: true } };
    };
    repos.getReleaseByTag = async () => {
        if (options.noRelease) throw Object.assign(new Error('Not found'), { status: 404 });
        return { data: { id: 10, body: options.body || 'Maintainer notes.', prerelease: false, draft: false } };
    };
    repos.compareCommitsWithBasehead = async () => {
        if (options.comparisonError) throw Object.assign(new Error('Comparison failed'), { status: 404 });
        return { data: { status: options.comparison || 'ahead' } };
    };
    repos.listReleaseAssets = () => {};
    repos.uploadReleaseAsset = async args => {
        if (options.failChecksum && args.name.startsWith('SHA256SUMS.txt')) throw new Error('Upload failed');
        return record('uploadReleaseAsset')(args);
    };
    const github = {
        rest: {
            repos,
            git: {
                getRef: async () => {
                    if (options.noTag) throw Object.assign(new Error('Not found'), { status: 404 });
                    return { data: { object: { sha: 'old' } } };
                },
                updateRef: record('updateRef'),
                createRef: record('createRef'),
            },
        },
        paginate: async () => [
            { id: 100, name: 'old_Setup.exe' },
            { id: 101, name: 'old-ubuntu24.04.tar.xz' },
            { id: 102, name: 'SHA256SUMS.txt' },
            { id: 103, name: 'manual-notes.txt' },
        ],
    };
    return {
        calls,
        args: { github, directory, core: { info() {} }, channel: publish.channels['windows-latest'],
            context: { repo: { owner: 'anne-skydancer', repo: 'vulkanstorm' }, runId: 200, sha: 'new-sha' } },
    };
}

test('every line gives every platform its own tag', () => {
    assert.deepEqual(Object.fromEntries(Object.values(publish.channels).map(channel => [channel.tag, channel.platforms])), {
        'windows-latest': ['windows'], 'linux-latest': ['linux'], 'linux-lts': ['linux-lts'],
        'experimental-windows': ['windows'], 'experimental-linux-latest': ['linux'], 'experimental-linux-lts': ['linux-lts'],
        'preview-windows': ['windows'], 'preview-linux-latest': ['linux'], 'preview-linux-lts': ['linux-lts'],
    });
    // Only windows-latest takes GitHub's Latest badge; experimental and preview are pre-releases.
    const tags = filter => Object.values(publish.channels).filter(filter).map(channel => channel.tag);
    assert.deepEqual(tags(channel => channel.makeLatest), ['windows-latest']);
    assert.deepEqual(tags(channel => !channel.prerelease), ['windows-latest', 'linux-latest', 'linux-lts']);
});

test('the plan: pushes build windows and linux, and only vkstorm-release pushes publish', () => {
    const repository = 'anne-skydancer/vulkanstorm';
    const platformsOf = result => result.matrix.include.map(entry => entry.platform);
    const push = publish.plan({ eventName: 'push', ref: 'refs/heads/vkstorm-release', repository, inputs: null });
    assert.deepEqual(platformsOf(push), ['windows', 'linux']);
    assert.deepEqual(push.publish, [{ platform: 'windows', tag: 'windows-latest' }, { platform: 'linux', tag: 'linux-latest' }]);
    // Each matrix entry carries its own tag to viewer-platform.yaml, and Release packaging.
    assert.deepEqual(push.matrix.include.map(entry => entry.tag), ['windows-latest', 'linux-latest']);
    assert.equal(push.release, true);
    for (const ref of ['refs/heads/vkstorm-devel', 'refs/heads/vkstorm-canary', 'refs/heads/codex/x', 'refs/tags/Second_Life_1']) {
        assert.deepEqual(publish.plan({ eventName: 'push', ref, repository, inputs: null }).publish, []);
    }
    // Pull requests also build linux-lts, and never publish.
    const pr = publish.plan({ eventName: 'pull_request', ref: 'refs/pull/1/merge', repository, inputs: null });
    assert.deepEqual(platformsOf(pr), ['windows', 'linux', 'linux-lts']);
    assert.deepEqual(pr.publish, []);
    assert.deepEqual(pr.matrix.include.map(entry => entry.tag), ['', '', '']);
    assert.equal(pr.release, false);
    // A fork never publishes.
    assert.deepEqual(publish.plan({ eventName: 'push', ref: 'refs/heads/vkstorm-release', repository: 'someone/vulkanstorm' }).publish, []);
    // The linux-lts entry carries the container and the package override.
    const lts = publish.plan({ eventName: 'workflow_dispatch', ref: 'refs/heads/vkstorm-release', repository,
        inputs: { windows: false, linux: false, linux_lts: true, publish: true } });
    assert.deepEqual(lts.publish, [{ platform: 'linux-lts', tag: 'linux-lts' }]);
    assert.equal(lts.matrix.include[0].container.image, 'ubuntu:24.04');
    assert.equal(lts.matrix.include[0].package_override, 'scripts/linux_lts_packages.json');
});

test('the plan: a dispatch builds and publishes only the platforms selected', () => {
    const repository = 'anne-skydancer/vulkanstorm';
    const dispatch = (ref, inputs) => publish.plan({ eventName: 'workflow_dispatch', ref, repository, inputs });
    assert.deepEqual(dispatch('refs/heads/vkstorm-canary', { windows: true, linux: false, linux_lts: true, publish: true }).publish,
        [{ platform: 'windows', tag: 'experimental-windows' }, { platform: 'linux-lts', tag: 'experimental-linux-lts' }]);
    assert.deepEqual(dispatch('refs/heads/vkstorm-devel', { windows: false, linux: 'true', linux_lts: 'false', publish: 'true' }).publish,
        [{ platform: 'linux', tag: 'preview-linux-latest' }]);
    // Without publish a dispatch only builds; other lines never publish.
    const unpublished = dispatch('refs/heads/vkstorm-canary', { windows: true, linux: true, linux_lts: false, publish: false });
    assert.deepEqual(unpublished.matrix.include.map(entry => entry.platform), ['windows', 'linux']);
    assert.deepEqual(unpublished.publish, []);
    assert.equal(unpublished.release, false);
    assert.deepEqual(dispatch('refs/heads/vkstorm-rust', { windows: true, linux: true, linux_lts: true, publish: true }).publish, []);
    assert.deepEqual(dispatch('refs/heads/vkstorm-release', { windows: false, linux: false, linux_lts: false, publish: true }).matrix.include, []);
});

test('a channel publishes only its own package and checksum before moving its tag', async t => {
    for (const [tag, platform] of [['windows-latest', 'windows'], ['linux-latest', 'linux'], ['linux-lts', 'linux-lts'], ['experimental-linux-lts', 'linux-lts']]) {
        const name = packages[platform];
        await t.test(tag, async t => {
            const { args, calls } = await fixture(t);
            await publish({ ...args, channel: publish.channels[tag] });
            const uploads = calls.filter(call => call.name === 'uploadReleaseAsset');
            assert.deepEqual(uploads.map(call => call.args.name), [`${name}.upload-200`, 'SHA256SUMS.txt.upload-200']);
            assert.equal(uploads[1].args.data.toString(),
                `${createHash('sha256').update(platform).digest('hex')}  ${name}\n`);
            const tagIndex = calls.findIndex(call => call.name === 'updateRef');
            assert.equal(calls.slice(0, tagIndex).filter(call => call.name === 'updateReleaseAsset').length, 2);
            assert.deepEqual(calls[tagIndex].args, {
                owner: 'anne-skydancer', repo: 'vulkanstorm', ref: `tags/${tag}`, sha: 'new-sha', force: true,
            });
            const release = calls.findLast(call => call.name === 'updateRelease').args;
            assert.match(release.body, new RegExp(`vulkanstorm-${tag}-run:200`));
            assert.match(release.body, /Maintainer notes/);
            assert.equal(release.prerelease, tag.startsWith('experimental-'));
            assert.equal(release.make_latest, tag === 'windows-latest' ? 'true' : 'false');
            assert.ok(!calls.some(call => call.name === 'deleteReleaseAsset' && call.args.asset_id === 103));
            // Superseded packages go only after the new one is live.
            assert.ok(calls.findIndex(call => call.name === 'deleteReleaseAsset' && call.args.asset_id === 100) > tagIndex);
        });
    }
});

test('a missing package prevents all publication writes for its channel only', async t => {
    const { args, calls } = await fixture(t, { missing: 'linux' });
    await assert.rejects(publish({ ...args, channel: publish.channels['linux-latest'] }), /Expected one linux package/);
    assert.equal(calls.length, 0);
    await publish({ ...args, channel: publish.channels['windows-latest'] });
    assert.ok(calls.some(call => call.name === 'updateRef' && call.args.ref === 'tags/windows-latest'));
});

test('a failed checksum upload leaves the tag and live assets untouched', async t => {
    const { args, calls } = await fixture(t, { failChecksum: true });
    await assert.rejects(publish(args), /Upload failed/);
    assert.deepEqual(calls.map(call => call.name), ['uploadReleaseAsset']);
});

test('late completion of an older run cannot replace a newer publication', async t => {
    const { args, calls } = await fixture(t, { body: '<!-- vulkanstorm-windows-latest-run:201 -->' });
    await publish(args);
    assert.equal(calls.length, 0);
});

test('a newly dispatched build of older source cannot move a tag backwards', async t => {
    const { args, calls } = await fixture(t, { comparison: 'behind' });
    await publish(args);
    assert.equal(calls.length, 0);
});

test('bootstraps a missing release and tag', async t => {
    const { args, calls } = await fixture(t, { noRelease: true, noTag: true });
    await publish({ ...args, channel: publish.channels['preview-linux-lts'] });
    assert.equal(calls[0].name, 'createRelease');
    assert.equal(calls[0].args.tag_name, 'preview-linux-lts');
    assert.equal(calls[0].args.draft, true);
    assert.equal(calls[0].args.prerelease, true);
    assert.ok(calls.some(call => call.name === 'createRef' && call.args.ref === 'refs/tags/preview-linux-lts'));
    assert.equal(calls.findLast(call => call.name === 'updateRelease').args.draft, false);
});

test('a comparison API failure does not get treated as a missing tag', async t => {
    const { args, calls } = await fixture(t, { comparisonError: true });
    await assert.rejects(publish(args), /Comparison failed/);
    assert.equal(calls.length, 0);
});

test('rerunning the same run replaces its summary without losing maintainer notes', async t => {
    const { args, calls } = await fixture(t, {
        body: '<!-- vulkanstorm-windows-latest-start -->\n<!-- vulkanstorm-windows-latest-run:200 -->\nold summary\n<!-- vulkanstorm-windows-latest-end -->\n\nNotes.',
    });
    await publish(args);
    const body = calls.findLast(call => call.name === 'updateRelease').args.body;
    assert.equal(body.match(/vulkanstorm-windows-latest-start/g).length, 1);
    assert.ok(body.endsWith('Notes.'));
    assert.ok(!body.includes('old summary'));
});

test('a newer run on one channel does not block another', async t => {
    const { args, calls } = await fixture(t, { body: '<!-- vulkanstorm-windows-latest-run:999 -->' });
    await publish({ ...args, channel: publish.channels['linux-latest'] });
    assert.ok(calls.some(call => call.name === 'updateRef' && call.args.ref === 'tags/linux-latest'));
});

test('each platform publishes only when its own build job succeeded', async () => {
    const jobs = [
        { name: 'windows / build', conclusion: 'success' },
        { name: 'linux / build', conclusion: 'failure' },
        { name: 'caller / linux-lts / build', conclusion: 'success' },
        { name: 'linux-lts / publish', conclusion: 'failure' },
    ];
    const requests = [];
    const github = {
        rest: { actions: { listJobsForWorkflowRun: 'list' } },
        paginate: async (route, args) => { requests.push({ route, args }); return jobs; },
    };
    const context = { repo: { owner: 'anne-skydancer', repo: 'vulkanstorm' }, runId: 200 };
    const core = { info() {} };
    const results = {};
    for (const platform of ['windows', 'linux', 'linux-lts']) {
        results[platform] = await publish.buildSucceeded({ github, context, core, platform });
    }
    assert.deepEqual(results, { windows: true, linux: false, 'linux-lts': true });
    assert.equal(requests[0].args.run_id, 200);
    assert.equal(requests[0].args.filter, 'latest');
    await assert.rejects(publish.buildSucceeded({ github, context, core, platform: 'freebsd' }), /Unknown platform/);
    jobs.length = 0;
    assert.equal(await publish.buildSucceeded({ github, context, core, platform: 'windows' }), false);
});
