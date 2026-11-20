"""Train a demo progression model from user-authorized early/late exposure assumptions.

Early and late windows are weak project labels, not observed onset or food safety.
The original workbook and exported scans are read-only.
"""
import argparse
import bisect
import json
import math
import re
import xml.etree.ElementTree as ET
import zipfile
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import balanced_accuracy_score, confusion_matrix
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler

FOODS = ['banana', 'chicken', 'egg', 'rice', 'sprouts', 'tofu']


def stamp(value):
    if isinstance(value, datetime):
        return value.replace(tzinfo=value.tzinfo or timezone.utc).timestamp() * 1000
    return datetime.fromisoformat(str(value).replace('Z', '+00:00')).timestamp() * 1000


def xlsx_rows(book_path, sheet_name):
    """Stream source rows with the Python standard library; never edit the XLSX."""
    main = '{http://schemas.openxmlformats.org/spreadsheetml/2006/main}'
    rel = '{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id'
    with zipfile.ZipFile(book_path) as archive:
        shared = ET.fromstring(archive.read('xl/sharedStrings.xml'))
        strings = [''.join(node.itertext()) for node in shared.findall(main + 'si')]
        workbook = ET.fromstring(archive.read('xl/workbook.xml'))
        target_id = next(node.attrib[rel] for node in workbook.find(main + 'sheets')
                         if node.attrib['name'] == sheet_name)
        relations = ET.fromstring(archive.read('xl/_rels/workbook.xml.rels'))
        target = next(node.attrib['Target'] for node in relations if node.attrib['Id'] == target_id)
        target = target.lstrip('/') if target.startswith('/') else 'xl/' + target
        with archive.open(target) as source:
            for _, row in ET.iterparse(source, events=('end',)):
                if row.tag != main + 'row':
                    continue
                values = {}
                for cell in row.findall(main + 'c'):
                    column = re.match(r'[A-Z]+', cell.attrib['r']).group()
                    raw = cell.findtext(main + 'v')
                    if raw is None:
                        continue
                    values[column] = strings[int(raw)] if cell.attrib.get('t') == 's' else raw
                yield values
                row.clear()


def load_environment(book_path):
    rows = xlsx_rows(book_path, 'EnvironmentHistory')
    headers = next(rows)
    columns = {name: column for column, name in headers.items()}
    result = defaultdict(list)
    for row in rows:
        try:
            device, session = row.get(columns['device_id']), row.get(columns['session_id'])
            at, temp = stamp(row[columns['timestamp']]), float(row[columns['temperature']])
            if device and session and math.isfinite(at) and math.isfinite(temp):
                result[(device, session)].append((at, temp))
        except (ValueError, TypeError, KeyError):
            continue
    for values in result.values():
        values.sort()
    return result


def rows_for_model(scans, environment):
    groups = defaultdict(list)
    for scan in scans:
        if scan.get('quality') == 'OK' and scan.get('food') in FOODS and scan.get('exposure'):
            groups[(scan['device'], scan['exposure'], scan['food'])].append(scan)
    examples = []
    for (device, exposure, food), group in groups.items():
        group.sort(key=lambda row: row['at'])
        if len(group) < 12:
            continue
        start, end = group[0]['at'], group[-1]['at']
        if end <= start:
            continue
        baseline = np.log10(np.asarray([row['gas'] for row in group[:6]], dtype=float))
        mean = baseline.mean(axis=0)
        scale = np.maximum(baseline.std(axis=0, ddof=1), .05)
        sessions = {row['session'] for row in group}
        env = sorted({item for session in sessions for item in environment.get((device, session), [])})
        times = [item[0] for item in env]
        warm = [0.0] * len(env)
        for i in range(1, len(env)):
            warm[i] = warm[i - 1]
            gap = times[i] - times[i - 1]
            if 0 < gap <= 600000 and env[i - 1][1] > 4:
                warm[i] += gap / 3600000
        for row in group[6:]:
            progress = (row['at'] - start) / (end - start)
            if progress > .2 and progress < .8:
                continue
            pos = bisect.bisect_right(times, row['at']) - 1
            if pos < 0 or row['at'] - times[pos] > 900000:
                continue
            gas = np.asarray(row['gas'], dtype=float)
            anomaly = float(np.mean(np.abs((np.log10(gas) - mean) / scale)))
            if not math.isfinite(anomaly):
                continue
            elapsed = (row['at'] - start) / 3600000
            features = [float(food == name) for name in FOODS] + [math.log1p(elapsed), env[pos][1], warm[pos], anomaly]
            examples.append({'group': exposure, 'food': food, 'label': int(progress >= .8),
                             'features': features, 'at': row['at']})
    return examples


