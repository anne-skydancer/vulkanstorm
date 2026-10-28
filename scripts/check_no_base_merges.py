#!/usr/bin/env python3
"""Reject merges of a base line into a feature branch.

Changes flow one way: a feature or PR branch is merged into its base line
(vkstorm-*) through its PR. Merging a base line into a feature branch
("Merge vkstorm-rust into codex/...", GitHub's "Update branch") is not used in
this repository (AGENTS.md).

What counts as merging a base line: a merge whose incoming parent is a commit
of a base line's own history, its first-parent chain (each tip it has had).
Merging the base line, or pressing "Update branch", brings in such a tip. A
feature that later lands on the base through its PR is only a second parent
of that PR's merge commit, never on the chain, so an earlier merge of that
feature into another feature (stacked branches) stays allowed.

Upstream-tracking lines (MIRROR_LINES; vkstorm-aleph tracks Firestorm master)
carry upstream's commits plus their own (vkstorm-release's build defaults and
CI). Upstream's commits on their chain are upstream history: bringing them in
is an upstream integration, which canary takes through integration merges
(AGENTS.md), so they are not checked. The line's own commits, those not
reachable from the upstream ref (UPSTREAM_REF, default upstream/master;
VKS_UPSTREAM_REF overrides it), are protected like any base line. Without
the upstream ref the whole chain is protected and a note says to fetch it.

Upstream's own merges are not checked either. Every base line branched from
upstream, so old upstream commits sit on its first-parent chain; an upstream
merge whose second parent is one of them (upstream merging its master into a
feature branch) arrives with an upstream integration but is upstream's
history, not a base merge. Merges the upstream ref can reach are skipped.
Without the upstream ref they are checked (fail closed).

usage:
  check_no_base_merges.py <base-ref> [<head-ref>]
      CI: every merge commit in <base-ref>..<head-ref> (head defaults to HEAD)
      whose second or later parent is on the first-parent chain of any base
      line (origin/vkstorm-*, plus <base-ref>) is reported; exit status 1.
  check_no_base_merges.py --pending
      pre-merge-commit hook: when the current branch is not a base line and
      the merge being made brings in a commit on the first-parent chain of any
      origin/vkstorm-* line, report it and exit 1. VKS_ALLOW_BASE_MERGE=1
      overrides it for a deliberate exception.
"""
import os
import subprocess
import sys

BASE_PREFIX = "vkstorm-"
# Lines that track upstream. Upstream's commits on their chain are exempt
# (merging upstream brings them in by design); their own commits are not.
MIRROR_LINES = ("vkstorm-aleph",)
UPSTREAM_REF = os.environ.get("VKS_UPSTREAM_REF", "refs/remotes/upstream/master")


def git(*args, check=True):
    # stdout/stderr pipes and universal_newlines: Python 3.6 compatible.
    proc = subprocess.run(["git"] + list(args), stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, universal_newlines=True)
    if check and proc.returncode != 0:
        sys.exit("git %s failed: %s" % (" ".join(args), proc.stderr.strip()))
    return proc


def subject(commit):
    return git("log", "-1", "--format=%h %s", commit).stdout.strip()


def resolve(name):
    proc = git("rev-parse", "--verify", "--quiet", name + "^{commit}", check=False)
    return proc.stdout.strip() if proc.returncode == 0 else None


def base_lines(extra=None):
    # Remote-tracking and local base branches: a local base line can be ahead
    # of origin (for example after pulling from an upstream remote).
    refs = []
    for prefix in ("refs/remotes/origin/", "refs/heads/"):
        refs += git("for-each-ref", "--format=%(refname)",
                    "%s%s*" % (prefix, BASE_PREFIX)).stdout.split()
    if extra and resolve(extra):
        refs.append(extra)
    return refs


def is_mirror(ref):
    return ref.rsplit("/", 1)[-1] in MIRROR_LINES


