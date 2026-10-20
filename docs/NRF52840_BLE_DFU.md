# nRF52840 application-only BLE DFU

## Confirmed compatibility boundary

The physical sensor is user-reported as a V1940 Pro Micro nRF52840 / nice!nano V2-compatible clone with Board-ID `nRF52840-nicenano`, Adafruit nRF52 Bootloader 0.6.0 and S140 6.1.1. The project builds it with Adafruit nRF52 Arduino core 1.7.0 using the `Adafruit Feather nRF52840 Express` target and `nrf52840_s140_v6.ld`. The nice!nano documentation identifies its bootloader as Adafruit's nRF52 bootloader, whose upstream board list includes `nice_nano` and whose bootloader implements BLE OTA entry through `GPREGRET=0xB1`.

The application registers the core's `BLEDfu` service before NUS. A compatible phone writes the legacy buttonless start command, after which the application disconnects and jumps into the already-installed bootloader. Version 2.0.6 also exposes its firmware revision through the standard BLE Device Information Service without changing NUS advertising or sensor frames. These changes do not write or replace the bootloader, MBR, SoftDevice, UICR or flash layout. The user completed an application-only update on the clone with the package described below; interrupted-transfer recovery remains untested.

This is **Adafruit/Nordic legacy DFU**, not Nordic Secure DFU. The reported 0.6.0 bootloader and the default generated application package use CRC, device-type and SoftDevice compatibility checks but do not establish the same cryptographic authenticity as the independently signed ESP32 OTA path. Enabling signed nRF packages would first require verifying and deliberately provisioning a matching signed bootloader; that is outside this change and must not be attempted without a recoverable SWD setup and separate approval.

## Package creation

Compile `firmware/nrf52840/bme688_node` with Adafruit nRF52 core 1.7.0 and board `Adafruit Feather nRF52840 Express`, S140 6.1.1. Arduino CLI equivalent:

```powershell
arduino-cli compile --fqbn adafruit:nrf52:feather52840 `
  --output-dir data/build-2.0.6-nrf firmware/nrf52840/bme688_node
```

The core creates `bme688_node.ino.zip`; use the ZIP itself for BLE DFU. Its manifest must contain only the application image and must report device type `82` (`0x0052`) and SoftDevice requirement `182` (`0x00B6`). Do not substitute a UF2, ESP BIN, bootloader ZIP, or a package built for S140 v7. Preserve the generated BIN/DAT/manifest together.

The 2026-10-19 version 2.0.6 build produced `data/nrf-dfu-2.0.6/bme688-node-2.0.6-legacy-dfu.zip` (162,102 bytes). `SHA256SUMS.txt` beside it records the package checksum for transfer-integrity checking. The ZIP was inspected after creation: it contains only the 161,240-byte application BIN, 14-byte DAT and manifest; the BIN contains the `2.0.6` and `BLE DFU service` build strings and does not contain the old `2.0.5` string. The manifest reports device type 82, SoftDevice requirement 182 and DFU format 0.5. This checksum does not add firmware authenticity to the legacy bootloader protocol. The earlier 2.0.5 package remains unchanged.

## Smartphone procedure

1. Stop the recording and let the ESP gateway acknowledge its existing queue/spool. Temporarily power off the ESP gateway or otherwise disconnect it from the sensor because the current peripheral configuration permits one central connection.
2. Keep the nRF powered from a stable source and place the generated DFU ZIP on the phone.
3. On Android, use Nordic Semiconductor's **nRF Device Firmware Update** app, which supports nRF5 SDK legacy and secure DFU ZIPs. Disable **Request high MTU** so the old bootloader uses the default ATT MTU 23, and use PRN 8 or lower. Select `bme688-node-2.0.6-legacy-dfu.zip`, then select the sensor named `BME688-<sensor-id>` and start the update. The user verified the Bootloader enabled, DFU initialized, Firmware uploaded and Completed stages with these low-capacity transport settings. Keep the screen active and remain near the sensor.
4. If using Adafruit Bluefruit LE Connect, connect to the sensor, open **Updates**, choose custom firmware and select the generated package when the installed app accepts its format. Phone/app compatibility with this legacy package is not yet verified; the Nordic nRF DFU app is the primary Android path.
5. After completion, restore the ESP gateway. Confirm the nRF boot message contains `version=2.0.6`. A BLE GATT browser can also read Firmware Revision `2.0.6` from Device Information Service characteristic 0x2A26. Then verify ESP BLE reconnection, 2-second ENV frames, the first gas scan after about 10 seconds, and the following scan about 600 seconds later. Check NUS framing and cloud ACK as before.

The first installation of a DFU-enabled application requires USB/UF2/serial upload if the firmware currently on the sensor does not expose legacy DFU service UUID `00001530-1212-EFDE-1523-785FEABCD123`. The tested physical sensor now has this service and completed the 2.0.6 smartphone update.

## ESP32 relay Phase 1

The gateway relay core reads a pre-staged `/nrf-dfu.zip` from SPIFFS. It accepts only the tested application-only Legacy DFU 0.5 shape and checks ZIP CRC32, the BIN CRC16 carried by DAT/manifest, device type `0x0052`, SoftDevice requirement `0x00B6`, and the application size before entering maintenance mode. It streams 20-byte packets with ATT MTU 23 and PRN 8; it does not load the full image into RAM or touch the nRF bootloader/SoftDevice.

The relay is deliberately unavailable in legacy ANY-sensor mode. `PAIR <16-lowercase-hex-id>` must select the connected sensor, cloud recording must be confirmed STOPPED, and pre-existing RAM/spool/HTTP work must drain without deletion. `NRF DFU?`, `NRF DFU START`, and `NRF DFU CANCEL` are the Phase 1 serial controls. A transfer is reported successful only after bootloader validation/activation, NUS reconnection, and a non-empty Device Information Firmware Revision read.

The existing local gateway management server now exposes `http://<gateway-ip>/nrf-dfu`. It reuses the ESP OTA token as the Basic-auth password (`admin` is the user) and the same Host/Origin check; there is no unauthenticated upload route. Upload is accepted only while recording is confirmed STOPPED and RAM queue, SPIFFS spool, HTTP/ACK work and both update state machines are idle. The browser streams one ZIP to `/nrf-dfu.upload.tmp`; the gateway reserves the complete spool budget plus filesystem headroom, validates the same Legacy DFU structure and CRCs, and then replaces `/nrf-dfu.zip` through a recoverable backup rename. A failed, state-raced, aborted, or 60-second stalled upload removes only the temporary file and preserves the prior package and sensor samples.

Uploading a ZIP does not start a transfer. After the page reports that the validated package was stored, use `NRF DFU?` to inspect state and issue `NRF DFU START` over the trusted USB serial session separately. Browser upload and ESP relay hardware operation remain unverified; continue using the verified smartphone route for production updates until those tests complete.

## Failure and recovery

An interrupted legacy DFU must not be assumed to preserve a bootable prior application; Adafruit documents dual-bank support as disabled by default and the actual 0.6.0 nice!nano build configuration has not been read from the board. An application-only package should leave the MBR, SoftDevice and bootloader intact. If transfer fails and the device remains in DFU mode, retry the same verified ZIP from the phone. If it no longer advertises, use the board's double-reset UF2 mode and copy a known-good application UF2, or use the existing USB serial DFU path. SWD is the final recovery path if the bootloader itself is ever damaged, but this application-only update does not intentionally touch it.

Do not update the bootloader or SoftDevice over BLE during the first test. Do not use a package with a mismatched device type or SoftDevice requirement. The nRF legacy DFU path and ESP32 signed OTA remain separate release artifacts and separate trust models.
