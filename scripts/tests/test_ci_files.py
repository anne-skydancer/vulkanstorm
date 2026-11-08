# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The CI files parse, and the composite actions are used as declared.

Every YAML file under .github and the CI JSON files parse; every run step of
a composite action (.github/actions/*/action.yml) names its shell, uses only
the keys composite steps support and only declared inputs; every workflow
step that uses a local action passes only inputs it declares. actionlint
checks the workflows themselves (ci-lint.yaml).

The build workflows ignore the same CI and documentation files (paths-ignore, on
every event), and ci-lint.yaml paths cover every ignored CI file; documentation
(DOC_ONLY_IGNORES) is ignored by the builds without being linted.
"""
import json
import pathlib
import re
import subprocess
import sys

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
STEP_KEYS = {"name", "id", "if", "run", "shell", "env", "uses", "with", "working-directory", "continue-on-error"}

BUILD_WORKFLOWS = ("build-windows.yaml", "build-linux.yaml", "build-linux-lts.yaml")
# Build-ignored without being linted: documentation has nothing for ci-lint to check.
DOC_ONLY_IGNORES = {"doc/**", "**/*.md"}


def trigger_lists(workflow, key):
    """{event: list} of the given path filter ('paths' or 'paths-ignore') in a workflow."""
    triggers = workflow.get(True, workflow.get("on")) or {}  # YAML reads a bare `on` as True
    return {event: config[key] for event, config in triggers.items()
            if isinstance(config, dict) and key in config}


def check_path_lists(documents):
    """The build workflows ignore the same files; ci-lint lints every CI file they ignore."""
    errors = []
    workflows = {path.name: doc for path, doc in documents.items()
                 if path.parent.name == "workflows" and isinstance(doc, dict)}
    ignored = {}
    for name in BUILD_WORKFLOWS:
        if name not in workflows:
            errors.append(f"{name}: build workflow missing")
            continue
        lists = trigger_lists(workflows[name], "paths-ignore")
        if "pull_request" not in lists:
            errors.append(f"{name}: pull_request has no paths-ignore")
        for event, patterns in lists.items():
            ignored[f"{name} {event}"] = set(patterns)
    reference = next(iter(ignored.values()), set())
    for where, patterns in ignored.items():
        if patterns != reference:
            errors.append(f"{where}: paths-ignore differs from the other build workflows "
                          f"(only here: {sorted(patterns - reference)}; missing: {sorted(reference - patterns)})")
    lint = trigger_lists(workflows.get("ci-lint.yaml", {}), "paths").get("pull_request")
    if lint is None:
        errors.append("ci-lint.yaml: pull_request has no paths")
    else:
        for pattern in sorted(reference - DOC_ONLY_IGNORES - set(lint)):
            errors.append(f"ci-lint.yaml: paths lacks '{pattern}', which the build workflows ignore")
    return errors


def main():
    errors = []
    # No ignore pattern may hide a CI file (a build-* pattern once hid build-linux-lts.yaml).
    ci_files = [p.relative_to(ROOT).as_posix() for p in (ROOT / ".github").rglob("*") if p.is_file()]
    ci_files += [p.relative_to(ROOT).as_posix() for p in (ROOT / "scripts").glob("ci_*")]
    result = subprocess.run(["git", "-C", str(ROOT), "check-ignore", "--no-index", "--stdin", "-z"],
                            input="\0".join(ci_files).encode(), capture_output=True)
    hidden = [name for name in result.stdout.decode().split("\0") if name]
    errors += [f"{name}: matched by .gitignore" for name in hidden]
    documents = {}
    for path in sorted((ROOT / ".github").rglob("*.y*ml")):
        try:
            documents[path] = yaml.safe_load(path.read_text(encoding="utf-8"))
        except yaml.YAMLError as error:
            errors.append(f"{path.relative_to(ROOT)}: {error}")
    for name in (".github/build-platforms.json", "scripts/linux_lts_packages.json"):
        path = ROOT / name
        if path.exists():
            try:
                json.loads(path.read_text(encoding="utf-8"))
            except ValueError as error:
                errors.append(f"{name}: {error}")
    actions = {}
    for path in sorted((ROOT / ".github/actions").glob("*/action.yml")):
        action = documents.get(path) or {}
        rel = path.relative_to(ROOT)
        if action.get("runs", {}).get("using") != "composite":
            errors.append(f"{rel}: not a composite action")
            continue
        declared = set(action.get("inputs") or {})
        actions[f"./.github/actions/{path.parent.name}"] = declared
        for index, step in enumerate(action["runs"]["steps"]):
            if ("run" in step) == ("uses" in step):
                errors.append(f"{rel} step {index}: needs exactly one of run and uses")
            if "run" in step and "shell" not in step:
                errors.append(f"{rel} step {index}: a composite run step must name its shell")
            if set(step) - STEP_KEYS:
                errors.append(f"{rel} step {index}: unsupported keys {sorted(set(step) - STEP_KEYS)}")
        text = path.read_text(encoding="utf-8")
        if "matrix." in text:
            errors.append(f"{rel}: a composite action has no matrix context")
        for used in sorted(set(re.findall(r"inputs\.([A-Za-z_]+)", text)) - declared):
            errors.append(f"{rel}: uses undeclared input {used}")
    for path, workflow in documents.items():
        if path.parent.name != "workflows" or not isinstance(workflow, dict):
            continue
        for job_name, job in (workflow.get("jobs") or {}).items():
            for step in job.get("steps") or []:
                uses = step.get("uses", "")
                if uses.startswith("./"):
                    if uses not in actions:
                        errors.append(f"{path.name} {job_name}: no local action {uses}")
                    elif set(step.get("with") or {}) - actions[uses]:
                        errors.append(f"{path.name} {job_name}: passes undeclared inputs "
                                      f"{sorted(set(step['with']) - actions[uses])} to {uses}")
    errors += check_path_lists(documents)
    if errors:
        sys.exit("\n".join(errors))
    print(f"{len(documents)} YAML file(s) parse; {len(actions)} composite action(s) consistent with their callers")


if __name__ == "__main__":
    main()
