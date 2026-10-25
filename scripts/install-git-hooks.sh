#!/bin/sh
# Installs the repository's git hooks into the shared hooks directory, so they
# apply to the main checkout and to every worktree.
#   pre-merge-commit: refuse to merge a base line into a feature branch
#   pre-commit:       the same check when a conflicted merge is concluded by
#                     "git commit"; an existing pre-commit hook is kept and
#                     called after it.
set -e
root=$(git rev-parse --show-toplevel)
hooks=$(git rev-parse --git-common-dir)/hooks
mkdir -p "$hooks"
cp "$root/scripts/git-hooks/pre-merge-commit" "$hooks/pre-merge-commit"
cp "$root/scripts/check_no_base_merges.py" "$hooks/check_no_base_merges.py"
chmod +x "$hooks/pre-merge-commit"
if [ -f "$hooks/pre-commit" ] && ! grep -q "vks-base-merge-check" "$hooks/pre-commit"; then
    mv "$hooks/pre-commit" "$hooks/pre-commit.local"
fi
cat > "$hooks/pre-commit" <<'EOF'
#!/bin/sh
# vks-base-merge-check: concluding a conflicted merge also runs the check.
if git rev-parse -q --verify MERGE_HEAD >/dev/null 2>&1; then
    "$(git rev-parse --git-common-dir)/hooks/pre-merge-commit" || exit 1
fi
hooks=$(git rev-parse --git-common-dir)/hooks
if [ -x "$hooks/pre-commit.local" ]; then
    exec "$hooks/pre-commit.local" "$@"
fi
exit 0
EOF
chmod +x "$hooks/pre-commit"
echo "Installed pre-merge-commit and pre-commit into $hooks"
