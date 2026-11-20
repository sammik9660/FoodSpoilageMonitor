import sys
from pathlib import Path

root = Path(__file__).parents[1]
sys.path.insert(0, str(root / "analysis"))
from edge_change import Model, load, process  # noqa: E402

first = [30.0, 250.0, 210.0, 180.0, 75.0, 69.0, 64.0, 41.0, 38.0, 36.0]
later = [x * (1.2 if i < 5 else 0.8) for i, x in enumerate(first)]
model = Model()
assert model.observe([5.68] * 10)[1] == "LOW_INFORMATION"
assert model.count == 0
assert model.observe([0.0] * 10)[1] == "INVALID_GAS"
for _ in range(3):
    assert model.observe(first)[1] == "BASELINE"
score, quality = model.observe(later)
assert quality == "OK" and score[0] > 0
another = Model()
for _ in range(3):
    another.observe(first)
assert another.observe(later)[0] == score  # no future scans needed
model.reset()
assert model.observe(later)[0] is None  # new container intent resets baseline

groups = load(root / "data/edge-ai-package-v2/scans_all_valid.csv")
output, vectors, summaries = process(groups)
assert len(output) == len(vectors) == 2040
assert len(summaries) == 12
assert sum(row["quality"] == "LOW_INFORMATION" for row in output) == 282
assert sum(row["quality"] == "OK" for row in output) == 1721
assert all(row["change_index"] is None for row in output if row["quality"] != "OK")
assert all(row["profile_id"] == "354" for row in output)
print("PASS causal baseline, quality rejection, reset, 2040 traced scans")
