// SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
// Promotes already published packages to stable (promote-stable.yaml): one
// package each of windows, linux and linux-lts, taken from the releases named
// (windows-latest, linux-latest and linux-lts by default), nothing rebuilt.
// The rotation keeps the semantics it had by hand:
//   1. the outgoing oldstable packages go to archive/stable-installers, with
//      SHA256SUMS-<version>.txt and a row in its table;
//   2. oldstable takes stable's packages, notes and commit;
//   3. stable takes the promoted packages and moves to the Windows source commit.
// Each step uploads before it replaces, so a failed run leaves the live
// releases as they were or one step further, and a rerun continues.
const { createHash } = require('node:crypto');
const { platforms } = require('./ci_publish_latest.cjs');

const ARCHIVE = 'archive/stable-installers';
const STABLE_PLATFORMS = ['windows', 'linux', 'linux-lts'];
const binary = name => /(?:_Setup\.exe|\.tar\.xz)$/.test(name);

// 1-0-1-81917 in a package name is version 1.0.1.81917.
function versionOf(name) {
    const match = name.match(/(\d+)-(\d+)-(\d+)-(\d+)(?=_Setup\.exe$|(?:-(?!\d)[^-]+)*\.tar\.xz$)/);
    return match ? match.slice(1).join('.') : undefined;
}

