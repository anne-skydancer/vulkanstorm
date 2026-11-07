// SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
// Plans one platform workflow's run (build-<platform>.yaml) and publishes its
// channel (a tag with its release) after that platform's build succeeded,
// under the channel's publication concurrency lock. doc/release_channels.md
// describes the channels.
const fs = require('node:fs/promises');
const path = require('node:path');
const { createHash } = require('node:crypto');

// The CI platforms (.github/build-platforms.json): a FreeBSD build
// (freebsd-latest, planned) is one more entry there.
const platforms = Object.fromEntries(Object.entries(require('../.github/build-platforms.json'))
    .filter(([name]) => !name.startsWith('_')));

// The lines that publish, and the tag each gives a platform: vkstorm-release
// publishes windows-latest, linux-latest and linux-lts; vkstorm-canary
// experimental-<platform>, vkstorm-devel preview-<platform>, always as
// pre-releases. Only windows-latest takes GitHub's Latest badge. No other line
// publishes, and no channel is a freebsd-image-* (CI infrastructure) release.
const lines = {
    'refs/heads/vkstorm-release': { kind: 'main', source: 'vkstorm-release' },
    'refs/heads/vkstorm-canary': { kind: 'experimental', source: 'vkstorm-canary' },
    'refs/heads/vkstorm-devel': { kind: 'preview', source: 'vkstorm-devel' },
};

function channelFor(kind, platform) {
    const { title, tags } = platforms[platform];
    const source = Object.values(lines).find(line => line.kind === kind).source;
    if (kind === 'main') {
        return {
            tag: tags.main, platforms: [platform], name: `Vulkanstorm — ${tags.main} (${title} Release)`,
            summary: `${title} Release of ${source}`, prerelease: false, makeLatest: tags.main === 'windows-latest',
        };
    }
    const tag = `${kind}-${tags.line}`;
    return {
        tag, platforms: [platform], name: `Vulkanstorm — ${tag} (${title} Release of ${source})`,
        summary: `${kind[0].toUpperCase()}${kind.slice(1)} ${title} Release build of ${source}`,
        prerelease: true, makeLatest: false,
    };
}
const channels = Object.fromEntries(Object.values(lines).flatMap(({ kind }) =>
    Object.keys(platforms).map(platform => channelFor(kind, platform))).map(channel => [channel.tag, channel]));

// One platform workflow's plan (build-<platform>.yaml, doc/release_channels.md):
// its build entry, whether it builds a Release package, and the tag it
// publishes to ('' for none). vkstorm-release publishes its automatic
// platforms (windows, linux) on every push; a dispatch with publish set
// publishes the dispatched platform on the three publishing lines.
function planPlatform({ eventName, ref, repository, inputs, platform }) {
    if (!platforms[platform]) throw new Error(`Unknown platform ${platform}`);
    inputs = inputs || {};
    const truthy = value => value === true || value === 'true';
    const dispatch = eventName === 'workflow_dispatch';
    const line = lines[ref];
    const publishes = repository === 'anne-skydancer/vulkanstorm' && line !== undefined
        && ((line.kind === 'main' && platforms[platform].automatic && ['push', 'schedule'].includes(eventName))
            || (dispatch && truthy(inputs.publish)));
    return {
        platform,
        entry: { platform, ...platforms[platform].build },
        release: ref === 'refs/heads/vkstorm-release' || (dispatch && truthy(inputs.publish)),
        tag: publishes ? channelFor(line.kind, platform).tag : '',
    };
}

// The one package of a platform's downloaded artifact.
async function packageFile(directory, platform) {
    const { subdirectory, suffix } = platforms[platform].package;
    const folder = path.join(directory, `${platform}-installer`, subdirectory);
    let names = [];
    try {
        names = (await fs.readdir(folder)).filter(name => name.endsWith(suffix));
    } catch (error) {
        if (error.code !== 'ENOENT') throw error;
    }
    if (names.length !== 1) throw new Error(`Expected one ${platform} package, found ${names.length}`);
    const file = path.join(folder, names[0]);
    const stat = await fs.stat(file);
    if (!stat.isFile() || stat.size === 0) throw new Error(`Empty or invalid package: ${file}`);
    return { name: names[0], file };
}

// <Vulkanstorm> The final republish is retried a few times: it can fail after
// the release was returned to draft, and a re-run finds that draft again.
const publishAttempts = 3;

