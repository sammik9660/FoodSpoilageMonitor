# Reliability v2 — operation, limits, validation

## Scope and preserved hardware

This change hardens the existing BME688 -> nRF52840 -> NUS -> ESP32-S3 -> Apps Script -> Sheets path. It does not deploy, erase historical rows, change the working URL, change partitions, adopt MQTT/BSEC, or claim new hardware success. Original TXT, the two user diagnostic sketches, historical reviews and mltest.ipynb are untouched.

Verified **by the user before this change**: V1940/nice!nano-compatible board using Feather nRF52840 Express variant; Wire.setPins(21,20) (D21/P0.31 SDA, D20/P0.29 SCL), address 0x76, chip ID 0x61, T/H/P 8X/2X/4X, IIR 3, heater 320°C/150ms. Units remain °C, %RH, hPa, kΩ. The 2026-09-28 incident and invalid historical data interval are recorded in PROJECT_STATE.md; its exact cause remains unknown.

## Sensor and wire protocol

Only a successful finite measurement increments SEQ and generates a sensor frame. Failed readings increment diagnostics and produce STAT only. Three consecutive failures enter recovery. Recovery resets/reinitializes address 0x76 and reapplies every setting; begin success alone is not counted as recovery until a measurement succeeds. Retry spacing grows 5/10/20/40/60 s. Successful measurement resets the backoff. The target start-to-start cadence is 2 seconds, without catch-up bursts; runtime delays still create jitter.

```text
BID=1234ABCD,SEQ=123,MS=246000,T=25.91,H=50.93,P=1004.04,G=71.63\n
STAT,BID=1234ABCD,MS=246000,FAIL=3,REC=2,TRY=2,TXERR=0,OK=1\n
```

BID is a SoftDevice-random boot discriminator (device/timer fallback if RNG unavailable), not a cryptographic identity. SEQ counts valid measurements, including ones produced while BLE is disconnected. MS is 32-bit nRF uptime (wraps at about 49.7 days). Gap tracking is per BID; same/backward SEQ is rejected, BID change reports a reboot. Sensor failures cannot be inferred solely from SEQ gaps because failed readings deliberately do not consume SEQ; STAT carries failure counts. A disconnected nRF does not retain samples: those are observable gaps, not guaranteed delivery.

Frames are split into <=20-byte chunks with newline framing. A leading delimiter limits damage from an interrupted prior TX. ESP rejects missing/reordered fields, trailing garbage, NaN/Inf, overflow and impossible configured ranges. CR is allowed only immediately before LF. On overflow discard through LF; on disconnect clear the partial frame. Diagnostics are not inserted into the sample queue.

## Gateway identity, capture time and ACK

Every accepted BLE sample receives a 128-bit random gateway boot ID and increasing 32-bit receive sequence. UID is 32 lowercase hex characters + `-` + decimal sequence. It is assigned once, before queueing, and never regenerated for retry/reboot replay. The nRF sequence is for BLE continuity; UID is for cloud dedup.

The ESP clock starts with NTP synchronization through configTime. At BLE receive time, synchronized-looking UTC is frozen into captured_at; otherwise time quality is unknown and nRF/ESP uptime remain in the payload. It is ESP receive time, not a claim of exact nRF acquisition UTC. Clock jumps, NTP correctness and RF latency require hardware validation. Unknown-time samples go to Unassigned; they are never assigned to whichever session is active when they arrive. No retroactive timestamp guess is made.

A version=2 JSON request contains samples (0..32) and current health. Each sample contains uid, seq, ms, nrf_boot, rx_ms, captured_at, quality and t/h/p/g. Batches stay FIFO within a gateway boot. Upload interval is 10 s; outage retry backoff is 10..60 s. BLE and networking run in separate tasks, so upload failure does not stop receipt. Long network calls do not block the main spool loop.

Release requires HTTP 2xx plus a parsed JSON object with success=true, version=2 and an exact, ordered ACK for EVERY submitted UID. Accepted statuses are stored, duplicate, not_recording and unassigned_time. not_recording means an intentional recording exclusion, not a saved experiment row. unassigned_time means the sample was saved in Unassigned. Missing/malformed/mismatched ACK or any failure leaves the batch pending. Worker owns an immutable copy; new BLE samples cannot change the request identity.

## Persistent outage spool: exact bounds and limitations

Installed ESP32 core 3.3.11 huge_app.csv allocates app0=0x300000 and SPIFFS=0xE0000 = **917,504 bytes / 896 KiB**, despite the menu shorthand 1MB. Actual usable SPIFFS.totalBytes() is lower and MUST be read from the device BOOT log; no device was mounted/measured in this coding session.

