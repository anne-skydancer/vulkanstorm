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
    'linux-LTS': 'new-ubuntu24.04.tar.xz',
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
        args: { github, directory, core: { info() {} }, channel: publish.channels.latest.windows,
            context: { repo: { owner: 'anne-skydancer', repo: 'vulkanstorm' }, runId: 200, sha: 'new-sha' } },
    };
}

test('the tags are latest-windows, latest-linux-LTS and latest-linux', () => {
    assert.deepEqual(Object.values(publish.channels.latest).map(channel => channel.tag),
        ['latest-windows', 'latest-linux-LTS', 'latest-linux']);
    assert.deepEqual(Object.values(publish.channels.experimental).map(channel => channel.tag),
        ['experimental-windows', 'experimental-linux-LTS', 'experimental-linux']);
});

test('publishes only its own platform\'s package and checksum before moving its tag', async t => {
    for (const [platform, name] of Object.entries(packages)) {
        await t.test(platform, async t => {
            const { args, calls } = await fixture(t);
            await publish({ ...args, channel: publish.channels.latest[platform] });
            const uploads = calls.filter(call => call.name === 'uploadReleaseAsset');
            assert.deepEqual(uploads.map(call => call.args.name), [`${name}.upload-200`, 'SHA256SUMS.txt.upload-200']);
            assert.equal(uploads[1].args.data.toString(),
                `${createHash('sha256').update(platform).digest('hex')}  ${name}\n`);
            const tagIndex = calls.findIndex(call => call.name === 'updateRef');
            assert.equal(calls.slice(0, tagIndex).filter(call => call.name === 'updateReleaseAsset').length, 2);
            assert.deepEqual(calls[tagIndex].args, {
                owner: 'anne-skydancer', repo: 'vulkanstorm', ref: `tags/latest-${platform}`, sha: 'new-sha', force: true,
            });
            const release = calls.findLast(call => call.name === 'updateRelease').args;
            assert.match(release.body, new RegExp(`vulkanstorm-latest-${platform}-run:200`));
            assert.match(release.body, /Maintainer notes/);
            assert.equal(release.prerelease, false);
            assert.equal(release.make_latest, platform === 'windows' ? 'true' : 'false');
            assert.ok(!calls.some(call => call.name === 'deleteReleaseAsset' && call.args.asset_id === 103));
            // Superseded packages go only after the new one is live.
            assert.ok(calls.findIndex(call => call.name === 'deleteReleaseAsset' && call.args.asset_id === 100) > tagIndex);
        });
    }
});

test('a missing package prevents all publication writes for its platform only', async t => {
    const { args, calls } = await fixture(t, { missing: 'linux-LTS' });
    await assert.rejects(publish({ ...args, channel: publish.channels.latest['linux-LTS'] }), /Expected one linux-LTS package/);
    assert.equal(calls.length, 0);
    await publish({ ...args, channel: publish.channels.latest.linux });
    assert.ok(calls.some(call => call.name === 'updateRef' && call.args.ref === 'tags/latest-linux'));
});

test('a failed checksum upload leaves the tag and live assets untouched', async t => {
    const { args, calls } = await fixture(t, { failChecksum: true });
    await assert.rejects(publish(args), /Upload failed/);
    assert.deepEqual(calls.map(call => call.name), ['uploadReleaseAsset']);
});

test('late completion of an older run cannot replace a newer publication', async t => {
    const { args, calls } = await fixture(t, { body: '<!-- vulkanstorm-latest-windows-run:201 -->' });
    await publish(args);
    assert.equal(calls.length, 0);
});

test('a newly dispatched build of older source cannot move latest backwards', async t => {
    const { args, calls } = await fixture(t, { comparison: 'behind' });
    await publish(args);
    assert.equal(calls.length, 0);
});

test('bootstraps a missing release and tag', async t => {
    const { args, calls } = await fixture(t, { noRelease: true, noTag: true });
    await publish({ ...args, channel: publish.channels.latest['linux-LTS'] });
    assert.equal(calls[0].name, 'createRelease');
    assert.equal(calls[0].args.tag_name, 'latest-linux-LTS');
    assert.equal(calls[0].args.draft, true);
    assert.ok(calls.some(call => call.name === 'createRef' && call.args.ref === 'refs/tags/latest-linux-LTS'));
    assert.equal(calls.findLast(call => call.name === 'updateRelease').args.draft, false);
});

