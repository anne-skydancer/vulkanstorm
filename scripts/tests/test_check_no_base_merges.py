#!/usr/bin/env python3
"""Regression tests for scripts/check_no_base_merges.py.

Each scenario builds a throwaway repository whose base lines are fake
refs/remotes/origin/vkstorm-* refs, then runs the checker in CI mode
(<base> <head>) or hook mode (--pending, with GIT_REFLOG_ACTION or
FETCH_HEAD as git would set them).
"""
import os
import subprocess
import sys
import tempfile

CHECK = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "check_no_base_merges.py"))
FAILURES = []


class Repo:
    def __init__(self, root):
        self.root = root
        self.git("init", "-q", "-b", "trunk")
        self.git("config", "user.name", "t")
        self.git("config", "user.email", "t@t")
        self.git("config", "core.hooksPath", os.devnull if os.name != "nt" else "NUL")
        self.n = 0

    def git(self, *args, env=None):
        proc = subprocess.run(["git"] + list(args), cwd=self.root, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, universal_newlines=True, env=env)
        if proc.returncode != 0:
            raise RuntimeError("git %s: %s" % (" ".join(args), proc.stderr))
        return proc.stdout.strip()

    def commit(self, branch, msg=None):
        has_commits = subprocess.run(["git", "rev-parse", "--verify", "--quiet", "HEAD"], cwd=self.root,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE).returncode == 0
        if has_commits:  # the first commit lands on the unborn initial branch
            self.git("checkout", "-q", branch)
        self.n += 1
        with open(os.path.join(self.root, "f%d" % self.n), "w") as fh:
            fh.write(str(self.n))
        self.git("add", ".")
        self.git("commit", "-q", "-m", msg or "c%d" % self.n)
        return self.git("rev-parse", "HEAD")

    def branch(self, name, start):
        self.git("branch", name, start)

    def merge(self, into, other, msg):
        self.git("checkout", "-q", into)
        self.git("merge", "-q", "--no-ff", "-m", msg, other)
        return self.git("rev-parse", "HEAD")

    def publish(self, line):
        """Mirror a local base branch to refs/remotes/origin/<line>."""
        self.git("update-ref", "refs/remotes/origin/" + line, line)

    def check(self, *args, env_extra=None):
        env = dict(os.environ)
        env.pop("VKS_ALLOW_BASE_MERGE", None)
        env.update(env_extra or {})
        proc = subprocess.run([sys.executable, CHECK] + list(args), cwd=self.root,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              universal_newlines=True, env=env)
        return proc.returncode, proc.stdout


def expect(name, got, want, output):
    if got != want:
        FAILURES.append("%s: exit %d, expected %d\n%s" % (name, got, want, output))
    print("%-62s %s" % (name, "ok" if got == want else "FAIL"))


def setup(tmp):
    r = Repo(tmp)
    r.commit("trunk", "root")
    r.branch("vkstorm-rust", "trunk")
    r.branch("vkstorm-devel", "trunk")
    r.commit("vkstorm-rust")
    r.publish("vkstorm-rust")
    r.publish("vkstorm-devel")
    return r


def scenario(fn):
    with tempfile.TemporaryDirectory() as tmp:
        fn(setup(tmp))


def base_merged_into_feature(r):
    r.branch("codex/a", "vkstorm-rust")
    r.commit("codex/a")
    r.commit("vkstorm-rust")
    r.publish("vkstorm-rust")
    r.merge("codex/a", "vkstorm-rust", "Merge vkstorm-rust into codex/a")
    code, out = r.check("origin/vkstorm-rust", "codex/a")
    expect("CI: base line merged into the feature is rejected", code, 1, out)


def stacked_feature_then_landed(r):
    # A and B both from the base; B merges A; A then lands on the base through its PR.
    r.branch("codex/a", "vkstorm-rust")
    r.branch("codex/b", "vkstorm-rust")
    r.commit("codex/a")
    r.commit("codex/b")
    r.merge("codex/b", "codex/a", "Merge codex/a into codex/b")
    r.merge("vkstorm-rust", "codex/a", "Merge pull request #1 from codex/a")
    r.publish("vkstorm-rust")
    code, out = r.check("origin/vkstorm-rust", "codex/b")
    expect("CI: stacked feature merge stays allowed after it lands", code, 0, out)


