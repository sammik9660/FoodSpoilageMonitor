import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "analysis"))
from fingerprints import reconstruct


class FingerprintsTest(unittest.TestCase):
    def setUp(self):
        indices = [250, 252, 6, 36, 41, 46, 51, 56, 61, 66]
        self.rows = [dict(sample_schema="3", sample_uid=f"u-{i}", session_id="EXP-A",
                          nrf_boot="305441741", scan_seq="2", profile_id="354",
                          heater_step=str(i), gas_index=str(i), meas_index=str(indices[i]),
                          nrf_seq=str(i+11), sensor_status="176", gas_valid="TRUE",
                          heater_stable="TRUE", measurement_valid="TRUE",
                          temperature="25.10", humidity="50.20", pressure="1005.30",
                          gas_resistance="84.20") for i in range(10)]

    def test_complete_scan_with_dummy_index_jumps_and_wrap(self):
        good, bad = reconstruct(self.rows)
        self.assertEqual(len(good), 1)
        self.assertEqual(bad, [])

    def test_missing_duplicate_invalid_and_cross_session(self):
        for rows in (self.rows[:-1], self.rows + [self.rows[-1] | {"sample_uid": "other"}],
                     [r | {"gas_resistance": "0"} if i == 3 else r for i, r in enumerate(self.rows)],
                     [r | {"session_id": "EXP-B"} if i == 9 else r for i, r in enumerate(self.rows)]):
            good, bad = reconstruct(rows)
            self.assertEqual(good, [])
            self.assertTrue(bad)

    def test_legacy_ignored_and_identical_export_duplicate_deduped(self):
        good, bad = reconstruct(self.rows + [copy.deepcopy(self.rows[0]),
                                             {"sample_schema": "2", "sample_uid": "old"}])
        self.assertEqual(len(good), 1)
        self.assertEqual(bad, [])

    def test_conflicting_uid_blocks_complete_scan(self):
        good, bad = reconstruct(self.rows + [self.rows[0] | {"gas_resistance": "99"}])
        self.assertEqual(good, [])
        self.assertTrue(bad)

    def test_shared_wire_vector_alone_is_not_a_complete_fingerprint(self):
        line = (Path(__file__).resolve().parent / "v3_vector.txt").read_text().strip()
        fields = dict(part.split("=") for part in line.split(","))
        row = self.rows[3] | {"nrf_boot": str(int(fields["BID"], 16)),
                              "scan_seq": fields["SCAN"], "heater_step": fields["STEP"],
                              "gas_index": fields["GI"], "meas_index": fields["MI"],
                              "sensor_status": fields["ST"],
                              "gas_resistance": fields["G"]}
        good, bad = reconstruct([row])
        self.assertEqual(good, [])
        self.assertTrue(bad)


if __name__ == "__main__":
    unittest.main()
