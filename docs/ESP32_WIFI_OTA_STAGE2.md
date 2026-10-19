# ESP32-S3 Wi-Fi OTA — Stage 2

## v2.0.7 to v2.0.8 validation

The user reports USB-installed 2.0.7/build 2026101905 with BLE, cloud ACK and spool drain working. Its OTA receiver is still unverified. Version 2.0.8/build 2026101906 changes only the release identifiers; retain the settings and custom partition below.

Build the current sketch, then copy its application-only `gateway.ino.bin` into `data/ota-2.0.8/`. Never use the merged image, bootloader or an earlier application binary. Sign and verify from the repository root in PowerShell 7:

```powershell
.\tools\sign_esp32_ota.ps1 -FirmwareBin .\data\ota-2.0.8\gateway.ino.bin -Version 2.0.8 -BuildNumber 2026101906
.\tests\ota_package_test.ps1 -FirmwareBin .\data\ota-2.0.8\gateway.ino.bin
```

Select that folder's `gateway.ino.manifest` in Manifest and `gateway.ino.bin` in Firmware. Keep recording STOPPED and let PREPARE drain pending data; do not bypass safety checks. Observe `OTA manifest received`, then either `OTA manifest verified` or `OTA manifest verify failed stage=...`. FORMAT means field/header/canonical format, TARGET means wrong target, VERSION means syntax or unchanged version, BUILD means non-increasing build, BASE64 means decoding failure, HASH means SHA operation failure, KEY means public-key parsing failure, and SIGNATURE means ECDSA verification failure. These stage logs are emitted by the installed receiver, not by the file being uploaded.

After acceptance, require `FIRMWARE build=2.0.8`, SPIFFS mount, BLE reconnect, cloud ACK, and `OTA boot self-check passed; rollback cancelled`. Confirm the rainbow and normal LED return visually. A package test is not evidence of a successful hardware OTA. Preserve earlier packages unchanged.

## v2.0.7 build settings

Build the gateway with ESP32 Arduino core 3.3.12, ESP32S3 Dev Module, Flash Size 16MB, OPI PSRAM, USB CDC On Boot enabled, Hardware CDC and JTAG, and Erase All Flash disabled. The sketch-local `partitions.csv` remains authoritative: both OTA slots are 3MiB and the existing SPIFFS region stays at `0x310000`/`0xE0000`. Version 2.0.7 uses build `2026101905`, which is strictly newer than the installed 2.0.5/build `2026101903` and the rejected 2.0.6/build `2026101904` package under the existing anti-rollback check. Hardware boot, wireless installation and the new manifest diagnostics/lease cleanup ordering remain unverified until the user tests the signed matching image.

After Arduino IDE **Sketch > Export Compiled Binary**, verify that the generated image contains version 2.0.7/build 2026101905, copy it into a new `data/ota-2.0.7/` directory, and sign that exact file:

```powershell
.\tools\sign_esp32_ota.ps1 -FirmwareBin .\data\ota-2.0.7\gateway.ino.bin -Version 2.0.7 -BuildNumber 2026101905
```

Do not rename an older image. Verify the BIN is no larger than `0x300000`, its SHA-256 equals the manifest `sha256`, and the manifest signature verifies against `ota_signing_key.h` before offering both files to the local updater.

The earlier `data/ota-2.0.6/` pair is retained unchanged for diagnosis. Do not mix its manifest or BIN with the 2.0.7 files.

This stage adds a signed, local-network updater for the ESP32-S3. The user has since installed firmware 2.0.0 by USB; the live partition table and wireless update path still need direct verification.

## Partition proposal

`firmware/esp32s3/gateway/partitions.csv` preserves the current 16 MiB Huge APP NVS, OTA metadata, app0, SPIFFS and coredump offsets and sizes. It adds app1 at `0x400000` with the same 3 MiB size as app0. Both app slots are 64 KiB aligned and end below 7 MiB, leaving the remaining flash unallocated. The existing SPIFFS region remains exactly `0x310000` + `0xE0000` (896 KiB).

This local CSV and compiled table are not proof of the device's current table. Read and compare the device table before claiming the OTA slots are present. Keep “Erase All Flash” disabled. Although the SPIFFS address and length are unchanged, a mismatched table or wrong CSV can make stored data inaccessible.

ESP32 Arduino core 3.3.12's ESP32-S3 sdkconfig enables `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` and `CONFIG_APP_ROLLBACK_ENABLE`. The updater defers the core's immediate rollback check and confirms the new image only after SPIFFS, BLE first frame, Wi-Fi and a fresh cloud ACK succeed. If those conditions do not arrive, the image remains pending verification and a reboot should select the prior slot. This lifecycle still needs physical tests on the actual bootloader.

## Signed update flow

The browser updater runs on the gateway's local HTTP address. The user must first connect USB serial and enter `OTA TOKEN`; the one-time printed random token becomes the HTTP Basic Auth password (`admin`). Only locally selected `.manifest` and `.bin` files are accepted. The manifest is ECDSA P-256 signed and binds target, version, build number and SHA-256 digest. The device verifies the signature before writing and streams the binary digest while `Update` validates the image. The page requires an explicit approval checkbox. The server reserves the device as `UPDATING` under the same script lock used by recording start, preventing an experiment from starting during the transfer. SPIFFS must be mounted and healthy, all RAM/spool samples drained, and Wi-Fi connected. A failed reservation, signature, digest or image validation leaves the current app selected and releases the server reservation.