def stacked_feature_landed_first(r):
    # A lands first, then B merges A's (now landed) tip: still a feature merge.
    r.branch("codex/a", "vkstorm-rust")
    r.branch("codex/b", "vkstorm-rust")
    r.commit("codex/a")
    r.merge("vkstorm-rust", "codex/a", "Merge pull request #1 from codex/a")
    r.publish("vkstorm-rust")
    r.commit("codex/b")
    r.merge("codex/b", "codex/a", "Merge codex/a into codex/b")
    code, out = r.check("origin/vkstorm-rust", "codex/b")
    expect("CI: merging a landed feature's tip is not a base merge", code, 0, out)


def other_line_merged(r):
    r.commit("vkstorm-devel")
    r.publish("vkstorm-devel")
    r.branch("codex/a", "vkstorm-rust")
    r.commit("codex/a")
    r.merge("codex/a", "vkstorm-devel", "Merge vkstorm-devel into codex/a")
    code, out = r.check("origin/vkstorm-rust", "codex/a")
    expect("CI: another base line's tip merged into the feature is rejected", code, 1, out)


def update_branch_style(r):
    # GitHub's "Update branch": the base tip merged into the PR branch.
    r.branch("codex/a", "vkstorm-rust")
    r.commit("codex/a")
    r.merge("vkstorm-rust", r.commit("trunk"), "Merge pull request #2 from elsewhere")
    r.publish("vkstorm-rust")
    r.merge("codex/a", "origin/vkstorm-rust", "Merge branch 'vkstorm-rust' into codex/a")
    code, out = r.check("origin/vkstorm-rust", "codex/a")
    expect("CI: an 'Update branch' merge is rejected", code, 1, out)


def clean_feature(r):
    r.branch("codex/a", "vkstorm-rust")
    r.commit("codex/a")
    r.commit("codex/a")
    code, out = r.check("origin/vkstorm-rust", "codex/a")
    expect("CI: a feature without merges passes", code, 0, out)


def hook_refuses_and_override(r):
    r.branch("codex/a", "vkstorm-rust")
    r.commit("codex/a")
    r.commit("vkstorm-rust")
    r.publish("vkstorm-rust")
    r.git("checkout", "-q", "codex/a")
    action = {"GIT_REFLOG_ACTION": "merge origin/vkstorm-rust"}
    code, out = r.check("--pending", env_extra=action)
    expect("hook: automatic merge of a base line is refused", code, 1, out)
    code, out = r.check("--pending", env_extra=dict(action, VKS_ALLOW_BASE_MERGE="1"))
    expect("hook: VKS_ALLOW_BASE_MERGE=1 lets it through", code, 0, out)
    r.branch("codex/b", "vkstorm-rust")
    r.commit("codex/b")
    r.git("checkout", "-q", "codex/a")
    code, out = r.check("--pending", env_extra={"GIT_REFLOG_ACTION": "merge codex/b"})
    expect("hook: a feature-into-feature merge is allowed", code, 0, out)


def hook_pull_fetch_head(r):
    r.branch("codex/a", "vkstorm-rust")
    r.commit("codex/a")
    tip = r.commit("vkstorm-rust")
    r.publish("vkstorm-rust")
    r.git("checkout", "-q", "codex/a")
    fetch_head = os.path.join(r.root, ".git", "FETCH_HEAD")
    with open(fetch_head, "w") as fh:
        # A remote URL that contains "not-for-merge": only the status field counts.
        fh.write("%s\t\tbranch 'vkstorm-rust' of https://example.invalid/not-for-merge/repo\n" % tip)
    code, out = r.check("--pending", env_extra={"GIT_REFLOG_ACTION": "pull origin vkstorm-rust"})
    expect("hook: git pull of a base line is refused (FETCH_HEAD field)", code, 1, out)
    with open(fetch_head, "w") as fh:
        fh.write("%s\tnot-for-merge\tbranch 'vkstorm-rust' of https://example.invalid/repo\n" % tip)
    code, out = r.check("--pending", env_extra={"GIT_REFLOG_ACTION": "pull origin vkstorm-rust"})
    expect("hook: FETCH_HEAD entries marked not-for-merge are ignored", code, 0, out)


def main():
    for fn in (base_merged_into_feature, stacked_feature_then_landed, stacked_feature_landed_first,
               other_line_merged, update_branch_style, clean_feature, hook_refuses_and_override,
               hook_pull_fetch_head):
        scenario(fn)
    if FAILURES:
        print("\n".join(FAILURES))
        return 1
    print("all scenarios passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