- RAM queue: 128 samples. Active upload: at most 32. A separate staging chunk: at most 32. An HTTP worker copy is a duplicate, not additional unique samples.
- Persistent record: exactly 88 bytes, CRC32 over its first 84 bytes. Chunk header: 16 bytes with CRC, schema version and count. Chunk payload cost = 16 + 88*N, N in 1..32.
- Maximum 128 immutable chunks. Maximum full-chunk payload = 128*(16+32*88) = **362,496 bytes**, **4,096 samples**, equivalent to **2 h 16 min 32 s** at exactly 2 s/sample. This is an upper bound, not a promised outage duration.
- Additional bounds: own payload <= floor(actual totalBytes*0.60); all filesystem usedBytes plus requested payload <= floor(totalBytes*0.75). Metadata/GC allocation can still fail before these bounds; failed writes latch a filesystem fault.
- Effective full-chunk ceiling: `min(128, floor(floor(totalBytes*0.60)/2832))*32`, further limited by other files/GC and partially filled chunks. BOOT/health report budget and actual pending count. Filling only 5 samples per chunk gives 640 samples/~21m20s at the 128-file bound. During sustained outage the 60 s flush normally groups about 30 samples; 128 such chunks are 3,840 samples/~2h08m, before overhead/other limits. Neither figure substitutes for the measured BOOT capacity.
- Normal fast successful uploads do not write sample flash. Failed uploads are persisted; a still in-flight batch is persisted after 10 s. Other outage samples flush in chunks every 60 s or at RAM depth 96. Thus flash operations are grouped, not one per 2 s sample.
- Save uses a new .tmp chunk, flush/close, readback CRC, then rename. Fully valid .tmp files are recovered on reboot. Torn/corrupt chunks are retained and latch ERROR; no auto-format or silent deletion. SPIFFS is not a transactional power-fail-proof database: hardware power-cut testing is required.
- FIFO is by chunk filename. On valid cloud ACK, delete the complete chunk. Crash before deletion replays identical UIDs. Whole-file removal reclaims space; live data are never rewritten for compaction. If deletion fails, preserve the file and report a fault.
- Overflow never overwrites old data. When spool cannot save, staging/active/RAM retain data until RAM fills; further incoming samples are rejected with a DATA LOSS counter and log. Counters are gateway-boot-scoped; a lost heartbeat or power failure can prevent remote observation of the final counter. Disk corruption remains detectable after reboot.

**Power-loss window:** in a responsive loop, ordinary samples wait roughly one 10 s batch interval; slow/failed active batches are scheduled for persistence at 10 s, and fresh outage RAM samples at up to 60 s (plus filesystem/scheduler time). Startup WiFiManager may block for the configured 20 s connection attempt. The hard storage bound is **192 unique volatile samples maximum** (128+32+32), or 384 seconds worth of samples at exactly 2 s cadence. This is a sample-count bound, NOT an unconditional oldest-sample age guarantee: a filesystem stall/full/corrupt state can leave those samples pending indefinitely. There is no honest finite wall-clock loss-window guarantee in those faults; ERROR and possible newest-sample drops are explicit. Power loss before flush can lose RAM samples. Never claim zero loss or week-long local storage.

Filesystem mount uses SPIFFS.begin(false). A new/unformatted partition will fault; automatic format would erase possible evidence. Prepare a verified empty partition once using a separate, manually approved format sketch/tool after backing up any existing files. Do not enable format-on-failure in production or erase all flash on ordinary uploads.

## Sheets, durable ingest and recording

Keep the existing SPREADSHEET_ID Script Property. Missing/inaccessible ID or missing SensorData is an ERROR, not permission to create another spreadsheet. The first five existing headers/rows are preserved. Seven columns are appended:

| Columns | Meaning |
| --- | --- |
| timestamp, temperature, humidity, pressure, gas_resistance | Original names; new timestamp is ISO UTC capture/receive time |
| sample_uid, session_id | Stable identity and experiment membership |
| nrf_seq, nrf_ms | Continuity and nRF uptime |
| capture_quality, received_at, nrf_boot | ntp/unknown/legacy_server, server arrival UTC, sensor boot discriminator |

Sessions contains session_id, experiment_label, started_at, stopped_at. Label is stored once per session (join by session_id); it is not automatically a spoilage ground truth. Unassigned uses the same 12 headers for unknown-time samples. IngestState has one compact row per gateway boot with acknowledged high water and at most one pending batch journal. Unlike per-sample ScriptProperties, steady dedup state grows per reboot, not per sample. Never delete/reorder IngestState or pending rows during collection.

