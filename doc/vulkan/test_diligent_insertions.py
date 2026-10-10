"""Failure-oriented checks for insertion accounting; no viewer source mutations."""
import contextlib
import io
import json
import pathlib
import sys
import unittest
from unittest import mock

import check_diligent_insertions as audit


class InsertionAccountingTests(unittest.TestCase):
    def proof(self, symbols, statement, owners=(), globals_=()):
        return audit.boundary_proof(
            "fixture.cpp", "cpp", symbols, statement, owners,
            {"LLView"}, "", set(globals_))

    def test_unowned_raw_api_is_unresolved(self):
        self.assertIsNone(self.proof("glBindTexture", "glBindTexture(target, texture);"))

    def test_generic_receiver_does_not_prove_buffer_identity(self):
        self.assertIsNone(self.proof("setBuffer", "unrelated->setBuffer();"))

    def test_declared_shader_receiver_maps_uniform_route(self):
        result = self.proof("uniform4f", "gFixtureShader.uniform4f(slot, r, g, b, a);",
                            globals_=("gFixtureShader",))
        self.assertIsNotNone(result)
        self.assertIn("I06", result[1])

    def test_font_reference_retains_lazy_gpu_dependency(self):
        result = self.proof("LLFontGL", "const LLFontGL* font;")
        self.assertIsNotNone(result)
        self.assertIn("I09", result[1])
        self.assertNotEqual("cpu-only", result[0])

    def test_catchall_does_not_own_unknown_callback(self):
        self.assertIsNone(self.proof("render-callback-or-lifecycle",
                                    "UnknownCallback::render() {", ("I24",)))

    def rejected_review(self, mutate, expected):
        records_path = audit.OUT / "diligent-insertion-records.json"
        original_read = pathlib.Path.read_text
        records = json.loads(original_read(records_path))
        mutate(records)

        def read(path, *args, **kwargs):
            if path == records_path:
                return json.dumps(records)
            return original_read(path, *args, **kwargs)

        original_output = audit.subprocess.check_output

        def output(command, *args, **kwargs):
            if command[:3] == ["git", "diff", "--name-only"]:
                # This fixture tests rejected review metadata after the source-pin
                # precondition. Exercise dirty-source rejection separately below.
                return ""
            return original_output(command, *args, **kwargs)

        with mock.patch.object(pathlib.Path, "read_text", read), \
                mock.patch.object(audit.subprocess, "check_output", output), \
                mock.patch.object(sys, "argv", ["check_diligent_insertions.py", "--accept"]), \
                contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(SystemExit, expected):
                audit.main()

    def test_unknown_review_disposition_is_rejected(self):
        self.rejected_review(
            lambda records: records["reviewed_ranges"][0].update(disposition="invented-success"),
            "Unknown review disposition")

    def test_stale_review_hash_is_rejected(self):
        self.rejected_review(
            lambda records: records["reviewed_ranges"][0].update(sha256="0" * 64),
            "Stale reviewed source")

    def test_dirty_source_is_rejected_before_review(self):
        # An uncommitted fix must not acquire clean source-pin acceptance, even
        # if its changed-file hashes have been reviewed separately.
        original_output = audit.subprocess.check_output

        def output(command, *args, **kwargs):
            if command[:3] == ["git", "diff", "--name-only"]:
                return "indra/newview/llcallingcard.cpp\n"
            return original_output(command, *args, **kwargs)

        with mock.patch.object(audit.subprocess, "check_output", output), \
                mock.patch.object(sys, "argv", ["check_diligent_insertions.py", "--accept"]):
            with self.assertRaisesRegex(SystemExit, "Renderer source differs from catalog baseline"):
                audit.main()


if __name__ == "__main__":
    unittest.main()