Generate a replacement key only when intentionally rotating the signing identity:

```powershell
.\tools\generate_esp32_ota_key.ps1
```

The private key is locally stored at ignored `secrets/esp32-ota-signing-key.pem`. Back it up in a protected password manager/offline vault. Losing it requires a deliberate key-rotation path. The public key in `firmware/esp32s3/gateway/ota_signing_key.h` is compiled into the device.

For a future release, build the ESP32 sketch using the custom table and current USB settings, confirm the binary is no larger than 3 MiB, increment firmware version/build number, then sign:

```powershell
.\tools\sign_esp32_ota.ps1 -FirmwareBin <path-to-gateway.ino.bin> -Version 2.0.1 -BuildNumber 2026101702
```

The output manifest and binary are a matched pair. Never accept an unsigned image or reuse an old build number. The browser, device and gateway must share a trusted LAN. Local HTTP does not encrypt the Basic Auth token; use a trusted network and rotate the token if exposed.

## Cloud/UI integration

The local Apps Script code appends `ota_state` to `DeviceRegistry`, rejects recording start for a device in `UPDATING` or `VERIFYING`, and keeps other devices independent. The dashboard shows each device's OTA state and a local updater link when its private LAN address is available. The production Apps Script is not deployed by this stage; deploy the matching `Code.gs` and `Dashboard.html` together only after separate approval.

## Validation status

As reported by the user on 2026-10-17, firmware 2.0.0 was installed by USB and booted with SPIFFS mount, local updater address, Wi-Fi and cloud ACK. The on-device partition table, running/next OTA slots, wireless update, and rollback remain unverified. The next local image is version 2.0.1/build 2026101702; its rainbow appears only after the new OTA boot passes self-check and rollback is cancelled. USB upload and ordinary reboot do not trigger it. The local build uses ESP32 core 3.3.12, 16MB, Huge APP menu, PSRAM disabled, CDC enabled, Hardware CDC/JTAG; the sketch's `partitions.csv` overrides the menu table. Keep full-flash erase disabled. The physical N16R8 PSRAM is not used by this verified configuration.

The yellow/orange GPIO48 NeoPixel is `PIXEL_WAIT`: BLE and Wi-Fi are connected, but cloud ACK is pending or a RAM/SPIFFS backlog remains. It is not a temperature, gas, or spoilage alarm. `otaSafeState()` also requires mounted healthy SPIFFS, zero spool and RAM/in-flight samples, Wi-Fi, a USB-issued token, and no pending OTA verification. Keep the sensor and gateway connected until each sample receives its application ACK and the queue/spool drain to zero; if a new sample arrives during transfer, the update is rejected and its sample remains queued. Schedule a quiet maintenance window after drain. Do not clear the spool to make OTA available.

The nRF source sets COOL to 60 seconds after scan completion and resumes ENV at timeout; the v2.0.5 worktree uses a 600-second scan interval and retains the 60-second scan timeout. The ESP retains the last `sensorHealth.mode` on BLE disconnect, so the dashboard could display stale COOL; this is not proof that it explains the earlier reported ~12 minutes. Confirm the new interval with timestamped nRF serial `STAT MODE`, `SCAN`, `RECOVERY`, and ESP BLE/`SENSOR metadata` lines together with the display state.

- Host Apps Script tests include update-reservation/recording mutual exclusion with virtual devices only.
- `tests/ota_partition_test.py` checks preserved offsets, alignment, overlap, and 16 MiB bounds.
- `tests/ota_signing_test.ps1` exercises the manifest signer and public-key verification with a temporary dummy image; it does not build or flash firmware.
- The ESP32 OTA 2.0.1 source compiles with the specified FQBN outside the Codex sandbox. The compiled `partitions.bin` matches the sketch CSV and the app fits its 3 MiB slot. Host gateway UI, cloud, and partition tests passed; these do not prove OTA success on hardware.
- No wireless update, live partition-table read, rollback test, production deployment, nRF DFU or ESP-to-nRF relay was performed in this task.

## First hardware rollout checklist

1. Stop recording, wait for application ACK and zero pending RAM/spool samples, then read the device partition table without writing flash.
2. Confirm flash is 16 MiB and the live NVS, otadata, both app slots, SPIFFS, and coredump offsets match this CSV exactly. Stop if they differ.
3. Verify existing 2.0.0 boots with SPIFFS, BLE, Wi-Fi and cloud ACK. Rotate the OTA token over USB and test reservation rejection while recording.
4. Build 2.0.1, sign its matching `.bin`/`.manifest`, and with all queues drained install the pair over local Wi-Fi. Confirm next-slot boot, self-check/rollback cancellation, three-second rainbow, SPIFFS, BLE, Wi-Fi, cloud ACK, and return to normal LED status.
5. In a separate controlled test, interrupt transfer and then induce failed self-check to verify the prior slot remains bootable; protect pending data first.

Any SPIFFS mismatch or rollback failure is a stop condition. Use USB recovery only after protecting the existing data.
