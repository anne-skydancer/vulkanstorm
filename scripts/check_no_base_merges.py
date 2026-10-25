#!/usr/bin/env python3
"""Reject merges of a base line into a feature branch.

Changes flow one way: a feature or PR branch is merged into its base line
(vkstorm-*) through its PR. Merging the base line into the feature branch
("Merge vkstorm-rust into codex/...", GitHub's "Update branch") is not used
in this repository (AGENTS.md). This finds such merges.

usage:
  check_no_base_merges.py <base-ref> [<head-ref>]
      Every merge commit in <base-ref>..<head-ref> (head defaults to HEAD) whose
      second or later parent is already contained in <base-ref> is reported,
      and the exit status is 1. Used by CI on pull requests.
  check_no_base_merges.py --pending
      For the pre-merge-commit hook: when the current branch is not a base line
      and a merge in progress (MERGE_HEAD) brings in a commit already contained
      in any remote base line (origin/vkstorm-*), report it and exit 1.
      VKS_ALLOW_BASE_MERGE=1 overrides it for a deliberate exception.
"""
import os
import subprocess
import sys

BASE_PREFIX = "vkstorm-"


def git(*args, check=True):
    proc = subprocess.run(["git"] + list(args), capture_output=True, text=True)
    if check and proc.returncode != 0:
        sys.exit("git %s failed: %s" % (" ".join(args), proc.stderr.strip()))
    return proc


def contained(commit, ref):
    return git("merge-base", "--is-ancestor", commit, ref, check=False).returncode == 0


def subject(commit):
    return git("log", "-1", "--format=%h %s", commit).stdout.strip()


def check_range(base, head):
    found = []
    for merge in git("rev-list", "--merges", "%s..%s" % (base, head)).stdout.split():
        parents = git("log", "-1", "--format=%P", merge).stdout.split()
        if any(contained(p, base) for p in parents[1:]):
            found.append(merge)
    if found:
        print("Merges of the base line into this branch (%s..%s):" % (base, head))
        for merge in found:
            print("  " + subject(merge))
        print("Changes flow from the branch into its base through the PR, never the "
              "other way (AGENTS.md). Bring base changes in by cherry-pick, or land "
              "the branch first and follow up.")
        return 1
    print("No merges of the base line in %s..%s." % (base, head))
    return 0


def resolve(name):
    proc = git("rev-parse", "--verify", "--quiet", name + "^{commit}", check=False)
    return proc.stdout.strip() if proc.returncode == 0 else None


def incoming_commits():
    """The commits a merge in progress brings in.

    A merge that stopped (conflicts, --no-commit) and is concluded by
    "git commit" has MERGE_HEAD. During an automatic merge commit git has not
    written MERGE_HEAD when pre-merge-commit runs, but it sets
    GIT_REFLOG_ACTION to "merge <commits>" (or "pull ..." for git pull, whose
    commits are the FETCH_HEAD lines marked for merge).
    """
    merge_head = git("rev-parse", "--git-path", "MERGE_HEAD").stdout.strip()
    if os.path.exists(merge_head):
        with open(merge_head) as fh:
            return fh.read().split()
    action = os.environ.get("GIT_REFLOG_ACTION", "").split()
    if action[:1] == ["merge"]:
        return [c for c in (resolve(t) for t in action[1:] if not t.startswith("-")) if c]
    if action[:1] == ["pull"]:
        fetch_head = git("rev-parse", "--git-path", "FETCH_HEAD").stdout.strip()
        if os.path.exists(fetch_head):
            with open(fetch_head) as fh:
                return [line.split()[0] for line in fh if line.strip() and "not-for-merge" not in line]
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
    bases = git("for-each-ref", "--format=%(refname:short)",
                "refs/remotes/origin/%s*" % BASE_PREFIX).stdout.split()
    for commit in incoming:
        for base in bases:
            if contained(commit, base):
                print("Refusing to merge %s into %s: it is already on %s." % (subject(commit), branch, base))
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