module.exports = async function promoteStable({ github, context, core, sources }) {
    const repo = context.repo;
    const runId = String(context.runId);

    async function release(tag) {
        try {
            return (await github.rest.repos.getReleaseByTag({ ...repo, tag })).data;
        } catch (error) {
            if (error.status === 404) return undefined;
            throw error;
        }
    }
    async function commitOf(tag) {
        const { data: ref } = await github.rest.git.getRef({ ...repo, ref: `tags/${tag}` });
        if (ref.object.type !== 'tag') return ref.object.sha;
        return (await github.rest.git.getTag({ ...repo, tag_sha: ref.object.sha })).data.object.sha;
    }
    async function assetsOf(releaseData) {
        return github.paginate(github.rest.repos.listReleaseAssets, { ...repo, release_id: releaseData.id, per_page: 100 });
    }
    async function download(asset) {
        const { data } = await github.request('GET /repos/{owner}/{repo}/releases/assets/{asset_id}', {
            ...repo, asset_id: asset.id, headers: { accept: 'application/octet-stream' },
        });
        const buffer = Buffer.from(data);
        if (buffer.length === 0 || (asset.size !== undefined && buffer.length !== asset.size)) {
            throw new Error(`Incomplete download of ${asset.name}`);
        }
        return buffer;
    }
    const checksums = files => Buffer.from(files.map(file =>
        `${createHash('sha256').update(file.data).digest('hex')}  ${file.name}`).join('\n') + '\n');

    // Replace a release's packages (and SHA256SUMS.txt) with files: staged
    // uploads first, then the swap; unrelated assets stay.
    async function replacePackages(target, files) {
        const assets = await assetsOf(target);
        const staged = [];
        for (const file of [...files, { name: 'SHA256SUMS.txt', data: checksums(files) }]) {
            const temporaryName = `${file.name}.upload-${runId}`;
            for (const old of assets.filter(old => old.name === temporaryName)) {
                await github.rest.repos.deleteReleaseAsset({ ...repo, asset_id: old.id });
            }
            const { data: asset } = await github.rest.repos.uploadReleaseAsset({
                ...repo, release_id: target.id, name: temporaryName, data: file.data,
                headers: { 'content-type': 'application/octet-stream', 'content-length': file.data.length },
            });
            staged.push({ id: asset.id, name: file.name });
        }
        const current = new Set(staged.map(asset => asset.name));
        for (const old of assets.filter(old => (binary(old.name) || old.name === 'SHA256SUMS.txt') && !old.name.includes('.upload-'))) {
            await github.rest.repos.deleteReleaseAsset({ ...repo, asset_id: old.id });
        }
        for (const asset of staged) {
            await github.rest.repos.updateReleaseAsset({ ...repo, asset_id: asset.id, name: asset.name });
        }
        return current;
    }
    async function moveTag(tag, sha) {
        await github.rest.git.updateRef({ ...repo, ref: `tags/${tag}`, sha, force: true });
    }

    // The packages to promote, all read before anything is written.
    const promoted = [];
    for (const platform of STABLE_PLATFORMS) {
        const tag = sources[platform];
        if (!tag || tag.startsWith('freebsd-image-') || tag === 'stable' || tag === 'oldstable' || tag.startsWith('archive/')) {
            throw new Error(`${tag || '(none)'} is not a viewer build release to promote for ${platform}`);
        }
        const source = await release(tag);
        if (!source) throw new Error(`No release ${tag} for ${platform}`);
        const suffix = platforms[platform].package.suffix;
        const matches = (await assetsOf(source)).filter(asset => asset.name.endsWith(suffix) && !asset.name.includes('.upload-'));
        if (matches.length !== 1) throw new Error(`Expected one ${platform} package in ${tag}, found ${matches.length}`);
        promoted.push({ platform, tag, asset: matches[0], name: matches[0].name, commit: await commitOf(tag) });
    }
    const version = versionOf(promoted[0].name);
    if (!version) throw new Error(`No version in ${promoted[0].name}`);
    for (const file of promoted) {
        if (versionOf(file.name) !== version) core.warning(`${file.name} is not version ${version}`);
    }

    const stable = await release('stable');
    const oldstable = await release('oldstable');
    const archive = await release(ARCHIVE);
    if (!stable || !oldstable || !archive) throw new Error('stable, oldstable and archive/stable-installers must exist');
    const stablePackages = (await assetsOf(stable)).filter(asset => binary(asset.name) && !asset.name.includes('.upload-'));
    const stableVersion = stablePackages.map(asset => versionOf(asset.name)).find(Boolean);
    if (stableVersion === version) {
        core.info(`stable is already ${version}; nothing to promote.`);
        return;
    }
    for (const file of promoted) file.data = await download(file.asset);

    // 1. Archive the outgoing oldstable packages.
    const oldPackages = (await assetsOf(oldstable)).filter(asset => binary(asset.name) && !asset.name.includes('.upload-'));
    const oldVersion = oldPackages.map(asset => versionOf(asset.name)).find(Boolean);
    if (oldVersion) {
        const archived = new Set((await assetsOf(archive)).map(asset => asset.name));
        const files = [];
        for (const asset of oldPackages) files.push({ name: asset.name, data: await download(asset) });
        for (const file of [...files, { name: `SHA256SUMS-${oldVersion}.txt`, data: checksums(files) }]) {
            if (archived.has(file.name)) continue;
            await github.rest.repos.uploadReleaseAsset({
                ...repo, release_id: archive.id, name: file.name, data: file.data,
                headers: { 'content-type': 'application/octet-stream', 'content-length': file.data.length },
            });
        }
        const row = `| ${oldVersion} | ${await commitOf('oldstable')} | oldstable until ${new Date().toISOString().slice(0, 10)} | |`;
        if (!(archive.body || '').includes(`| ${oldVersion} |`)) {
            const body = (archive.body || '').replace(/(\|---\|[^\n]*\n)/, `$1${row}\n`);
            await github.rest.repos.updateRelease({ ...repo, release_id: archive.id, body: body.includes(row) ? body : `${body}\n${row}\n` });
        }
    }

    // 2. oldstable takes stable.
    const stableFiles = [];
    for (const asset of stablePackages) stableFiles.push({ name: asset.name, data: await download(asset) });
    await replacePackages(oldstable, stableFiles);
    await moveTag('oldstable', await commitOf('stable'));
    const stableNotes = (stable.body || '').replace(/<!-- vulkanstorm-stable-start -->[\s\S]*?<!-- vulkanstorm-stable-end -->\s*/g, '').trim();
    await github.rest.repos.updateRelease({
        ...repo, release_id: oldstable.id, name: `Vulkanstorm ${stableVersion} — Old stable`, make_latest: 'false',
        body: `**Old stable:** the previous stable release, kept available after stable moved to ${version}.\n\n${stableNotes}`,
    });

    // 3. stable takes the promoted packages.
    await replacePackages(stable, promoted.map(({ name, data }) => ({ name, data })));
    await moveTag('stable', promoted[0].commit);
    const summary = [
        '<!-- vulkanstorm-stable-start -->',
        `<!-- vulkanstorm-stable-run:${runId} -->`,
        `Stable ${version}, promoted by https://github.com/${repo.owner}/${repo.repo}/actions/runs/${runId}:`,
        ...promoted.map(file => `- ${file.name}: from ${file.tag} (${file.commit})`),
        '<!-- vulkanstorm-stable-end -->',
    ].join('\n');
    await github.rest.repos.updateRelease({
        ...repo, release_id: stable.id, name: `Vulkanstorm ${version} — Stable`, make_latest: 'false',
        body: `${summary}\n\n${stableNotes}`.trim(),
    });
    core.info(`stable is now ${version}; oldstable is ${stableVersion}${oldVersion ? `; ${oldVersion} is archived` : ''}.`);
};
module.exports.versionOf = versionOf;
module.exports.STABLE_PLATFORMS = STABLE_PLATFORMS;
