// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
const { test } = require('node:test');
const assert = require('node:assert/strict');
const promote = require('../ci_promote_stable.cjs');

const NEW = '1-0-2-82100';
function fixture(options = {}) {
    const calls = [];
    let nextId = 1000;
    const releases = {
        'windows-latest': { id: 1, assets: [{ id: 11, name: `Vulkanstorm-Release_AVX2-${NEW}_Setup.exe`, size: 3 }] },
        'linux-latest': { id: 2, assets: [{ id: 21, name: `Vulkanstorm-Release_AVX2-${NEW}-ubuntu-latest.tar.xz`, size: 3 }] },
        'linux-lts': { id: 3, assets: [{ id: 31, name: `Vulkanstorm-Release_AVX2-${NEW}-ubuntu24.04.tar.xz`, size: 3 }] },
        stable: { id: 4, body: '<!-- vulkanstorm-stable-start -->\nold\n<!-- vulkanstorm-stable-end -->\n\nStable notes.', assets: [
            { id: 41, name: 'Vulkanstorm-Release_AVX2-1-0-1-81917_Setup.exe', size: 3 },
            { id: 42, name: 'Vulkanstorm-Release_AVX2-1-0-1-81917.tar.xz', size: 3 },
            { id: 43, name: 'SHA256SUMS.txt', size: 3 },
        ] },
        oldstable: { id: 5, body: 'Old notes.', assets: [
            { id: 51, name: 'Vulkanstorm-Release_AVX2-1-0-0-80844_Setup.exe', size: 3 },
            { id: 52, name: 'Vulkanstorm-Release_AVX2-1-0-0-80844.tar.xz', size: 3 },
            { id: 53, name: 'SHA256SUMS.txt', size: 3 },
        ] },
        'archive/stable-installers': { id: 6, body: '| Version | Source commit | Was | Base |\n|---|---|---|---|\n| 1.0.0.80779 | x | y | z |\n', assets: [] },
        'freebsd-image-15.1-20261016-53cc663': { id: 7, assets: [{ id: 71, name: 'image.qcow2.xz', size: 3 }] },
        ...options.releases,
    };
    const byId = Object.fromEntries(Object.values(releases).map(release => [release.id, release]));
    const record = name => async args => { calls.push({ name, args }); return { data: {} }; };
    const github = {
        rest: {
            repos: {
                getReleaseByTag: async ({ tag }) => {
                    if (!releases[tag]) throw Object.assign(new Error('Not found'), { status: 404 });
                    return { data: { id: releases[tag].id, body: releases[tag].body } };
                },
                listReleaseAssets: 'list',
                uploadReleaseAsset: async args => {
                    calls.push({ name: 'uploadReleaseAsset', args });
                    return { data: { id: nextId++ } };
                },
                deleteReleaseAsset: record('deleteReleaseAsset'),
                updateReleaseAsset: record('updateReleaseAsset'),
                updateRelease: record('updateRelease'),
            },
            git: {
                getRef: async ({ ref }) => ({ data: { object: { type: 'commit', sha: `sha-of-${ref.slice(5)}` } } }),
                getTag: async () => { throw new Error('unexpected'); },
                updateRef: record('updateRef'),
            },
        },
        paginate: async (route, { release_id }) => byId[release_id].assets,
        request: async (route, { asset_id }) => { calls.push({ name: 'download', args: { asset_id } }); return { data: Buffer.from('abc') }; },
    };
    return {
        calls,
        args: {
            github, core: { info() {}, warning() {} },
            context: { repo: { owner: 'anne-skydancer', repo: 'vulkanstorm' }, runId: 300 },
            sources: { windows: 'windows-latest', linux: 'linux-latest', 'linux-lts': 'linux-lts', ...options.sources },
        },
    };
}