def evaluate(examples, columns):
    groups = sorted({row['group'] for row in examples})
    truth, predicted = [], []
    for group in groups:
        train = [row for row in examples if row['group'] != group]
        test = [row for row in examples if row['group'] == group]
        if not test or len({row['label'] for row in train}) != 2:
            continue
        model = make_pipeline(StandardScaler(), LogisticRegression(max_iter=1000, class_weight='balanced'))
        model.fit(np.asarray([row['features'] for row in train])[:, columns], [row['label'] for row in train])
        truth.extend(row['label'] for row in test)
        predicted.extend(model.predict(np.asarray([row['features'] for row in test])[:, columns]).tolist())
    matrix = confusion_matrix(truth, predicted, labels=[0, 1]).tolist()
    return {'groups': len(groups), 'evaluated': len(truth), 'correct': sum(a == b for a, b in zip(truth, predicted)),
            'balanced_accuracy': balanced_accuracy_score(truth, predicted) if truth else None,
            'confusion_early_late': matrix}


def export(examples, columns):
    model = make_pipeline(StandardScaler(), LogisticRegression(max_iter=1000, class_weight='balanced'))
    x = np.asarray([row['features'] for row in examples])[:, columns]
    model.fit(x, [row['label'] for row in examples])
    scaler, logistic = model.steps[0][1], model.steps[1][1]
    return {'feature_indices': columns, 'mean': scaler.mean_.tolist(), 'scale': scaler.scale_.tolist(),
            'weights': logistic.coef_[0].tolist(), 'intercept': float(logistic.intercept_[0])}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--scans', type=Path, required=True)
    parser.add_argument('--workbook', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    scans = json.loads(args.scans.read_text(encoding='utf8'))
    examples = rows_for_model(scans, load_environment(args.workbook))
    if len({row['group'] for row in examples}) < 3:
        raise RuntimeError('Too few independent exposure groups')
    time_only, a, b = list(range(7)), list(range(9)), list(range(10))
    evaluation = {'assumption': 'first 20% early, last 20% late; middle unlabeled; endpoints assumed by user',
                  'not_measured_spoilage_accuracy': True, 'examples': len(examples),
                  'model_time': evaluate(examples, time_only),
                  'model_a': evaluate(examples, a), 'model_b': evaluate(examples, b)}
    chosen = 'model_b' if evaluation['model_b']['balanced_accuracy'] > evaluation['model_a']['balanced_accuracy'] else 'model_a'
    payload = {'schema': 'assumed-progress-v1', 'food_order': FOODS,
               'feature_order': ['food_' + food for food in FOODS] +
                   ['log1p_elapsed_h', 'latest_heater_off_env_c', 'observed_over_4c_h', 'gas_anomaly_index'],
               'selected': chosen, 'models': {'model_time': export(examples, time_only),
                    'model_a': export(examples, a), 'model_b': export(examples, b)},
               'evaluation': evaluation}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'model.json').write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps({'examples': len(examples), 'selected': chosen,
                      'time': evaluation['model_time'], 'A': evaluation['model_a'], 'B': evaluation['model_b']}))


if __name__ == '__main__':
    main()
