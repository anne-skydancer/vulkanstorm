"""Reviewed ranges move only with unchanged lines; any edit inside one drops it."""
import unittest

from carry_reviewed_ranges import moved_range, unchanged_lines

OLD = ["a", "b", "c", "d", "e"]


def review(start, end):
    return {"path": "x.cpp", "start": start, "end": end, "sha256": "old", "record": "I24",
            "disposition": "mapped", "reason": "kept verbatim"}


class CarryReviewedRanges(unittest.TestCase):
    def test_insertion_above_moves_review_with_its_reason(self):
        mapping = unchanged_lines(OLD, ["new", "a", "b", "c", "d", "e"])
        carried = moved_range(review(3, 4), mapping, "new")
        self.assertEqual((carried["start"], carried["end"], carried["sha256"]), (4, 5, "new"))
        self.assertEqual(carried["reason"], "kept verbatim")

    def test_edited_line_inside_review_drops_it(self):
        mapping = unchanged_lines(OLD, ["a", "b", "C", "d", "e"])
        self.assertIsNone(moved_range(review(2, 4), mapping, "new"))
        self.assertIsNotNone(moved_range(review(4, 5), mapping, "new"))

    def test_insertion_splitting_a_review_drops_it(self):
        mapping = unchanged_lines(OLD, ["a", "b", "inserted", "c", "d", "e"])
        self.assertIsNone(moved_range(review(2, 3), mapping, "new"))

    def test_deleted_line_drops_its_review(self):
        mapping = unchanged_lines(OLD, ["a", "c", "d", "e"])
        self.assertIsNone(moved_range(review(2, 2), mapping, "new"))
        self.assertEqual(moved_range(review(3, 3), mapping, "new")["start"], 2)


if __name__ == "__main__":
    unittest.main()