test('a comparison API failure does not get treated as a missing tag', async t => {
    const { args, calls } = await fixture(t, { comparisonError: true });
    await assert.rejects(publish(args), /Comparison failed/);
    assert.equal(calls.length, 0);
});

test('rerunning the same run replaces its summary without losing maintainer notes', async t => {
    const { args, calls } = await fixture(t, {
        body: '<!-- vulkanstorm-latest-windows-start -->\n<!-- vulkanstorm-latest-windows-run:200 -->\nold summary\n<!-- vulkanstorm-latest-windows-end -->\n\nNotes.',
    });
    await publish(args);
    const body = calls.findLast(call => call.name === 'updateRelease').args.body;
    assert.equal(body.match(/vulkanstorm-latest-windows-start/g).length, 1);
    assert.ok(body.endsWith('Notes.'));
    assert.ok(!body.includes('old summary'));
});

test('the experimental channel moves its own tag as a pre-release that never takes Latest', async t => {
    const { args, calls } = await fixture(t);
    await publish({ ...args, channel: publish.channels.experimental.windows });
    const tagUpdate = calls.find(call => call.name === 'updateRef').args;
    assert.equal(tagUpdate.ref, 'tags/experimental-windows');
    assert.equal(tagUpdate.sha, 'new-sha');
    const release = calls.findLast(call => call.name === 'updateRelease').args;
    assert.equal(release.prerelease, true);
    assert.equal(release.make_latest, 'false');
    assert.match(release.name, /experimental/);
    assert.match(release.body, /vulkanstorm-experimental-windows-run:200/);
    assert.ok(!/vulkanstorm-latest-/.test(release.body));
});

test('bootstraps a missing experimental release as a draft pre-release', async t => {
    const { args, calls } = await fixture(t, { noRelease: true, noTag: true });
    await publish({ ...args, channel: publish.channels.experimental.linux });
    assert.equal(calls[0].name, 'createRelease');
    assert.equal(calls[0].args.tag_name, 'experimental-linux');
    assert.equal(calls[0].args.prerelease, true);
    assert.ok(calls.some(call => call.name === 'createRef' && call.args.ref === 'refs/tags/experimental-linux'));
});

test('a newer run on one channel does not block the other', async t => {
    const { args, calls } = await fixture(t, { body: '<!-- vulkanstorm-latest-windows-run:999 -->' });
    await publish({ ...args, channel: publish.channels.latest.linux });
    assert.ok(calls.some(call => call.name === 'updateRef' && call.args.ref === 'tags/latest-linux'));
});

test('each platform publishes only when its own build job succeeded', async t => {
    const jobs = [
        { name: 'build (windows)', conclusion: 'success' },
        { name: 'build (linux-LTS)', conclusion: 'failure' },
        { name: 'caller / build (linux)', conclusion: 'success' },
    ];
    const requests = [];
    const github = {
        rest: { actions: { listJobsForWorkflowRun: 'list' } },
        paginate: async (route, args) => { requests.push({ route, args }); return jobs; },
    };
    const context = { repo: { owner: 'anne-skydancer', repo: 'vulkanstorm' }, runId: 200 };
    const core = { info() {} };
    const results = {};
    for (const platform of ['windows', 'linux-LTS', 'linux']) {
        results[platform] = await publish.buildSucceeded({ github, context, core, platform });
    }
    assert.deepEqual(results, { windows: true, 'linux-LTS': false, linux: true });
    assert.equal(requests[0].args.run_id, 200);
    assert.equal(requests[0].args.filter, 'latest');
    await assert.rejects(publish.buildSucceeded({ github, context, core, platform: 'Linux' }), /Unknown platform/);
    jobs.length = 0;
    assert.equal(await publish.buildSucceeded({ github, context, core, platform: 'windows' }), false);
});