def first_parent_chains(refs):
    """commit -> the base line whose first-parent chain contains it.

    For an upstream-tracking line only its own commits count: those on its
    first-parent chain that the upstream ref cannot reach.
    """
    owner = {}
    upstream = resolve(UPSTREAM_REF)
    for ref in refs:
        args = ["rev-list", "--first-parent", ref]
        if is_mirror(ref):
            if upstream:
                args += ["--not", upstream]
            else:
                print("note: %s not found; all of %s is protected. Fetch upstream "
                      "(git fetch upstream master) to exempt upstream's commits." % (
                          UPSTREAM_REF, short_ref(ref)))
        for commit in git(*args).stdout.split():
            owner.setdefault(commit, ref)
    return owner


def short_ref(ref):
    return ref.replace("refs/remotes/", "").replace("refs/heads/", "")


def check_range(base, head):
    owner = first_parent_chains(base_lines(base))
    found = []
    # Upstream's own merges (reachable from the upstream ref) are upstream
    # history brought in by an upstream integration, not base merges.
    merges = ["rev-list", "--merges", "%s..%s" % (base, head)]
    upstream = resolve(UPSTREAM_REF)
    if upstream:
        merges += ["--not", upstream]
    else:
        print("note: %s not found; upstream's own merges are checked too. Fetch "
              "upstream (git fetch upstream master) to exempt them." % UPSTREAM_REF)
    for merge in git(*merges).stdout.split():
        parents = git("log", "-1", "--format=%P", merge).stdout.split()
        for parent in parents[1:]:
            if parent in owner:
                found.append((merge, owner[parent]))
                break
    if found:
        print("Merges of a base line into this branch (%s..%s):" % (base, head))
        for merge, line in found:
            print("  %s  (brings in %s)" % (subject(merge), short_ref(line)))
        print("Changes flow from the branch into its base through the PR, never the "
              "other way (AGENTS.md). Bring base changes in by cherry-pick, or land "
              "the branch first and follow up.")
        return 1
    print("No merges of a base line in %s..%s." % (base, head))
    return 0


def fetch_head_merges():
    path = git("rev-parse", "--git-path", "FETCH_HEAD").stdout.strip()
    if not os.path.exists(path):
        return []
    commits = []
    with open(path) as fh:
        for line in fh:
            fields = line.rstrip("\n").split("\t")
            # <sha> TAB <"" or "not-for-merge"> TAB <description>
            if len(fields) >= 2 and fields[1] != "not-for-merge":
                commits.append(fields[0])
    return commits


def incoming_commits():
    """The commits the merge being made brings in.

    A merge that stopped (conflicts, --no-commit) and is concluded by
    "git commit" has MERGE_HEAD. During an automatic merge commit git has not
    written MERGE_HEAD when pre-merge-commit runs, but it sets
    GIT_REFLOG_ACTION to "merge <commits>" (or "pull ..." for git pull, whose
    commits are the FETCH_HEAD entries marked for merge).
    """
    merge_head = git("rev-parse", "--git-path", "MERGE_HEAD").stdout.strip()
    if os.path.exists(merge_head):
        with open(merge_head) as fh:
            return fh.read().split()
    action = os.environ.get("GIT_REFLOG_ACTION", "").split()
    if action[:1] == ["merge"]:
        return [c for c in (resolve(t) for t in action[1:] if not t.startswith("-")) if c]
    if action[:1] == ["pull"]:
        return fetch_head_merges()
    return []


def check_pending():
    if os.environ.get("VKS_ALLOW_BASE_MERGE") == "1":
        return 0
    branch = git("symbolic-ref", "--quiet", "--short", "HEAD", check=False).stdout.strip()
    if not branch or branch.startswith(BASE_PREFIX):
        return 0
    incoming = incoming_commits()
    if not incoming:
        return 0
    owner = first_parent_chains(base_lines())
    for commit in incoming:
        if commit in owner:
            print("Refusing to merge %s into %s: it is on %s's own history." % (
                subject(commit), branch, short_ref(owner[commit])))
            print("Feature branches are merged into their base line through the PR, "
                  "never the other way (AGENTS.md). Use a cherry-pick instead, or set "
                  "VKS_ALLOW_BASE_MERGE=1 for a deliberate exception.")
            return 1
    return 0


def main(argv):
    if argv[:1] == ["--pending"]:
        return check_pending()
    if not argv or len(argv) > 2:
        print(__doc__)
        return 2
    return check_range(argv[0], argv[1] if len(argv) > 1 else "HEAD")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
