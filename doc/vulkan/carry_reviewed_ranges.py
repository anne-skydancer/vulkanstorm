"""Carry reviewed source ranges across committed source changes, then re-pin.

The source-evidence check (check_diligent_insertions.py) pins its catalog to
`source_commit` and each reviewed range to its file's sha256. After reviewed
source changes, this tool remaps the existing reviews instead of discarding
them, as the earlier catalog refreshes did ("retain previous responsibility
reasons"):

- a range in an unchanged file is kept as is;
- a range whose every line is unchanged (a difflib "equal" block between the
  pinned source and HEAD) moves to its new line numbers and file hash;
- a range touching an added, removed or edited line is dropped, so that line
  is re-discovered by the checker and must be reviewed again.

Both review files are remapped and pinned to HEAD: diligent-insertion-records.json
and diligent-platform-review.json. It never adds a review. Run the checker
afterwards; any site left unmapped or needs-source-review is the review debt.

usage: python doc/vulkan/carry_reviewed_ranges.py [--dry-run]
"""
import argparse
import difflib
import hashlib
import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "doc/vulkan"
FILES = ("diligent-insertion-records.json", "diligent-platform-review.json")


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, check=True).stdout


def unchanged_lines(old, new):
    """1-based old line -> new line, for lines inside unchanged ("equal") blocks only."""
    mapping = {}
    for tag, i1, i2, j1, _ in difflib.SequenceMatcher(None, old, new, autojunk=False).get_opcodes():
        if tag == "equal":
            for k in range(i2 - i1):
                mapping[i1 + k + 1] = j1 + k + 1
    return mapping


def moved_range(review, mapping, digest):
    """The review at its new lines, or None when any of its lines changed or it was split."""
    start, end = review["start"], review["end"]
    if all(line in mapping for line in range(start, end + 1)) and mapping[end] - mapping[start] == end - start:
        return dict(review, start=mapping[start], end=mapping[end], sha256=digest)
    return None


class Remapper:
    def __init__(self):
        self.maps, self.hashes = {}, {}

    def digest(self, path):
        if path not in self.hashes:
            self.hashes[path] = hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
        return self.hashes[path]

    def line_map(self, pin, path):
        key = (pin, path)
        if key not in self.maps:
            old = git("show", "%s:%s" % (pin, path)).decode("utf-8", "replace").splitlines()
            new = (ROOT / path).read_text(encoding="utf-8", errors="replace").splitlines()
            self.maps[key] = unchanged_lines(old, new)
        return self.maps[key]

    def carry(self, pin, ranges):
        kept, moved, dropped = [], 0, 0
        for review in ranges:
            path = review["path"]
            if not (ROOT / path).exists():
                dropped += 1
                continue
            digest = self.digest(path)
            if digest == review["sha256"]:
                kept.append(review)
                continue
            carried = moved_range(review, self.line_map(pin, path), digest)
            if carried:
                kept.append(carried)
                moved += 1
            else:
                dropped += 1
        return kept, moved, dropped


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    head = git("rev-parse", "HEAD").decode().strip()
    remapper = Remapper()
    for name in FILES:
        path = OUT / name
        data = json.loads(path.read_text(encoding="utf-8"))
        pin = data["source_commit"]
        ranges, moved, dropped = remapper.carry(pin, data["reviewed_ranges"])
        print("%s: %s -> %s; %d reviews, %d moved to new lines, %d dropped for re-review"
              % (name, pin[:10], head[:10], len(ranges), moved, dropped))
        if not args.dry_run:
            data["reviewed_ranges"] = ranges
            data["source_commit"] = head
            # Same layout as the checked-in files: two-space indent, ASCII escapes, LF.
            with path.open("w", encoding="utf-8", newline="\n") as handle:
                handle.write(json.dumps(data, indent=2) + "\n")


if __name__ == "__main__":
    main()
