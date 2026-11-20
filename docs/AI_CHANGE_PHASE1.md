# AI change detection Phase 1 — shadow candidate, no food safety verdict

The local AI branch starts at `codex/remote-tls-stabilization` commit `99df839`,
not the divergent `master` (`28f6eef`), which lacks recent OTA/DFU changes.
The supplied ZIP was extracted only under ignored `data/edge-ai-package-v2/`.
Its four-food, 1,716-byte MLP remains a reference and is **not** a spoilage
model. Nothing in this phase modifies the original workbook, sensor firmware,
cloud deployment, device flash, Sheets data or existing wire/storage schema.

Reproduce the read-only source audit and derived output with:

```powershell
python analysis/edge_change.py --scans data/edge-ai-package-v2/scans_all_valid.csv --workbook 'C:\Users\user\Downloads\Food Spoilage Monitor Data (2).xlsx' --output data/edge-ai-work/change-v1
python tests/ai_change_test.py
```

The output directory is ignored by Git and may be regenerated without touching
the original workbook or the extracted ZIP files.

## Source and quality

- `Food Spoilage Monitor Data (2).xlsx` in Downloads matches the ZIP audit's
  SHA-256 `64a5aea8e69a1b634df1cc71262b8a85c9c12f7421b1f6e5cdcdc9508b2848f1`.
- The supplied CSV has 2,040 complete ten-step scans from two devices. The
  new reader checks unique `(device_id,nrf_boot,scan_seq,profile_id)`, profile
  354, ten source rows and UIDs, scan duration <=180 seconds, and heater
  position 0..9 without changing kΩ measurements.
- `analysis/edge_change.py` emits separate `change_scores.csv`,
  `change_report.json` and `cpp_parity.txt`. It rejects 282 low-information
  scans (eight or more gas steps near 5.68 or log span <0.05), keeps 37 scans
  as per-group baseline acquisition, and scores 1,721 later scans. It marks
  21 gaps over 30 minutes; it does not interpolate missing measurements.
- Groups are kept by device plus physical `exposure_group`; rows without an
  exposure group use their own session ID. Different sensors and specimens
  are not pooled. The first three acceptable chronological scans form the
  baseline. A new explicit storage session must reset this baseline.

## Score and evaluation limit

The shadow score is
`0.7 * mean(abs(centered_log10_gas - baseline_shape)) +
0.3 * abs(mean_log10_gas - baseline_level)`.
The score is a within-container change index in log-gas units, **not** a
spoilage probability or a statement that food is safe. Its shape-only and
level-only components are retained for inspection. The optional trailing
three-score mean and slope use only current/past scans. Temperature, humidity,
pressure, elapsed time and scan gap are kept as separate causal features for
later composite reasoning; no unverified temperature or age threshold has
been turned into a food label.

Banana ambient early/late median change indices were 0.0183/0.0682 and rice
ambient 0.0188/0.0656. Tofu ambient, whose final condition is **unknown**,
rose to a late median 0.4267. This demonstrates that change alone is not a
validated deterioration classifier. The banana and rice observations are
positive **endpoints**, not onset timestamps; there are no verified negative
food observations. No binary accuracy, sensitivity, specificity or calibrated
threshold is reported. The package's food-class accuracy is unrelated to this
task and must not be reused as a spoilage metric.

## Native implementation and boundaries

`AiChange.h` is a standalone C++17 implementation with a fixed ten-step input
and per-session baseline; it has no heap allocation, model partition or
cloud/OTA/DFU dependency. Its MSVC instance size is 96 bytes.
`tests/ai_change_test.cpp` compared its output with
the Python score for all 2,040 scans (1,721 scored) within 1e-5. The packaged
reference `test_checked_inference.cpp` also passed, but its class output is
not used. The local gateway now observes accepted v3 frames through a
strict 0..9 scan assembler and a separate four-entry Shadow queue; it never
modifies sample, UID, ACK or spool release. The cloud response carries the
explicit storage session ID. The gateway activates its model only for the
matching session and sensor and persists the first three baseline scans in
NVS with a CRC. Reboot or OTA reloads only an exactly matching baseline;
otherwise the score remains unavailable until three new valid scans. Actual
ESP32 inference time and memory have not been measured on hardware.