test('versions are read from the package names', () => {
    assert.equal(promote.versionOf('Vulkanstorm-Release_AVX2-1-0-1-81917_Setup.exe'), '1.0.1.81917');
    assert.equal(promote.versionOf('Vulkanstorm-Release_AVX2-1-0-1-81917.tar.xz'), '1.0.1.81917');
    assert.equal(promote.versionOf(`Vulkanstorm-Release_AVX2-${NEW}-ubuntu24.04.tar.xz`), '1.0.2.82100');
    assert.equal(promote.versionOf(`Vulkanstorm-Release_AVX2-${NEW}-ubuntu-latest.tar.xz`), '1.0.2.82100');
    assert.equal(promote.versionOf('SHA256SUMS.txt'), undefined);
});

test('archives oldstable, moves stable to oldstable, then promotes the three packages', async () => {
    const { args, calls } = fixture();
    await promote(args);
    const order = calls.filter(call => ['uploadReleaseAsset', 'updateRef'].includes(call.name))
        .map(call => call.name === 'updateRef' ? `tag ${call.args.ref}` : `${call.args.release_id} ${call.args.name}`);
    assert.deepEqual(order, [
        '6 Vulkanstorm-Release_AVX2-1-0-0-80844_Setup.exe',
        '6 Vulkanstorm-Release_AVX2-1-0-0-80844.tar.xz',
        '6 SHA256SUMS-1.0.0.80844.txt',
        '5 Vulkanstorm-Release_AVX2-1-0-1-81917_Setup.exe.upload-300',
        '5 Vulkanstorm-Release_AVX2-1-0-1-81917.tar.xz.upload-300',
        '5 SHA256SUMS.txt.upload-300',
        'tag tags/oldstable',
        `4 Vulkanstorm-Release_AVX2-${NEW}_Setup.exe.upload-300`,
        `4 Vulkanstorm-Release_AVX2-${NEW}-ubuntu-latest.tar.xz.upload-300`,
        `4 Vulkanstorm-Release_AVX2-${NEW}-ubuntu24.04.tar.xz.upload-300`,
        '4 SHA256SUMS.txt.upload-300',
        'tag tags/stable',
    ]);
    const tags = Object.fromEntries(calls.filter(call => call.name === 'updateRef').map(call => [call.args.ref, call.args.sha]));
    assert.deepEqual(tags, { 'tags/oldstable': 'sha-of-stable', 'tags/stable': 'sha-of-windows-latest' });
    const deleted = calls.filter(call => call.name === 'deleteReleaseAsset').map(call => call.args.asset_id);
    assert.deepEqual(deleted.sort(), [41, 42, 43, 51, 52, 53]);
    const updates = calls.filter(call => call.name === 'updateRelease').map(call => call.args);
    assert.match(updates[0].body, /\| 1\.0\.0\.80844 \| sha-of-oldstable \|/);
    assert.equal(updates[1].name, 'Vulkanstorm 1.0.1.81917 — Old stable');
    assert.match(updates[1].body, /^\*\*Old stable:\*\*[\s\S]*Stable notes\.$/);
    assert.ok(!updates[1].body.includes('vulkanstorm-stable-start'));
    assert.equal(updates[2].name, 'Vulkanstorm 1.0.2.82100 — Stable');
    assert.match(updates[2].body, /from linux-lts \(sha-of-linux-lts\)/);
    assert.ok(updates.every(update => update.make_latest !== 'true'));
});

test('nothing is written when a source is missing its package', async () => {
    const { args, calls } = fixture({ releases: { 'linux-lts': { id: 3, assets: [] } } });
    await assert.rejects(promote(args), /Expected one linux-lts package in linux-lts/);
    assert.equal(calls.length, 0);
});

test('the FreeBSD CI image and the stable releases themselves are never promoted', async () => {
    for (const tag of ['freebsd-image-15.1-20261016-53cc663', 'stable', 'oldstable', 'archive/stable-installers']) {
        const { args, calls } = fixture({ sources: { linux: tag } });
        await assert.rejects(promote(args), /is not a viewer build release/);
        assert.equal(calls.length, 0);
    }
});

test('promoting the version stable already has changes nothing', async () => {
    const { args, calls } = fixture({ releases: { stable: { id: 4, body: '', assets: [
        { id: 41, name: `Vulkanstorm-Release_AVX2-${NEW}_Setup.exe`, size: 3 },
    ] } } });
    await promote(args);
    assert.ok(!calls.some(call => call.name !== 'download'));
});
