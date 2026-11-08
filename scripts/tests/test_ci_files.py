# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The CI files parse, and the composite actions are used as declared.

Every YAML file under .github and the CI JSON files parse; every run step of
a composite action (.github/actions/*/action.yml) names its shell, uses only
the keys composite steps support and only declared inputs; every workflow
step that uses a local action passes only inputs it declares. actionlint
checks the workflows themselves (ci-lint.yaml).

The build workflows ignore the same CI and documentation files (paths-ignore,
the same list per event), and ci-lint.yaml paths cover every ignored CI file; documentation
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

# The build workflows are every .github/workflows/build-*.yaml or .yml (one per platform); the events
# whose path filters must agree are the code-triggered ones they declare.
FILTERED_EVENTS = ("pull_request", "push")
SHOWN = 10  # differences listed in full before they are summarised
# Build-ignored without being linted: documentation has nothing for ci-lint to check.
DOC_ONLY_IGNORES = {"doc/**", "**/*.md"}


def raw_triggers(workflow):
    """The `on` value as written: a mapping, a name or a list (YAML reads a bare `on` as True)."""
    return workflow.get(True, workflow.get("on"))


def triggers_of(workflow):
    """The `on` mapping of a workflow; a name or a list becomes {event: None} (no filters)."""
    triggers = raw_triggers(workflow)
    if isinstance(triggers, dict):
        return triggers
    if isinstance(triggers, str):
        return {triggers: None}
    if isinstance(triggers, list):
        return {str(event): None for event in triggers}
    return {}


def brief(patterns):
    """Sorted patterns as comma-separated text, summarised beyond SHOWN entries."""
    items = sorted(patterns)
    if not items:
        return "none"
    if len(items) <= SHOWN:
        return ", ".join(items)
    return f"{', '.join(items[:SHOWN])} and {len(items) - SHOWN} more"


def pattern_list(value):
    """A path filter as a list of strings (YAML allows a single string)."""
    if value is None:
        return []
    return [value] if isinstance(value, str) else [str(item) for item in value]


def check_path_lists(documents):
    """The build workflows ignore the same files per event; ci-lint lints every CI file they ignore."""
    errors = []
    # An empty or scalar workflow is kept as {} so a build workflow like that fails the
    # trigger check instead of being skipped.
    workflows = {path.name: doc if isinstance(doc, dict) else {} for path, doc in documents.items()
                 if path.parent.name == "workflows"}
    by_event = {event: {} for event in FILTERED_EVENTS}  # event -> {workflow: set of patterns}
    build_names = sorted(name for name in workflows
                         if name.startswith("build-") and name.endswith((".yaml", ".yml")))
    if not build_names:
        errors.append("no .github/workflows/build-*.yaml or build-*.yml build workflows found")
    for name in build_names:
        shorthand = not isinstance(raw_triggers(workflows[name]), dict)
        triggers = triggers_of(workflows[name])
        if "pull_request" not in triggers:
            errors.append(f"{name}: no pull_request trigger")
        for event in FILTERED_EVENTS:
            if event not in triggers:
                continue  # e.g. Build Linux LTS has no push trigger
            config = triggers[event]
            if config is None and shorthand:
                errors.append(f"{name}: {event} is a shorthand trigger; it must be a mapping to carry paths-ignore")
                continue
            if not isinstance(config, dict) or "paths-ignore" not in config:
                errors.append(f"{name}: {event} has no paths-ignore")
                continue
            by_event[event][name] = set(pattern_list(config["paths-ignore"]))
    ignored = set()
    for event, lists in by_event.items():
        if not lists:
            continue
        reference_name = next(iter(lists))
        reference = lists[reference_name]
        ignored |= reference
        for name, patterns in lists.items():
            ignored |= patterns
            if patterns != reference:
                errors.append(f"{name} {event}: paths-ignore differs from {reference_name} "
                              f"(only here: {brief(patterns - reference)}; missing: {brief(reference - patterns)})")
    lint_trigger = triggers_of(workflows.get("ci-lint.yaml", {})).get("pull_request")
    if not isinstance(lint_trigger, dict) or "paths" not in lint_trigger:
        errors.append("ci-lint.yaml: pull_request has no paths")
    else:
        lint = set(pattern_list(lint_trigger["paths"]))
        for pattern in sorted(ignored - DOC_ONLY_IGNORES - lint):
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
