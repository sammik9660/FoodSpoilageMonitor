# ESP32-S3 PSRAM and technical-debt audit

This note records source and build-setting evidence. It does not claim a new
hardware run, cloud deployment, OTA/DFU installation, or long-duration test.

## Active memory configuration

The saved Arduino build options use ESP32 core 3.3.12 with 16 MB flash and
`PSRAM=opi`. Core 3.3.12 maps that menu choice to `BOARD_HAS_PSRAM` and the OPI
PSRAM type. The sketch-local `partitions.csv` remains authoritative and keeps
NVS, `otadata`, two 3 MiB application slots, and SPIFFS at
`0x310000`/`0xE0000`; PSRAM does not change this flash layout.

At boot the gateway reports flash size, total and free PSRAM, followed by
explicit internal 8-bit heap free/largest/minimum and PSRAM free/largest
figures. A valid N16R8 run should report approximately 8 MiB total PSRAM. A
non-zero build setting alone is not proof that the physical chip initialized;
the boot log is the runtime evidence.

## Allocation decision

The serialized HTTP request is the only large application allocation retained
through the POST TLS handshake. A maximum 32-record v3 request is about 13 KiB.
It is now copied to an exact-size PSRAM allocation after cJSON serialization;
the original internal allocation is released before the network task starts.
`HTTPClient::POST(uint8_t*, size_t)` in the installed core writes from the
provided buffer synchronously. If PSRAM is absent or the allocation fails, the
existing internal-RAM payload is retained and the failure is logged once. The
same helper is used for the small OTA cloud-lease request.

The following remain in internal memory intentionally:

- FreeRTOS queue control/storage, task stacks, BLE/network objects and TLS
  state, because their library/driver access constraints are more important
  than their potential size reduction.
- The 128-sample queue (15,360 bytes) and active/staging sample arrays (7,680
  bytes), because they are hot, cross-task data and are part of the existing
  reliability path.
- OTA manifest/SHA state and DFU control buffers, which are small. nRF DFU
  already streams its ZIP/BIN from SPIFFS in 20-byte packets and never retains
  the complete image in RAM.
- ACK response `String` and cJSON parse tree. Normal Apps Script ACK bodies are
  only a few hundred bytes; moving these would add allocator complexity without
  addressing the observed handshake pressure.

Before/after hardware numbers are not available in this coding run. The
expected internal-heap release at `POST-pre` is exactly the serialized payload
size shown by `HTTP POST begin ... bytes=... payload_mem=psram`. Compare the
new `HTTPS heap` internal/largest/minimum and PSRAM columns over repeated
POST/redirect cycles. This optimization does not prove that all TLS allocation
failures are fixed.

## Existing safeguards and open items

- The decoded lwIP crash was in core 3.3.12 DNS cache handling. Gateway HTTPS
  clients currently resolve through thread-safe `lwip_getaddrinfo()` and keep
  the hostname for TLS SNI. Wi-Fi event callbacks only record state, and mDNS is
  initialized once. Hardware recurrence testing is still required.
- Brownout detection remains enabled. Earlier brownouts cannot be attributed to
  firmware from logs alone; supply/cable/regulator voltage under Wi-Fi current
  peaks must be measured on hardware.
- POST and redirect GET clients have separate scopes and explicitly call
  `HTTPClient::end()` and `WiFiClientSecure::stop()`. The request payload is
  released after POST, before redirect TLS setup. No confirmed object leak was
  found; fragmentation and momentary internal-heap exhaustion remain hardware
  checks.
- TLS still uses `setInsecure()`. This is a known prototype security debt and
  was not changed without a tested certificate/provisioning path. Local OTA and
  nRF package upload retain Basic authentication plus Host/Origin checks.
- SPIFFS mounts with `begin(false)`, never auto-formats, and releases samples
  only after exact application ACK. UID/dedup, capture-time session assignment,
  legacy 88-byte replay and current 120-byte records are unchanged.
- Dashboard reads bounded live cache state and CSV export remains paged at
  10,000 source rows. No current evidence justifies redesigning it around a
  full-data download or new date-range API.
- HP-354 raw rows and the fingerprint completeness tool support dataset
  preparation. There is no validated spoilage classifier or ground-truth label
  pipeline; experiment labels must not be treated as food-safety truth.

The source currently sets HP-354 attempts to 600,000 ms, while the repository
agent instructions describe two minutes. This audit does not change the working
firmware cadence; the intended authoritative interval should be reconciled
before the next sensor release.
