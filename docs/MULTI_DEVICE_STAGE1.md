# Multi-device stage 1: identity, sessions, and dashboard

This change supports two gateways in the same Apps Script deployment while leaving the existing sample UID, gateway-boot high-water ACK, journal, and retry protocol unchanged.

## Identity and pairing

- ESP32 `device_id` is the lower 48-bit eFuse MAC rendered as 12 lowercase hexadecimal characters. It remains stable across reboot and does not use or rewrite Wi-Fi/NVS credential keys.
- nRF advertises its full 64-bit factory identifier as `BME688-` plus 16 lowercase hexadecimal characters in the scan response, while keeping the NUS service and message framing unchanged.
- ESP pairing target is stored in the separate `fsm-pair` NVS namespace. Over USB serial, `PAIR?` prints the gateway ID and target; `PAIR <16 lowercase hex digits>` sets a sensor target; `PAIR ANY` restores the legacy first-NUS-device behavior. Changing the target disconnects the current sensor so BLE rescans against the new setting; connection logs include the sensor ID. Pair each gateway to its own sensor before two sensors advertise together. A blank target retains single-device compatibility, but does not provide cross-pair protection for a two-gateway deployment.
- Cloud accepts old requests without `device_id` as `legacy`. New IDs are validated before any sheet write. UID and ACK high-water continue to use the original random gateway boot ID.

## Sheet schema and migration

The first execution of the updated Apps Script adds `device_id` as the last column of `SensorData`, `EnvironmentHistory`, and `Unassigned`; adds it after the four existing columns in `Sessions`; and creates an additive `DeviceRegistry` tab. Existing rows, the first 22 data columns, the first four session columns, and `IngestState` are retained. Rows with no recorded ID stay `legacy`; they are never attributed to either physical gateway. New sessions are scoped to one ID and sample assignment remains capture-time based. Live and recording caches use one key per device. Device CSV links pass both session and device filters.

No production deployment or real spreadsheet migration was performed in this stage. Before deployment, export a copy of the spreadsheet and verify existing header rows. The code fails closed on occupied/unrecognized extension columns. Rollback can restore the prior Apps Script source/deployment without deleting appended columns or rows; while the old global-session code is active, operate only one gateway at a time because it cannot enforce device isolation. Keep the additive columns and registry tab during rollback.

## Verification status

`node --test tests/cloud.test.cjs` covers two virtual IDs, concurrent independent sessions, device-specific live cache, device CSV filtering, legacy compatibility, and existing journal/ACK cases. These tests use mock Sheets only. ESP and nRF builds must be reported separately from actual BLE pairing. No second gateway exists in the current lab, and no device upload or Apps Script deployment is part of this stage.

## Two-device hardware check when unit 2 is built

1. Read the nRF `BLE sensor ID` from each sensor's USB log and verify the IDs differ.
2. Connect each ESP32 separately, issue `PAIR <16-digit sensor ID>` over its USB serial console, then `PAIR?`; power-cycle and confirm the target remains stored. This uses the `fsm-pair` namespace and must leave the saved Wi-Fi networks untouched.
3. With both nRF sensors advertising, confirm each ESP log shows only its configured sensor ID and the matching `device` ID remains unchanged after reboot.
4. After an approved Apps Script deployment, start different sessions on each dashboard card. Confirm each gateway's sample is written with its `device_id`, each session stays independent, and each CSV contains only that device/session. Stop one device and verify the other remains recording. Repeat with one device offline and confirm the other card still refreshes.
5. Repeat a retry/reboot replay and compare UIDs and ACKs to verify the existing deduplication behavior.

## OTA / DFU follow-up (not implemented)

- The installed ESP32 core 3.3.12 `huge_app.csv` is `nvs 0x9000/0x5000`, `otadata 0xe000/0x2000`, `app0 0x10000/0x300000`, `spiffs 0x310000/0xE0000`, and `coredump 0x3F0000/0x10000`. There is only one app slot. The last successful baseline build was 1,466,957 bytes in a 3 MiB app partition. A candidate dual-slot CSV can preserve every current partition and add `ota_1 0x400000/0x300000`; it would leave 9 MiB after `0x700000` for later model/reserved storage. This is a layout proposal only; the running board table and content of unallocated flash were not read in this stage. Validate both before any table write. The candidate preserves SPIFFS offset/size and NVS Wi-Fi data, but still changes the partition table and requires a full SPIFFS backup, recorded table backup, interrupted-OTA/rollback tests, signed artifact verification, and a recording-state interlock. Use a binary host, not Apps Script. See [Espressif partition-table guidance](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/partition-tables.html).
- The nice!nano vendor documents that its v0.6.0 image is the Adafruit nRF52 bootloader. The official bootloader supports BLE OTA DFU, and its upstream board list includes `nice_nano`; the installed Adafruit Arduino core's Feather 52840 target defaults to S140 6.1.1 with SoftDevice FWID `0x00B6`, matching the reported SoftDevice. That makes this route technically plausible. The exact boot entry gesture, release-specific DFU key/settings, and a package built from this project's compiled image have not been verified on this unit. Next, record the bootloader/settings identity read-only, produce a test package with the matching device type and S140 FWID, then try smartphone/PC BLE DFU and UF2 recovery on one expendable unit. Do not replace the bootloader or SoftDevice in stage 1. References: [nice!nano bootloader](https://nicekeyboards.com/docs/nice-nano/), [Adafruit bootloader BLE OTA and package instructions](https://github.com/adafruit/Adafruit_nRF52_Bootloader).
- ESP32-to-nRF DFU relay remains blocked on successful standalone BLE DFU and a memory/transport failure analysis.
