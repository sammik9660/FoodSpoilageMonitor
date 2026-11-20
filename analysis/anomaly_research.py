"""Label-free, causal six-scan diagonal Gaussian versus the fixed change index.

This is research only. Endpoint observations are not per-scan labels.
"""
import argparse
import json
import math
import statistics
from pathlib import Path

from edge_change import Model, load


def acceptable(gas):
    if any(not math.isfinite(x) or x <= 0 for x in gas):
        return False
    logs = [math.log10(x) for x in gas]
    return sum(abs(x - 5.68) <= 0.01 for x in gas) < 8 and max(logs) - min(logs) >= 0.05


def compare(groups):
    reports = []
    for (device, exposure), rows in sorted(groups.items()):
        if not exposure:
            continue
        fixed = Model()
        training = []
        paired = []
        for row in rows:
            gas = row['gas']
            fixed_score, _ = fixed.observe(gas)
            if not acceptable(gas):
                continue
            logs = [math.log10(x) for x in gas]
            if len(training) < 6:
                training.append(logs)
                continue
            center = [sum(sample[i] for sample in training) / 6 for i in range(10)]
            # Diagonal shrinkage: 0.05 log10 units floor prevents tiny n variance blow-up.
            scale = [max(0.05, math.sqrt(sum((sample[i] - center[i]) ** 2 for sample in training) / 5))
                     for i in range(10)]
            anomaly = sum(abs(logs[i] - center[i]) / scale[i] for i in range(10)) / 10
            assert fixed_score is not None
            paired.append((fixed_score[0], anomaly))
        if len(paired) < 8:
            continue
        q = max(1, len(paired) // 4)
        reports.append({'device_id': device, 'exposure_group': exposure,
                        'baseline_scans': 6, 'paired_scans': len(paired),
                        'fixed_early_median': statistics.median(x[0] for x in paired[:q]),
                        'fixed_late_median': statistics.median(x[0] for x in paired[-q:]),
                        'trained_early_median': statistics.median(x[1] for x in paired[:q]),
                        'trained_late_median': statistics.median(x[1] for x in paired[-q:])})
    return reports


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--scans', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = {'model': 'per-exposure six-scan diagonal Gaussian with 0.05 log floor',
              'parameters_bytes_float32': 80, 'supervised_accuracy': None,
              'warning': 'Only descriptive early/late comparisons; unknown endpoints cannot be negative labels',
              'groups': compare(load(args.scans))}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('PASS label-free trained anomaly comparison groups=', len(report['groups']))


if __name__ == '__main__':
    main()
