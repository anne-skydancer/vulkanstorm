# Vulkanstorm version numbering

The viewer version is `1.0.0.<count>` on release, development and their feature branches. Canary uses `1.0.0-canary.<count>`. `<count>` is `git rev-list --count HEAD` at the source commit, including merge commits. It is not a hash or an Autobuild timestamp. Different branches can have different counts; this is expected.

`indra/newview/VIEWER_VERSION_FS.txt` specifies `1.0.0`, or `1.0.0-canary` on canary. CMake validates the label and always obtains the count from the source repository. The viewer display, generated package metadata and installer AppVersion retain the full version. Package filenames retain the existing separator conventions (for example, Windows uses `1-0-0-canary-<count>`).

C++ major/minor/patch values remain numeric. Windows binary and installer resource versions use `1.0.0.0` because each resource component is limited to 16 bits and the source count exceeds 65535; the string ProductVersion and installer UI retain the complete version. macOS short bundle versions keep the numeric three-component core.

The user authorized including this schema now, superseding the earlier instruction to wait for preceding CI qualification. Existing artifacts remain numbered from their source commit; new numbering requires a new build. CI must still qualify the change, and `latest` must still advance only after both release platform builds succeed.

Build configuration rejects shallow repositories and CI fetches complete history. VVM update comparisons parse complete stable/canary input and reject malformed versions instead of treating them as rollbacks. Velopack retains a separate numeric canary prerelease count, and both macOS plist templates use the three-component numeric bundle version. Regression checks cover these consumers.

Stable package versions also use `1.0.0.<count>`. Velopack package metadata and the macOS update locator use the same dotted version as the viewer; there is no separate stable `1.0.0-<count>` format.
