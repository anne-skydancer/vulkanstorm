// SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
// Plans build.yaml's build matrix and publication, and publishes one channel
// (a tag with its release) after its platform's build succeeded
// (viewer-platform.yaml), under that channel's publication concurrency lock. doc/release_channels.md describes
// the channels.
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

// The build matrix and the channels this run publishes. Pushes build the
// automatic platforms (windows, linux), pull requests also linux-lts, all as
// unpublished RelWithDebInfo qualification builds except on vkstorm-release;
// workflow_dispatch builds the platforms its inputs select. vkstorm-release publishes its pushes (and
// schedules); a dispatch publishes only with publish set, on the three lines.
function plan({ eventName, ref, repository, inputs }) {
    inputs = inputs || {};
    const dispatch = eventName === 'workflow_dispatch';
    const truthy = value => value === true || value === 'true';
    const selected = Object.keys(platforms).filter(platform => dispatch
        ? truthy(inputs[platforms[platform].dispatch_input] ?? platforms[platform].automatic)
        : platforms[platform].automatic || (eventName === 'pull_request' && platforms[platform].pull_requests === true));
    const line = lines[ref];
    const publishes = repository === 'anne-skydancer/vulkanstorm' && line !== undefined
        && ((line.kind === 'main' && ['push', 'schedule'].includes(eventName)) || (dispatch && truthy(inputs.publish)));
    const tagOf = platform => publishes ? channelFor(line.kind, platform).tag : '';
    return {
        // Release packages: vkstorm-release, Second_Life tags, a dispatch with publish.
        release: ref === 'refs/heads/vkstorm-release' || ref.startsWith('refs/tags/Second_Life') || (dispatch && truthy(inputs.publish)),
        matrix: { include: selected.map(platform => ({ platform, ...platforms[platform].build, tag: tagOf(platform) })) },
        publish: publishes ? selected.map(platform => ({ platform, tag: tagOf(platform) })) : [],
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

// Whether this run's build job for the platform ("<platform> / build", from
// viewer-platform.yaml) succeeded.
async function buildSucceeded({ github, context, core, platform }) {
    if (!platforms[platform]) throw new Error(`Unknown platform ${platform}`);
    const name = `${platform} / build`;
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
    await github.rest.repos.updateRelease({
        ...repo, release_id: release.id, name: channel.name,
        body: `${summary}\n\n${notes}`.trim(), draft: false,
        prerelease: channel.prerelease || (release.prerelease ?? false), make_latest: channel.makeLatest ? 'true' : 'false',
    });
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
module.exports.plan = plan;
module.exports.buildSucceeded = buildSucceeded;
module.exports.packageFile = packageFile;
