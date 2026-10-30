// Called only after the complete Release build matrix succeeds, under the
// workflow's shared publication concurrency lock.
const fs = require('node:fs/promises');
const path = require('node:path');
const { createHash } = require('node:crypto');

// A channel is a tag with its release. latest follows vkstorm-release; experimental
// is a manually dispatched Release build of vkstorm-canary, always a pre-release
// that never takes GitHub's Latest badge.
const channels = {
    latest: {
        tag: 'latest', name: 'Vulkanstorm — latest Windows and Linux Release',
        summary: 'Latest successful Windows and Linux CI Release', prerelease: false, makeLatest: true,
    },
    experimental: {
        tag: 'experimental', name: 'Vulkanstorm — experimental (vkstorm-canary) Windows and Linux Release',
        summary: 'Experimental Windows and Linux Release build of vkstorm-canary', prerelease: true, makeLatest: false,
    },
};

module.exports = async function publishLatest({ github, context, core, directory, channel = channels.latest }) {
    const repo = context.repo;
    const { tag } = channel;
    const runId = String(context.runId);
    const files = [];
    // The upload paths in build.yaml preserve Release/ in the Windows artifact.
    for (const [platform, subdirectory, suffix] of [['Windows', 'Release', '_Setup.exe'], ['Linux', '', '.tar.xz']]) {
        const folder = path.join(directory, `${platform}-installer`, subdirectory);
        const names = (await fs.readdir(folder)).filter(name => name.endsWith(suffix));
        if (names.length !== 1) throw new Error(`Expected one ${platform} installer, found ${names.length}`);
        const file = path.join(folder, names[0]);
        const stat = await fs.stat(file);
        if (!stat.isFile() || stat.size === 0) throw new Error(`Empty or invalid installer: ${file}`);
        files.push({ name: names[0], file });
    }

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

    // Both binaries and their checksums are uploaded before replacing live names
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
    await github.rest.repos.updateRelease({
        ...repo, release_id: release.id, name: channel.name, target_commitish: context.sha,
        body: `${summary}\n\n${notes}`.trim(), draft: false,
        prerelease: channel.prerelease || (release.prerelease ?? false), make_latest: channel.makeLatest ? 'true' : 'false',
    });
    // Retire superseded binaries only after the new pair is published; preserve
    // unrelated manually attached release assets.
    const current = new Set(staged.map(asset => asset.name));
    for (const asset of assets) {
        const abandonedUpload = /(?:_Setup\.exe|\.tar\.xz|^SHA256SUMS\.txt)\.upload-\d+$/.test(asset.name)
            && !asset.name.endsWith(`.upload-${runId}`);
        if (abandonedUpload || (!current.has(asset.name) && (asset.name.endsWith('_Setup.exe') || asset.name.endsWith('.tar.xz')))) {
            await github.rest.repos.deleteReleaseAsset({ ...repo, asset_id: asset.id });
        }
    }
    core.info(`Published Windows and Linux Release binaries; ${tag} now points to ${context.sha}.`);
};
module.exports.channels = channels;
