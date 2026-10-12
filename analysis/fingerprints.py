"""Reconstruct complete HP-354 scans from v3 SensorData CSV rows.

No spoilage label or model is inferred here. Rejected groups remain visible.
"""

import csv
import math
from collections import defaultdict
from itertools import chain
from pathlib import Path


def read_csv(path):
    with Path(path).open(encoding="utf-8-sig", newline="") as stream:
        yield from csv.DictReader(stream)


def reconstruct(rows):
    groups = defaultdict(list)
    rejected = []
    seen_uid = {}
    conflicted = set()
    for row in rows:
        if str(row.get("sample_schema", "")).strip() != "3":
            continue  # fixed-heater v2 is never a fabricated HP-354 step
        uid = row.get("sample_uid", "")
        if uid in seen_uid:
            if row != seen_uid[uid]:
                rejected.append((uid, "conflicting duplicate UID"))
                prior = seen_uid[uid]
                conflicted.add((prior.get("session_id", ""), prior.get("nrf_boot", ""),
                                prior.get("scan_seq", ""), prior.get("profile_id", "")))
            continue  # overlapping CSV export pages may repeat the same row
        seen_uid[uid] = row
        key = (row.get("session_id", ""), row.get("nrf_boot", ""),
               row.get("scan_seq", ""), row.get("profile_id", ""))
        groups[key].append(row)

    complete = []
    for key, items in groups.items():
        reason = None
        if key in conflicted:
            reason = "conflicting duplicate UID"
        elif not key[0] or not key[1] or not key[2] or key[3] != "354":
            reason = "missing session/scan identity or wrong profile"
        elif len(items) != 10:
            reason = "not exactly ten step records"
        try:
            ordered = sorted(items, key=lambda row: int(row["heater_step"]))
            if not reason and [int(r["heater_step"]) for r in ordered] != list(range(10)):
                reason = "missing or repeated step"
            if not reason:
                for step, row in enumerate(ordered):
                    status = int(row["sensor_status"])
                    if (int(row["gas_index"]) != step or status & 0xB0 != 0xB0 or
                        any(str(row[name]).lower() not in ("true", "1") for name in
                            ("gas_valid", "heater_stable", "measurement_valid")) or
                        not all(math.isfinite(float(row[name])) for name in
                                ("temperature", "humidity", "pressure", "gas_resistance")) or
                        float(row["gas_resistance"]) <= 0):
                        reason = "invalid gas/status/measurement"
                        break
                    if step:
                        # Bosch meas_index counts dummy TPHG conversions too:
                        # require forward modulo-256 order, never a delta of 1.
                        delta = (int(row["meas_index"]) - int(ordered[step-1]["meas_index"])) & 255
                        if not 1 <= delta <= 127 or int(row["nrf_seq"]) != int(ordered[step-1]["nrf_seq"]) + 1:
                            reason = "measurement/frame sequence discontinuity"
                            break
        except (KeyError, TypeError, ValueError, OverflowError):
            reason = "malformed step metadata"
        if reason:
            rejected.append((key, reason))
        else:
            complete.append((key, ordered))
    return complete, rejected


if __name__ == "__main__":
    import sys
    if len(sys.argv) < 2:
        raise SystemExit("Usage: python analysis/fingerprints.py PAGE.csv [MORE_PAGES.csv ...]")
    complete, rejected = reconstruct(chain.from_iterable(read_csv(p) for p in sys.argv[1:]))
    print(f"complete_scans={len(complete)} incomplete_or_conflicted={len(rejected)}")
    for key, reason in rejected[:20]:
        print(f"{key}: {reason}")