Under script lock, validate whole batch, finish earlier journals, reserve append positions in a write-ahead journal, bulk setValues, flush, then advance high water/clear journal. ACK is returned only afterwards. A retry after write-before-ACK interruption completes/replays the same reserved cells and cannot append duplicate rows. Conflicting manually edited rows cause explicit ERROR rather than overwrite. All cooperating readers/control operations use the same lock. Do not manually edit Sheets while collection is active; Apps Script cannot make unrelated external writers transactional. Ambiguous partial writes that conflict with the journal require manual repair.

Recording defaults to STOPPED (no open Sessions row). Start generates UUID session, label and server start time; only one session may be active. Stop writes stop time. Membership uses **[start, stop)** capture interval. Delayed samples captured before Stop still join that stopped session; pre-Start samples never enter the new one. Live telemetry continues when stopped. Legacy T/H/P/G and original long-name single POST are accepted but have server-arrival timestamps and fresh server IDs; they cannot provide retry dedup or historical capture-time guarantees. Use v2 firmware for experiments.

At exactly 2 s/sample: 43,200 samples/day, 302,400/week. Twelve filled cells per row is ~3.63 million/week, but allocated grid cells include unused columns. A retained 26-column grid uses ~7.86 million cells/week. Code intentionally refuses growth beyond **9.5 million allocated cells or 320,000 rows per sheet** as conservative project limits, not claims about Google's current service ceiling. Google announced a 20-million-cell limit in September 2026 ([official update](https://workspaceupdates.googleblog.com/2026/09/doubled-cell-limits-in-google-sheets-now-generally-available.html)); account/runtime performance must still be verified. Other sheets reduce the project's headroom. Archive/rotate deliberately before the bound; no auto-delete/reset. One-week feasibility depends on actual sample count and existing data.

