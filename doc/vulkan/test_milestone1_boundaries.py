"""Failure-oriented regression tests for milestone source acceptance evidence."""
import copy
import json
import unittest
from unittest.mock import patch

import check_milestone1_boundaries as audit


class MilestoneEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.baseline = json.loads(audit.MANIFEST.read_text(encoding='utf-8'))
        cls.current_census = audit.census()

    def setUp(self):
        census_patch = patch.object(audit, 'census', return_value=copy.deepcopy(self.current_census))
        census_patch.start()
        self.addCleanup(census_patch.stop)

    def rejected(self, mutate, expected):
        data = copy.deepcopy(self.baseline)
        mutate(data)
        self.assertTrue(any(expected in error for error in audit.validate(data)))

    def test_current_evidence(self):
        self.assertEqual([], audit.validate(self.baseline))

    def test_stale_source(self):
        self.rejected(lambda d: d['sources']['indra/newview/llstartup.cpp'].update(sha256='0' * 64), 'Stale reviewed source')

    def test_missing_callback(self):
        self.rejected(lambda d: d['registries']['messages'].pop(), 'incomplete messages registry')

    def test_new_registration_requires_review(self):
        current = audit.census()
        current['settings'].append({'line': 9999, 'call': 'setting_setup_signal_listener(new_service)'})
        with patch.object(audit, 'census', return_value=current):
            self.assertTrue(any('incomplete settings registry' in e for e in audit.validate(self.baseline)))

    def test_missing_state(self):
        self.rejected(lambda d: d['states'].pop('STATE_INVENTORY_SEND2'), 'startup-state accounting')

    def test_missing_init_destroy_callback(self):
        self.rejected(lambda d: d['lifecycles'].pop(), 'init/destroy registration census')

    def test_missing_family(self):
        self.rejected(lambda d: d['families'].pop('I24'), 'insertion-family accounting')

    def test_deferred_callback_without_gate(self):
        self.rejected(lambda d: d['registries']['floaters'][0].update(gate=''), 'Unclassified floaters')

    def test_route_without_caller_outcome(self):
        self.rejected(lambda d: d['routes']['R12'].update(cpu_outcome=''), 'caller/gate contract')

    def test_registration_without_caller_route(self):
        self.rejected(lambda d: d['registries']['names'][0].update(caller_route=''), 'Unaccounted names caller')

    def test_cannot_claim_runtime(self):
        self.rejected(lambda d: d.update(gates_implemented=True), 'must not claim runtime')


if __name__ == '__main__':
    unittest.main()
