# nRF52840 application-only BLE DFU

## Confirmed compatibility boundary

The physical sensor is user-reported as a V1940 Pro Micro nRF52840 / nice!nano V2-compatible clone with Board-ID `nRF52840-nicenano`, Adafruit nRF52 Bootloader 0.6.0 and S140 6.1.1. The project builds it with Adafruit nRF52 Arduino core 1.7.0 using the `Adafruit Feather nRF52840 Express` target and `nrf52840_s140_v6.ld`. The nice!nano documentation identifies its bootloader as Adafruit's nRF52 bootloader, whose upstream board list includes `nice_nano` and whose bootloader implements BLE OTA entry through `GPREGRET=0xB1`.

The application now registers the core's `BLEDfu` service before NUS. A compatible phone writes the legacy buttonless start command, after which the application disconnects and jumps into the already-installed bootloader. This change does not write or replace the bootloader, MBR, SoftDevice, UICR or flash layout. Actual service discovery and a complete update on this clone remain hardware tests.

This is **Adafruit/Nordic legacy DFU**, not Nordic Secure DFU. The reported 0.6.0 bootloader and the default generated application package use CRC, device-type and SoftDevice compatibility checks but do not establish the same cryptographic authenticity as the independently signed ESP32 OTA path. Enabling signed nRF packages would first require verifying and deliberately provisioning a matching signed bootloader; that is outside this change and must not be attempted without a recoverable SWD setup and separate approval.

## Package creation

Compile `firmware/nrf52840/bme688_node` with Adafruit nRF52 core 1.7.0 and board `Adafruit Feather nRF52840 Express`, S140 6.1.1. Arduino CLI equivalent:

```powershell
arduino-cli compile --fqbn adafruit:nrf52:feather52840 `
  --output-dir data/build-2.0.5-nrf firmware/nrf52840/bme688_node
```

The core creates `bme688_node.ino.zip`; use the ZIP itself for BLE DFU. Its manifest must contain only the application image and must report device type `82` (`0x0052`) and SoftDevice requirement `182` (`0x00B6`). Do not substitute a UF2, ESP BIN, bootloader ZIP, or a package built for S140 v7. Preserve the generated BIN/DAT/manifest together.

The 2026-10-19 build produced `data/nrf-dfu-2.0.5/bme688-node-2.0.5-legacy-dfu.zip` (161,238 bytes). `SHA256SUMS.txt` beside it records the package checksum for transfer-integrity checking. The ZIP was inspected after creation: it contains only the 160,376-byte application BIN, 14-byte DAT and manifest; the BIN contains the `2.0.5` and `BLE DFU service` build strings. This checksum does not add firmware authenticity to the legacy bootloader protocol.

## Smartphone procedure

1. Stop the recording and let the ESP gateway acknowledge its existing queue/spool. Temporarily power off the ESP gateway or otherwise disconnect it from the sensor because the current peripheral configuration permits one central connection.
2. Keep the nRF powered from a stable source and place the generated DFU ZIP on the phone.
3. On Android, use Nordic Semiconductor's **nRF Device Firmware Update** app, which supports nRF5 SDK legacy and secure DFU ZIPs. Select the ZIP, then select the sensor named `BME688-<sensor-id>` and start the update. The application-side DFU service should disconnect and transfer control to the bootloader automatically. Keep the screen active and remain near the sensor.
4. If using Adafruit Bluefruit LE Connect, connect to the sensor, open **Updates**, choose custom firmware and select the generated package when the installed app accepts its format. Phone/app compatibility with this legacy package is not yet verified; the Nordic nRF DFU app is the primary Android path.
5. After completion, restore the ESP gateway. Confirm the nRF boot message contains `version=2.0.5`, then verify ESP BLE reconnection, 2-second ENV frames, the first gas scan after about 10 seconds, and the following scan about 600 seconds later. Check NUS framing and cloud ACK as before.

The first installation of this DFU-enabled application requires USB/UF2/serial upload because the previously installed application does not expose `BLEDfu`. Later application-only updates may use BLE after the first physical test succeeds.

## Failure and recovery

An interrupted legacy DFU must not be assumed to preserve a bootable prior application; Adafruit documents dual-bank support as disabled by default and the actual 0.6.0 nice!nano build configuration has not been read from the board. An application-only package should leave the MBR, SoftDevice and bootloader intact. If transfer fails and the device remains in DFU mode, retry the same verified ZIP from the phone. If it no longer advertises, use the board's double-reset UF2 mode and copy a known-good application UF2, or use the existing USB serial DFU path. SWD is the final recovery path if the bootloader itself is ever damaged, but this application-only update does not intentionally touch it.

Do not update the bootloader or SoftDevice over BLE during the first test. Do not use a package with a mismatched device type or SoftDevice requirement. The nRF legacy DFU path and ESP32 signed OTA remain separate release artifacts and separate trust models.
