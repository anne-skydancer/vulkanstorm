const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { createHash } = require('node:crypto');
const publish = require('../ci_publish_latest.cjs');

async function fixture(t, options = {}) {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'vulkanstorm-publish-'));
    t.after(() => fs.rm(directory, { recursive: true, force: true }));
    for (const [platform, filename] of [['Windows', 'new_Setup.exe'], ['Linux', 'new.tar.xz']]) {
        // Match actions/download-artifact's layout for the CI upload paths.
        const folder = path.join(directory, `${platform}-installer`, platform === 'Windows' ? 'Release' : '');
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
        'createRelease', 'deleteReleaseAsset', 'updateReleaseAsset', 'updateRelease',
    ].map(name => [name, record(name)]));
    repos.getReleaseByTag = async () => {
        if (options.noRelease) throw Object.assign(new Error('Not found'), { status: 404 });
        return { data: { id: 10, body: options.body || 'Maintainer notes.', prerelease: false } };
    };
    repos.compareCommitsWithBasehead = async () => {
        if (options.comparisonError) throw Object.assign(new Error('Comparison failed'), { status: 404 });
        return { data: { status: options.comparison || 'ahead' } };
    };
    repos.listReleaseAssets = () => {};
    repos.uploadReleaseAsset = async args => {
        if (options.failLinux && args.name.startsWith('new.tar.xz')) throw new Error('Upload failed');
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
            { id: 101, name: 'old.tar.xz' },
            { id: 102, name: 'SHA256SUMS.txt' },
            { id: 103, name: 'manual-notes.txt' },
        ],
    };
    return {
        calls,
        args: { github, directory, core: { info() {} },
            context: { repo: { owner: 'anne-skydancer', repo: 'vulkanstorm' }, runId: 200, sha: 'new-sha' } },
    };
}

test('publishes both binaries and verified checksums before moving latest', async t => {
    const { args, calls } = await fixture(t);
    await publish(args);
    const uploads = calls.filter(call => call.name === 'uploadReleaseAsset');
    assert.equal(uploads.length, 3);
    const checksum = uploads[2].args.data.toString();
    for (const [content, name] of [['Windows', 'new_Setup.exe'], ['Linux', 'new.tar.xz']]) {
        assert.ok(checksum.includes(`${createHash('sha256').update(content).digest('hex')}  ${name}`));
    }
    const tagIndex = calls.findIndex(call => call.name === 'updateRef');
    assert.ok(calls.slice(0, tagIndex).filter(call => call.name === 'updateReleaseAsset').length === 3);
    assert.deepEqual(calls[tagIndex].args, {
        owner: 'anne-skydancer', repo: 'vulkanstorm', ref: 'tags/latest', sha: 'new-sha', force: true,
    });
    const release = calls.find(call => call.name === 'updateRelease').args;
    assert.match(release.body, /vulkanstorm-latest-run:200/);
    assert.match(release.body, /Maintainer notes/);
    assert.equal(release.prerelease, false);
    assert.ok(!calls.some(call => call.name === 'deleteReleaseAsset' && call.args.asset_id === 103));
    assert.ok(calls.findIndex(call => call.name === 'deleteReleaseAsset' && call.args.asset_id === 100) > tagIndex);
});

test('missing platform artifacts prevent all publication writes', async t => {
    const { args, calls } = await fixture(t, { missing: 'Linux' });
    await assert.rejects(publish(args), /Expected one Linux installer/);
    assert.equal(calls.length, 0);
});

test('failed second upload leaves the tag and live assets untouched', async t => {
    const { args, calls } = await fixture(t, { failLinux: true });
    await assert.rejects(publish(args), /Upload failed/);
    assert.deepEqual(calls.map(call => call.name), ['uploadReleaseAsset']);
});

test('late completion of an older run cannot replace a newer publication', async t => {
    const { args, calls } = await fixture(t, { body: '<!-- vulkanstorm-latest-run:201 -->' });
    await publish(args);
    assert.equal(calls.length, 0);
});

test('a newly dispatched build of older source cannot move latest backwards', async t => {
    const { args, calls } = await fixture(t, { comparison: 'behind' });
    await publish(args);
    assert.equal(calls.length, 0);
});

test('bootstraps a missing latest release and tag', async t => {
    const { args, calls } = await fixture(t, { noRelease: true, noTag: true });
    await publish(args);
    assert.equal(calls[0].name, 'createRelease');
    assert.equal(calls[0].args.draft, true);
    assert.ok(calls.some(call => call.name === 'createRef' && call.args.ref === 'refs/tags/latest'));
    assert.equal(calls.find(call => call.name === 'updateRelease').args.draft, false);
});

test('a comparison API failure does not get treated as a missing tag', async t => {
    const { args, calls } = await fixture(t, { comparisonError: true });
    await assert.rejects(publish(args), /Comparison failed/);
    assert.equal(calls.length, 0);
});

test('rerunning the same run replaces its summary without losing maintainer notes', async t => {
    const { args, calls } = await fixture(t, {
        body: '<!-- vulkanstorm-latest-start -->\n<!-- vulkanstorm-latest-run:200 -->\nold summary\n<!-- vulkanstorm-latest-end -->\n\nNotes.',
    });
    await publish(args);
    const body = calls.find(call => call.name === 'updateRelease').args.body;
    assert.equal(body.match(/vulkanstorm-latest-start/g).length, 1);
    assert.ok(body.endsWith('Notes.'));
    assert.ok(!body.includes('old summary'));
});
