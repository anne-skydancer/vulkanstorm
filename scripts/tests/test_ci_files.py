# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The CI files parse, and the composite actions are used as declared.

Every YAML file under .github and the CI JSON files parse; every run step of
a composite action (.github/actions/*/action.yml) names its shell, uses only
the keys composite steps support and only declared inputs; every workflow
step that uses a local action passes only inputs it declares. actionlint
checks the workflows themselves (ci-lint.yaml).
"""
import json
import pathlib
import re
import subprocess
import sys

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
STEP_KEYS = {"name", "id", "if", "run", "shell", "env", "uses", "with", "working-directory", "continue-on-error"}


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
    if errors:
        sys.exit("\n".join(errors))
    print(f"{len(documents)} YAML file(s) parse; {len(actions)} composite action(s) consistent with their callers")


if __name__ == "__main__":
    main()
