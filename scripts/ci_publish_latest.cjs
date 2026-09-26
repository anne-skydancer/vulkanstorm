// Called only after the complete Release build matrix succeeds, under the
// workflow's shared publication concurrency lock.
const fs = require('node:fs/promises');
const path = require('node:path');
const { createHash } = require('node:crypto');

module.exports = async function publishLatest({ github, context, core, directory }) {
    const repo = context.repo;
    const runId = String(context.runId);
    const files = [];
    for (const [platform, suffix] of [['Windows', '_Setup.exe'], ['Linux', '.tar.xz']]) {
        const folder = path.join(directory, `${platform}-installer`);
        const names = (await fs.readdir(folder)).filter(name => name.endsWith(suffix));
        if (names.length !== 1) throw new Error(`Expected one ${platform} installer, found ${names.length}`);
        const file = path.join(folder, names[0]);
        const stat = await fs.stat(file);
        if (!stat.isFile() || stat.size === 0) throw new Error(`Empty or invalid installer: ${file}`);
        files.push({ name: names[0], file });
    }

    let release;
    try {
        ({ data: release } = await github.rest.repos.getReleaseByTag({ ...repo, tag: 'latest' }));
    } catch (error) {
        if (error.status !== 404) throw error;
    }
    const previousRun = release?.body?.match(/<!-- vulkanstorm-latest-run:(\d+) -->/);
    if (previousRun && BigInt(previousRun[1]) > BigInt(runId)) {
        core.info('A newer CI run has already published latest; leaving it unchanged.');
        return;
    }
    let tagExists = true;
    try {
        await github.rest.git.getRef({ ...repo, ref: 'tags/latest' });
    } catch (error) {
        if (error.status !== 404) throw error;
        tagExists = false;
    }
    if (tagExists) {
        const { data: comparison } = await github.rest.repos.compareCommitsWithBasehead({
            ...repo, basehead: `latest...${context.sha}`,
        });
        if (comparison.status === 'behind') {
            core.info('This source is older than latest; leaving it unchanged.');
            return;
        }
    }
    if (!release) {
        ({ data: release } = await github.rest.repos.createRelease({
            ...repo, tag_name: 'latest', target_commitish: context.sha,
            name: 'Vulkanstorm — latest Windows and Linux Release', draft: true,
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
        await github.rest.git.updateRef({ ...repo, ref: 'tags/latest', sha: context.sha, force: true });
    } else {
        await github.rest.git.createRef({ ...repo, ref: 'refs/tags/latest', sha: context.sha });
    }
    const summary = [
        '<!-- vulkanstorm-latest-start -->',
        `<!-- vulkanstorm-latest-run:${runId} -->`,
        `Latest successful Windows and Linux CI Release: ${context.sha}.`,
        `Build: https://github.com/${repo.owner}/${repo.repo}/actions/runs/${runId}`,
        ...files.map(file => `- ${file.name}`),
        '<!-- vulkanstorm-latest-end -->',
    ].join('\n');
    const notes = (release.body || '').replace(/<!-- vulkanstorm-latest-start -->[\s\S]*?<!-- vulkanstorm-latest-end -->\s*/g, '');
    await github.rest.repos.updateRelease({
        ...repo, release_id: release.id, name: 'Vulkanstorm — latest Windows and Linux Release',
        body: `${summary}\n\n${notes}`.trim(), draft: false,
        prerelease: release.prerelease ?? false, make_latest: 'true',
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
    core.info(`Published Windows and Linux Release binaries; latest now points to ${context.sha}.`);
};
