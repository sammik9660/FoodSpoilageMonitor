"""Causal, per-exposure gas change scores; never edits source measurements."""
import argparse
import csv
import hashlib
import json
import math
import statistics
from collections import defaultdict, deque
from datetime import datetime
from pathlib import Path

SOURCE_HASH = "64a5aea8e69a1b634df1cc71262b8a85c9c12f7421b1f6e5cdcdc9508b2848f1"


class Model:
    def __init__(self):
        self.reset()

    def reset(self):
        self.baseline = [0.0] * 10
        self.baseline_mean = 0.0
        self.count = 0

    def observe(self, gas):
        if len(gas) != 10 or any(not math.isfinite(g) or g <= 0 for g in gas):
            return None, "INVALID_GAS"
        logs = [math.log10(g) for g in gas]
        if sum(abs(g - 5.68) <= 0.01 for g in gas) >= 8 or max(logs) - min(logs) < 0.05:
            return None, "LOW_INFORMATION"
        mean = sum(logs) / 10
        centered = [x - mean for x in logs]
        if self.count < 3:
            self.baseline = [a + b / 3 for a, b in zip(self.baseline, centered)]
            self.baseline_mean += mean / 3
            self.count += 1
            return None, "BASELINE"
        shape = sum(abs(a - b) for a, b in zip(centered, self.baseline)) / 10
        level = abs(mean - self.baseline_mean)
        return (0.7 * shape + 0.3 * level, shape, level), "OK"


def load(path):
    groups = defaultdict(list)
    seen = set()
    with path.open(newline="", encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            key = (row["device_id"], row["nrf_boot"], row["scan_seq"], row["profile_id"])
            if key in seen:
                raise ValueError(f"Duplicate scan identity: {key}")
            seen.add(key)
            if int(row["profile_id"]) != 354 or float(row["scan_span_s"]) > 180:
                raise ValueError(f"Invalid profile or scan span: {key}")
            rows = row["source_rows"].split(";")
            uids = row["sample_uids"].split(";")
            if len(rows) != 10 or len(set(rows)) != 10 or len(uids) != 10 or len(set(uids)) != 10:
                raise ValueError(f"Incomplete/duplicate steps: {key}")
            row["time"] = datetime.fromisoformat(row["timestamp_utc"])
            row["gas"] = [float(row[f"gas_step_{i}"]) for i in range(10)]
            groups[(row["device_id"], row["exposure_group"] or row["session_id"])].append(row)
    for rows in groups.values():
        rows.sort(key=lambda r: r["time"])
    return groups


def process(groups):
    output, vectors, summaries = [], [], []
    for group_index, ((device, exposure), rows) in enumerate(sorted(groups.items())):
        if not exposure:
            continue  # no specimen boundary: do not pool unrelated scans
        model, previous, recent = Model(), None, deque(maxlen=3)
        scored = []
        for row in rows:
            gas = row["gas"]
            score, quality = model.observe(gas)
            gap = (row["time"] - previous).total_seconds() / 60 if previous else None
            previous = row["time"]
            if gap is not None and gap < 0:
                raise ValueError("Non-monotonic exposure time")
            item = {k: row[k] for k in ("device_id", "exposure_group", "session_id", "nrf_boot", "scan_seq",
                                          "profile_id", "timestamp_utc", "source_rows", "sample_uids")}
            item.update(quality=quality, gap_minutes=gap, long_gap=gap is not None and gap > 30,
                        temperature_c=row["temp_mean_c"], humidity_pct=row["humidity_mean_pct"],
                        pressure_hpa=row["pressure_mean_hpa"], elapsed_session_hours=row["elapsed_session_hours"],
                        change_index=None, shape_change=None, level_change=None, rolling3=None, slope_per_hour=None)
            if score is not None:
                item.update(change_index=score[0], shape_change=score[1], level_change=score[2])
                recent.append((row["time"], score[0]))
                item["rolling3"] = sum(s for _, s in recent) / len(recent)
                span = (recent[-1][0] - recent[0][0]).total_seconds() / 3600
                item["slope_per_hour"] = (recent[-1][1] - recent[0][1]) / span if span > 0 else None
                scored.append(score)
            output.append(item)
            vectors.append((group_index, gas, score[0] if score else None))
        if scored:
            n = max(1, len(scored) // 4)
            summaries.append({"device_id": device, "exposure_group": exposure, "valid_scores": len(scored),
                              "early_median": statistics.median(s[0] for s in scored[:n]),
                              "late_median": statistics.median(s[0] for s in scored[-n:]),
                              "early_shape_median": statistics.median(s[1] for s in scored[:n]),
                              "late_shape_median": statistics.median(s[1] for s in scored[-n:]),
                              "early_level_median": statistics.median(s[2] for s in scored[:n]),
                              "late_level_median": statistics.median(s[2] for s in scored[-n:])})
    return output, vectors, summaries


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scans", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workbook", type=Path)
    args = parser.parse_args()
    if args.workbook and hashlib.sha256(args.workbook.read_bytes()).hexdigest() != SOURCE_HASH:
        raise ValueError("Source workbook SHA-256 differs from the supplied audit")
    args.output.mkdir(parents=True, exist_ok=True)
    source_hash = hashlib.sha256(args.scans.read_bytes()).hexdigest()
    output, vectors, summaries = process(load(args.scans))
    with (args.output / "change_scores.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(output[0]))
        writer.writeheader()
        writer.writerows(output)
    with (args.output / "cpp_parity.txt").open("w", encoding="ascii") as f:
        for group, gas, score in vectors:
            f.write(" ".join([str(group)] + [f"{g:.9g}" for g in gas] + [f"{score:.12g}" if score is not None else "-1"]) + "\n")
    report = {"input_sha256": source_hash, "scans": len(output), "quality_counts": {}, "baseline_scans": 3,
              "score_formula": "0.7*mean_abs_centered_log10_gas_delta + 0.3*abs(mean_log10_gas_delta)",
              "candidate_components": ["shape_only", "level_only", "combined_0.7_shape_0.3_level"],
              "meaning": "Within-exposure change index, not spoilage probability or safety verdict",
              "supervised_accuracy": None, "exposures": summaries}
    for row in output:
        report["quality_counts"][row["quality"]] = report["quality_counts"].get(row["quality"], 0) + 1
    (args.output / "change_report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"PASS {len(output)} traced scans, {len(summaries)} exposure groups; SHA256 {source_hash}")


if __name__ == "__main__":
    main()
