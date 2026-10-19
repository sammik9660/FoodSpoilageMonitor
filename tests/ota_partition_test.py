import csv
from pathlib import Path


path = Path(__file__).parents[1] / "firmware/esp32s3/gateway/partitions.csv"
parts = []
with path.open(newline="", encoding="utf-8-sig") as source:
    for row in csv.reader(line for line in source if not line.lstrip().startswith("#")):
        if len(row) < 5 or not row[0].strip():
            continue
        name, kind, subtype, offset, size = [column.strip() for column in row[:5]]
        parts.append((name, kind, subtype, int(offset, 0), int(size, 0)))

by_name = {part[0]: part for part in parts}
assert len(by_name) == len(parts), "duplicate partition names"
assert set(by_name) == {"nvs", "otadata", "app0", "spiffs", "coredump", "app1"}
assert (by_name["nvs"][3], by_name["nvs"][4]) == (0x9000, 0x5000)
assert (by_name["otadata"][3], by_name["otadata"][4]) == (0xE000, 0x2000)
assert (by_name["app0"][3], by_name["app0"][4]) == (0x10000, 0x300000)
assert (by_name["spiffs"][3], by_name["spiffs"][4]) == (0x310000, 0xE0000)
assert (by_name["coredump"][3], by_name["coredump"][4]) == (0x3F0000, 0x10000)
assert (by_name["app1"][3], by_name["app1"][4]) == (0x400000, 0x300000)

ordered = sorted(parts, key=lambda part: part[3])
for left, right in zip(ordered, ordered[1:]):
    assert left[3] + left[4] <= right[3], f"overlap: {left[0]} / {right[0]}"
for part in parts:
    if part[1] == "app":
        assert part[3] % 0x10000 == 0 and part[4] % 0x10000 == 0
    assert part[3] + part[4] <= 0x1000000, f"outside 16 MiB flash: {part[0]}"

print("PASS OTA partition layout: existing data offsets preserved, OTA slots aligned/non-overlapping")
