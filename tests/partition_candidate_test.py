import csv
from pathlib import Path

root = Path(__file__).parents[1]
candidate = root / "firmware/esp32s3/gateway/partitions.16m-spiffs3m-ai5m.csv"
parts = {}
with candidate.open(newline="", encoding="utf-8-sig") as source:
    for row in csv.reader(line for line in source if not line.lstrip().startswith("#")):
        if len(row) < 5 or not row[0].strip():
            continue
        name, kind, subtype, offset, size = [v.strip() for v in row[:5]]
        assert name not in parts and len(name) <= 15
        parts[name] = (kind, subtype, int(offset, 0), int(size, 0))

assert parts["nvs"][2:] == (0x9000, 0x5000)
assert parts["otadata"][2:] == (0xE000, 0x2000)
assert parts["app0"][2:] == (0x10000, 0x300000)
assert parts["spiffs_legacy"][2:] == (0x310000, 0xE0000)
assert parts["coredump"][2:] == (0x3F0000, 0x10000)
assert parts["app1"][2:] == (0x400000, 0x300000)
assert parts["spiffs"] == ("data", "spiffs", 0x700000, 0x300000)
assert parts["ai_model"] == ("data", "0x40", 0xA00000, 0x500000)
ordered = sorted(parts.items(), key=lambda p: p[1][2])
for (left_name, left), (right_name, right) in zip(ordered, ordered[1:]):
    assert left[2] + left[3] <= right[2], (left_name, right_name)
for name, (kind, _, offset, size) in parts.items():
    assert offset % 0x1000 == 0 and size % 0x1000 == 0
    if kind == "app":
        assert offset % 0x10000 == 0
    assert offset + size <= 0x1000000, name
assert ordered[-1][1][2] + ordered[-1][1][3] == 0xF00000
print("PASS candidate: current offsets intact, two 3MiB OTA slots, 3MiB new SPIFFS, 5MiB model, 1MiB spare")
