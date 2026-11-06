// SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
// Called for one platform after its own build job succeeds, under that
// platform's publication concurrency lock.
const fs = require('node:fs/promises');
const path = require('node:path');
const { createHash } = require('node:crypto');

// The build matrix's platforms (build.yaml); each uploads <platform>-installer.
// The upload paths preserve Release/ in the Windows artifact.
const platforms = {
    windows: { title: 'Windows', subdirectory: 'Release', suffix: '_Setup.exe' },
    'linux-LTS': { title: 'Linux (built in Ubuntu 24.04; runs on Ubuntu 24.04 and newer)', subdirectory: '', suffix: '-ubuntu24.04.tar.xz' },
    linux: { title: "Linux (built on GitHub's ubuntu-latest runner)", subdirectory: '', suffix: '-ubuntu-latest.tar.xz' },
};

// A channel is a tag with its release, one per platform: latest-<platform>
// follows vkstorm-release; experimental-<platform> is a manually dispatched
// Release build of vkstorm-canary, always a pre-release. Only latest-windows
// takes GitHub's Latest badge.
function channel(kind, platform) {
    const { title } = platforms[platform];
    const latest = kind === 'latest';
    return {
        tag: `${kind}-${platform}`, platform,
        name: latest ? `Vulkanstorm — latest ${title} Release` : `Vulkanstorm — experimental (vkstorm-canary) ${title} Release`,
        summary: latest ? `Latest successful ${title} CI Release` : `Experimental ${title} Release build of vkstorm-canary`,
        prerelease: !latest, makeLatest: latest && platform === 'windows',
    };
}
const channels = Object.fromEntries(['latest', 'experimental'].map(kind =>
    [kind, Object.fromEntries(Object.keys(platforms).map(platform => [platform, channel(kind, platform)]))]));

// Whether this run's build job for the platform ("build (<platform>)") succeeded.
async function buildSucceeded({ github, context, core, platform }) {
    if (!platforms[platform]) throw new Error(`Unknown platform ${platform}`);
    const name = `build (${platform})`;
    const jobs = await github.paginate(github.rest.actions.listJobsForWorkflowRun, {
        ...context.repo, run_id: context.runId, filter: 'latest', per_page: 100,
    });
    const job = jobs.find(job => job.name === name || job.name.endsWith(` / ${name}`));
    if (job?.conclusion === 'success') return true;
    core.info(`${name} ${job ? `concluded ${job.conclusion}` : 'is not in this run'}: nothing published for ${platform}.`);
    return false;
}

module.exports = async function publishLatest({ github, context, core, directory, channel }) {
    const repo = context.repo;
    const { tag, platform } = channel;
    const runId = String(context.runId);
    const { subdirectory, suffix } = platforms[platform];
    const folder = path.join(directory, `${platform}-installer`, subdirectory);
    const names = (await fs.readdir(folder)).filter(name => name.endsWith(suffix));
    if (names.length !== 1) throw new Error(`Expected one ${platform} package, found ${names.length}`);
    const file = path.join(folder, names[0]);
    const stat = await fs.stat(file);
    if (!stat.isFile() || stat.size === 0) throw new Error(`Empty or invalid package: ${file}`);
    const files = [{ name: names[0], file }];

    let release;
    try {
        ({ data: release } = await github.rest.repos.getReleaseByTag({ ...repo, tag }));
    } catch (error) {
        if (error.status !== 404) throw error;
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

    // The binary and its checksum are uploaded before replacing live names
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
    await github.rest.repos.updateRelease({
        ...repo, release_id: release.id, name: channel.name,
        body: `${summary}\n\n${notes}`.trim(), draft: false,
        prerelease: channel.prerelease || (release.prerelease ?? false), make_latest: channel.makeLatest ? 'true' : 'false',
    });
    // Retire superseded binaries only after the new one is published; preserve
    // unrelated manually attached release assets.
    const current = new Set(staged.map(asset => asset.name));
    for (const asset of assets) {
        const abandonedUpload = /(?:_Setup\.exe|\.tar\.xz|^SHA256SUMS\.txt)\.upload-\d+$/.test(asset.name)
            && !asset.name.endsWith(`.upload-${runId}`);
        if (abandonedUpload || (!current.has(asset.name) && (asset.name.endsWith('_Setup.exe') || asset.name.endsWith('.tar.xz')))) {
            await github.rest.repos.deleteReleaseAsset({ ...repo, asset_id: asset.id });
        }
    }
    core.info(`Published the ${platform} Release package; ${tag} now points to ${context.sha}.`);
};
module.exports.channels = channels;
module.exports.platforms = platforms;
module.exports.buildSucceeded = buildSucceeded;