Properties read/write quotas depend on account type and can change ([official quotas](https://developers.google.com/apps-script/guides/services/quotas)). One uploader and one 10 s dashboard perform roughly 17,280 SPREADSHEET_ID reads/day, plus controls/exports; samples and dedup do not write ScriptProperties. Additional viewers multiply reads and sheet calls, so this is not a multi-user load-test result.

## Dashboard, CSV and security

Dashboard.html uses 10 s polling; live health is in bounded CacheService (600 s), not per-sample Properties. Shows freshness, sensor/BLE/spool failure, recording, queues and counters. Lost cloud contact becomes STALE / UNKNOWN; a disconnected gateway cannot truthfully report a current cause through that same broken network. Last successful upload is from the preceding acknowledged cycle. The live view is for one gateway; multiple gateways require a per-device view before deployment.

CSV uses an absolute ScriptApp.getService().getUrl() URL with target=_blank. Existing `?download=csv` still works for <=10,000 data rows. Larger exports require explicit pages and fail rather than silently truncate: `?download=csv&offset=0&limit=10000`, then 10000, 20000, etc. Offset counts source SensorData rows excluding the header. Optional `&session=EXP-...` filters within that source window; page through the source range even if a filtered page is empty. Dashboard supplies offset controls and active-session link. For a stopped session copy its ID from Sessions. Each page has the same header; concatenate with pandas and deduplicate by sample_uid if export overlaps. Stop recording and wait for queues to drain before a reproducible export. This avoids holding an entire week's data in a single Apps Script response.

Current endpoint and AP credential literals are unchanged; credentials are no longer logged, but the AP name still contains the baseline credential. TLS setInsecure is still prototype debt. Public Apps Script deployments may expose Start/Stop and ingest without strong application authentication: restrict deployment to trusted users where compatible and add access control before untrusted/public use. This change does not claim security hardening or safe food classification.

## Build and automated validation

Windows/PowerShell; actual installed dependencies: Adafruit nRF52 1.7.0, Adafruit BME680 2.0.6, ESP32 3.3.11, WiFiManager 2.0.17. ESP uses the core's cJSON; no ArduinoJson install required.

```powershell
arduino-cli compile --fqbn adafruit:nrf52:feather52840 firmware/nrf52840/bme688_node
arduino-cli compile --fqbn 'esp32:esp32:esp32s3:FlashSize=4M,PartitionScheme=huge_app,PSRAM=disabled,CDCOnBoot=cdc,USBMode=hwcdc' firmware/esp32s3/gateway
node tests/cloud.test.cjs
```

Host C++ tests require a C++17 compiler. With MSVC Developer Command Prompt, choose output paths outside the repository:

```text
cl /std:c++17 /EHsc tests/integrity_test.cpp /Fe:<temp>/integrity.exe
cl /std:c++17 /EHsc /Itests/sensor_stubs tests/sensor_test.cpp /Fe:<temp>/sensor.exe
cl /std:c++17 /EHsc /Itests/spool_stubs tests/spool_test.cpp /Fe:<temp>/spool.exe
```

Run each resulting executable. Tests compile the real shared parser/spool and real nRF .ino with mock hardware. They are not electrical tests. Cloud tests evaluate actual Code.gs against mock Sheets with injected write failures, not an actual Google deployment.

Validation completed (2026-09-28/29):

- nRF compile passed: flash 144,764 / 815,104 bytes, static RAM 15,984 / 237,568 bytes.
- ESP compile passed with the listed 4MB/Huge APP/CDC options: flash 1,428,505 / 3,145,728 bytes, static RAM 55,884 / 327,680 bytes. Runtime queue/task/JSON allocations are additional heap use; no hardware heap watermark has been measured.
- 15 Node tests passed: additive migration; stopped behavior; dedup; capture boundaries/late arrival; unknown-time quarantine; strict validation; two legacy key formats; write/journal interruptions; conflict protection; session exclusivity/no recreation; CSV routing/paging; freshness; actual dashboard JavaScript/link execution.
- Three MSVC C++17 executables passed: real parser/framer/continuity/CRC/ACK-gated PendingBatch; real nRF .ino under failed-read/reset mocks; real Spool.h under reboot, rename interruption, corruption, torn-write and full-storage mocks.
- Existing endpoint/AP constants compared to HEAD without displaying their values: unchanged. Original TXT and historical/diagnostic files preserved. git diff --check passed.
- cJSON ACK checks and FreeRTOS network/task integration were compiled and reviewed, not exercised against a live cloud or real ESP. SPIFFS mock tests do not prove physical power-fail durability. No upload, Apps Script deployment, actual Sheet mutation, new hardware test, commit or push was performed.

## Manual deployment and acceptance sequence

1. Back up the existing spreadsheet and note its SPREADSHEET_ID, deployment ID, sharing/access settings. Preserve old data, including the documented invalid incident interval until you manually decide how to annotate it.
2. In the SAME Apps Script project, replace Code.gs and create Dashboard.html (HTML file named Dashboard). Keep the existing SPREADSHEET_ID property. Run setupReliability once and inspect that original rows remain, 12 headers extend correctly, Sessions/IngestState/Unassigned exist. If it errors, inspect schema/config; never clear SensorData as a fix.
3. Update the existing deployment to a new version using Manage deployments -> Edit -> New version. Do not create a different URL. Open the existing /exec URL, verify default STOPPED, live state and CSV route. Stop any old firmware/manual POST producer during the coordinated protocol upgrade.
4. Compile/upload ESP32 using the exact 4MB/Huge APP/PSRAM disabled/CDC enabled/Hardware CDC-JTAG settings above. Do not erase all flash. Open USB CDC at 115200; if unavailable inspect supported UART0 with a suitable adapter. Check BOOT, SPIFFS.totalBytes/budget/fault and NTP quality. Prepare a new empty filesystem manually only if verified safe; never treat a mount error as proof it is empty.
5. Compile/upload nRF Feather nRF52840 Express variant with the existing bootloader/port choice. Check D21/D20, sensor recovery success, SEQ/MS/STAT and gateway parse/queue ACK logs. Leave heater 320°C/150ms.
6. While STOPPED, verify live values change but SensorData does not grow. Start a named experiment; verify all valid ~2 s samples, units, IDs and timestamps. Stop; flush late batches and verify their original session. Start another session and ensure old samples do not migrate.
7. Disable hotspot/network long enough to spool. Confirm pending count increases. Re-enable, check FIFO and no duplicated sample_uid. Repeat a controlled gateway reboot only after observing a successful spool flush. Then separately test power interruption/torn write with expendable test data and a backup.
8. Inject/read-simulate sensor failure safely: confirm no T/H/P/G sample or valid SEQ on failed measurement, visible SENSOR ERROR, bounded recovery attempts and eventual recovery. Do not create unsafe electrical shorts. Test BLE disconnect mid-frame and overflow with a test sender.
9. Export small and paged CSV/session CSV; compare counts, UID uniqueness, capture boundaries and gaps. Join Sessions for labels. Do not use adjacent random train/test splits or humidity-generated labels.
10. Run a 24-hour soak before a week experiment. Record sample counts, sensor failures, BLE gaps, queue drops, flash capacity/latency, reboot IDs and power consumption. This step was not performed by the coding agent.
