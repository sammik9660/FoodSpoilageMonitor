# Experimental GAS edge inference — local integration only

The source is the unchanged `Food Spoilage Monitor Data (4).xlsx` workbook.
`data/gas-ai-20261116/food_model.json` is a six-food nearest-centroid model
trained on 2,973 quality-accepted complete HP-354 scans. Its supervised
exposure holdout is valid for only 2,043 scans: egg and sprouts each have one
physical exposure, leaving their class absent from the corresponding training
fold. Eligible accuracy is 1,104/2,043 (54.04%), with severe class imbalance.
It is an **experimental food identity candidate**, never a spoilage or safety
probability. The model remains `release_approved=false`.

## Exact input and output

`AiScan.h` passively assembles accepted BLE v3 steps GI 0..9 into one scan keyed
by nRF boot and scan ID. It retains measured resistance in kΩ and the mean of
the ten T/H/P values. It rejects invalid status, sequence, step or measurement
index order without generating missing steps. A separate four-entry RAM queue
receives only a complete scan already accepted by the original sample queue.

`AiFoodModel.h` computes `log10(gas[0..9])`, subtracts that scan's mean log
gas, appends average temperature °C, humidity %RH and pressure hPa, applies
the saved 13-element mean/std, then compares squared Euclidean distance to
the six saved centers. It rejects non-finite/nonpositive gas, eight or more
5.68 kΩ floor values, low log span and invalid T/H/P. The output is candidate
label, squared distance, runner-up gap, measurement time and reason. The gap
is **not a calibrated confidence**; one-exposure classes are marked
`ABSTAIN_ONE_EXPOSURE`. The original 1,716-byte four-food MLP is preserved as
an independent reference and is not called by this runtime.

`AiAnomaly.h` is a separate per-sensor, per-STO-session model. Its first six
quality-accepted complete scans fit ten mean/standard-deviation log-gas
values, with 0.05 log floor. Later scans produce the mean absolute
standardized deviation and an optional three-result slope per hour. It has
no spoilage target or threshold. State is written to a new CRC-protected NVS
key under the existing AI namespace and restored only for the same sensor,
STO session and start time. The original three-scan Shadow change model and
its NVS blob remain unchanged.

Python/C++ parity uses all 3,385 audited complete scans from the local copy:
3,093 food measurements and 3,008 later anomaly scores matched within
the documented host tolerances. This is numerical equivalence, not accuracy
or ESP latency validation.

## Cloud display and remote diagnosis

The gateway adds `food_ai`, `gas_anomaly`, `sensor_tx_errors` and passive scan
partial/rejected/mask counters to health. Existing sample, UID, ACK, spool,
BLE, OTA and DFU fields do not change. `Code.gs` validates and caches the
optional fields. The dashboard shows the experimental food candidate and GAS
anomaly separately from the rule-based storage risk and the unavailable
spoilage AI. Older gateways simply omit the optional fields.

The previous local workbook captured a pattern where one nRF boot repeatedly
reported only GI 0..2 while the other device supplied GI 0..9. A missing GI
cannot be reconstructed at the gateway. The passive mask and existing
`sensor_failures`, `sensor_recoveries`, `sensor_attempts`, `sensor_tx_errors`,
BLE gaps/restarts distinguish an incomplete scan from a cloud transport
problem, but cannot identify the physical cause of I²C or heater failure.

## Reproduce locally

1. Run `analysis/review_current_data.py` against the read-only workbook into
   an ignored `data/` output directory.
2. Run `analysis/train_gas_ai.py` and `analysis/infer_gas_ai.py` using the
   generated `scans.json` and model JSON.
3. Run `analysis/export_gas_edge.py` using that exact model JSON. Its hash is
   embedded in the generated header.
4. Run `analysis/export_gas_vectors.py`, `tests/gas_ai_test.py`, the native
   `tests/gas_edge_parity_test.cpp`, and the existing reliability suite.

No installed device or operating Apps Script was changed. A safe field trial
requires separate approval, matching source/build identity, spool protection,
and checking cloud heartbeat, BLE and complete scans after installation.