module.exports = async function publishLatest({ github, context, core, directory, channel, retryDelayMs = 2000 }) {
    const repo = context.repo;
    const { tag } = channel;
    const runId = String(context.runId);
    const files = [];
    for (const platform of channel.platforms) files.push(await packageFile(directory, platform));

    let release;
    try {
        ({ data: release } = await github.rest.repos.getReleaseByTag({ ...repo, tag }));
    } catch (error) {
        if (error.status !== 404) throw error;
    }
    if (!release) {
        // getReleaseByTag never returns a draft: look for one a failed run left behind.
        const releases = await github.paginate(github.rest.repos.listReleases, { ...repo, per_page: 100 });
        release = releases.find(candidate => candidate.draft && candidate.tag_name === tag);
        if (release) core.info(`Resuming the draft release left for ${tag}.`);
    }
    const previousRun = release?.body?.match(new RegExp(`<!-- vulkanstorm-${tag}-run:(\\d+) -->`));
    if (previousRun && BigInt(previousRun[1]) > BigInt(runId)) {
        core.info(`A newer CI run has already published ${tag}; leaving it unchanged.`);
        return;
    }
    let tagExists = true;
    try {
        await github.rest.git.getRef({ ...repo, ref: `tags/${tag}` });
    } catch (error) {
        if (error.status !== 404) throw error;
        tagExists = false;
    }
    if (tagExists) {
        const { data: comparison } = await github.rest.repos.compareCommitsWithBasehead({
            ...repo, basehead: `${tag}...${context.sha}`,
        });
        if (comparison.status === 'behind') {
            core.info(`This source is older than ${tag}; leaving it unchanged.`);
            return;
        }
    }
    if (!release) {
        ({ data: release } = await github.rest.repos.createRelease({
            ...repo, tag_name: tag, target_commitish: context.sha,
            name: channel.name, draft: true, prerelease: channel.prerelease,
        }));
    }
    const assets = await github.paginate(github.rest.repos.listReleaseAssets, {
        ...repo, release_id: release.id, per_page: 100,
    });
    const staged = [];
    const checksums = [];
    async function upload(name, data) {
        const temporaryName = `${name}.upload-${runId}`;
        // A rerun can resume after a failed upload without touching live assets.
        for (const asset of assets.filter(asset => asset.name === temporaryName)) {
            await github.rest.repos.deleteReleaseAsset({ ...repo, asset_id: asset.id });
        }
        const { data: asset } = await github.rest.repos.uploadReleaseAsset({
            ...repo, release_id: release.id, name: temporaryName, data,
            headers: { 'content-type': 'application/octet-stream', 'content-length': data.length },
        });
        staged.push({ id: asset.id, name });
    }
    for (const { name, file } of files) {
        const data = await fs.readFile(file);
        checksums.push(`${createHash('sha256').update(data).digest('hex')}  ${name}`);
        await upload(name, data);
    }
    await upload('SHA256SUMS.txt', Buffer.from(checksums.join('\n') + '\n'));

    // The packages and their checksums are uploaded before replacing live names
    // or moving the source tag. GitHub does not offer an atomic release update.
    for (const asset of staged) {
        for (const old of assets.filter(old => old.name === asset.name)) {
            await github.rest.repos.deleteReleaseAsset({ ...repo, asset_id: old.id });
        }
        await github.rest.repos.updateReleaseAsset({ ...repo, asset_id: asset.id, name: asset.name });
    }
    if (tagExists) {
        await github.rest.git.updateRef({ ...repo, ref: `tags/${tag}`, sha: context.sha, force: true });
    } else {
        await github.rest.git.createRef({ ...repo, ref: `refs/tags/${tag}`, sha: context.sha });
    }
    const summary = [
        `<!-- vulkanstorm-${tag}-start -->`,
        `<!-- vulkanstorm-${tag}-run:${runId} -->`,
        `${channel.summary}: ${context.sha}.`,
        `Build: https://github.com/${repo.owner}/${repo.repo}/actions/runs/${runId}`,
        ...files.map(file => `- ${file.name}`),
        `<!-- vulkanstorm-${tag}-end -->`,
    ].join('\n');
    const marker = new RegExp(`<!-- vulkanstorm-${tag}-start -->[\\s\\S]*?<!-- vulkanstorm-${tag}-end -->\\s*`, 'g');
    const notes = (release.body || '').replace(marker, '');
    // GitHub dates a release when it is first published and never again, and
    // keeps the commit it was created on. Return a published release to draft
    // first, so that publishing it again dates it with this build; the tag and
    // assets stay in place throughout.
    if (!release.draft) {
        await github.rest.repos.updateRelease({
            ...repo, release_id: release.id, draft: true, target_commitish: context.sha,
        });
    }
    for (let attempt = 1; ; attempt++) {
        try {
            await github.rest.repos.updateRelease({
                ...repo, release_id: release.id, name: channel.name, target_commitish: context.sha,
                body: `${summary}\n\n${notes}`.trim(), draft: false,
                prerelease: channel.prerelease || (release.prerelease ?? false), make_latest: channel.makeLatest ? 'true' : 'false',
            });
            break;
        } catch (error) {
            if (attempt >= publishAttempts) throw error;
            core.info(`Publishing ${tag} failed (attempt ${attempt}): ${error.message}; retrying.`);
            await new Promise(resolve => setTimeout(resolve, retryDelayMs * attempt));
        }
    }
    // Retire superseded packages only after the new ones are published; preserve
    // unrelated manually attached release assets.
    const current = new Set(staged.map(asset => asset.name));
    for (const asset of assets) {
        const abandonedUpload = /(?:_Setup\.exe|\.tar\.xz|^SHA256SUMS\.txt)\.upload-\d+$/.test(asset.name)
            && !asset.name.endsWith(`.upload-${runId}`);
        if (abandonedUpload || (!current.has(asset.name) && (asset.name.endsWith('_Setup.exe') || asset.name.endsWith('.tar.xz')))) {
            await github.rest.repos.deleteReleaseAsset({ ...repo, asset_id: asset.id });
        }
    }
    core.info(`Published ${channel.platforms.join(' and ')} for ${tag}; it now points to ${context.sha}.`);
};
module.exports.channels = channels;
module.exports.platforms = platforms;
module.exports.planPlatform = planPlatform;
module.exports.packageFile = packageFile;