The storage and AI additions remain uninstalled. Existing email alerts are
unchanged; endpoint labels and real false-alert testing are still needed
before any deterioration warning may use this change index.

## Storage session prototype (local only)

The separate `StorageSessions.gs` sheet and script property now hold an
explicit per-device new-food intent. The responsive dashboard has Start/End
controls distinct from experiment recording. A PENDING intent becomes ACTIVE
only when a later valid ENV sample from the same sensor is reported; ordinary
reboot, BLE reconnection, ESP OTA and nRF DFU do not create another intent.
The gateway adds only an optional `environment.captured_at_ms` health field;
existing BLE, sample, spool and ACK schemas are unchanged. This timestamp is
the gateway's UTC when it receives the valid nRF ENV frame. The nRF itself
does not send absolute UTC, so this is the closest available start-time
estimate and includes BLE delivery latency. The field is absent when gateway
time is untrusted, leaving the intent PENDING until the next timed measurement.
Cloud mock tests cover persistence and the later-measurement gate. The code
has not been deployed; no live Sheets rows were changed. A separate
`StorageTelemetry` sheet would sample matching sensor/session ENV at most
once per five minutes and retain the timestamp of each new gas change score.
The mobile dashboard can display the last 288 points and keeps the score
labeled Shadow Mode. No AI alert or food-safety state is connected to the
existing email alerts.

## Separate learned anomaly research

`analysis/anomaly_research.py` trains a per-exposure diagonal Gaussian on
the first six quality-accepted ten-step log-gas scans (20 float32 parameters,
80 bytes). Its scale has a 0.05 log10 floor to control tiny-sample variance.
It is compared causally with the three-scan fixed change index in
`data/edge-ai-work/change-v1/anomaly_research.json`; neither uses endpoint
observations as scan labels. Ambient banana/rice late medians increase in
both methods, but outcome-unknown ambient chicken/tofu also increase more.
There is no defensible binary accuracy or ranking of food safety. The
trained anomaly model is research only and is not exported to the gateway.

## AI Shadow activation diagnostics (local changes, not deployed)

`recording.session_id=EXP-...` is an experiment identity and never activates
AI. The separate `storage_session.storage_session_id=STO-...` is created only
by the dashboard's explicit **새 음식 시작** action. Cloud marks it ACTIVE after
a subsequent valid ENV value with gateway receive UTC newer than the request.
The latest STAT `environment_ok` may lag that accepted ENV frame, so it no
longer blocks storage activation. Existing recording stays as it was.

The serial `ACK BODY` preview is truncated before the later storage field; it
cannot prove the field is absent. New transition logs report `AI SHADOW`
state, active flag, reason and baseline 0/3..3/3, plus each complete accepted
scan and any later change score. The Basic-authenticated local `/status` JSON
and health heartbeat expose `ai_shadow.feature=1` even when inactive. If the
field is missing locally, the installed firmware lacks these diagnostics.
`STORAGE_FIELD_MISSING` indicates a successful ACK without the separate
storage response; `STORAGE_MODULE_MISSING` means the cloud deployment lacks
`StorageSessions.gs`; `GATEWAY_ENV_UTC_MISSING` means the gateway has not
reported a trusted captured ENV time. A newly started session may wait up to
the next 120-second ENV measurement, then three valid 10-minute gas scans
establish the baseline. A later valid complete scan produces the first score.

After separate deployment approvals: deploy `Code.gs`, `StorageSessions.gs`
and `Dashboard.html` together; install the matching ESP firmware without
erasing NVS/SPIFFS; verify `/status` contains `ai_shadow.feature=1`; click
**새 음식 시작** for the intended device; observe PENDING→ACTIVE and baseline
0/3→3/3. Existing `EXP-...` recording need not be ended or restarted.
