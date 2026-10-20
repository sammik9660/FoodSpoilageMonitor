# V1940 battery ADC diagnostic

This diagnostic is separate from the production sensor firmware. It does not
modify the BME688 measurement, OLED, production BLE NUS/DFU services, or
gateway protocol. The diagnostic exposes its own BLE NUS instance solely so a
battery-powered board can report ADC observations without USB.

## Confirmed facts and limits

- The physical board is user-reported as a V1940 Pro Micro nRF52840 / nice!nano
  V2-compatible clone. The project compiles it as `Adafruit Feather nRF52840
  Express` with Adafruit nRF52 core 1.7.0 and S140 6.1.1.
- The production hardware uses Arduino D21/P0.31 for BME688 SDA and
  D20/P0.29 for SCL. The Feather variant's `PIN_VBAT` alias is D20/P0.29, so
  that alias must not be used on this board.
- The hardware diagnostic showed that P0.04 does not respond to battery removal
  on this V1940 board, so production firmware does not use it.
- P0.24 is not one of the nRF52840 SAADC-capable GPIOs. Firmware cannot obtain
  an analog voltage from that pin, so this diagnostic never reads it.
- The installed core configures `AR_INTERNAL` as a 0.6 V internal reference
  with gain 1/6. With 14-bit resolution the ADC-domain conversion is
  `input_mV = raw * 3600 / 16383`.
- User hardware validation with LiPo-only power measured VDDH as 4.09--4.10 V
  while a multimeter across B+/B- measured 4.09 V. USB power changes VDDH so it
  cannot be treated as battery voltage while VBUS is present. Production code
  therefore uses VDDH/5 only when the nRF USB supply status reports no VBUS.
- The BLE advertising name is `V1940-ADC-TEST`. Measurements continue every
  five seconds when no central is connected. When notifications are enabled,
  each newline-framed message is split into at most 20-byte NUS writes.

## Files and build

Sketch:

`firmware/nrf52840/battery_adc_diagnostic/battery_adc_diagnostic.ino`

Arduino IDE settings:

1. Board package: Adafruit nRF52 core 1.7.0.
2. Board: Adafruit Feather nRF52840 Express.
3. SoftDevice: S140 6.1.1.
4. Open the diagnostic sketch and use **Verify/Compile** first.

Equivalent CLI compile command:

```powershell
arduino-cli compile --fqbn adafruit:nrf52:feather52840 firmware/nrf52840/battery_adc_diagnostic
```

Compilation only verifies the API and board target. The battery-only VDDH
agreement above is user hardware evidence; long-run accuracy over discharge,
load and temperature remains unverified.

## Hardware test after the current experiment

1. Preserve the known-good production firmware and the existing recovery ZIP
   `data/nrf-dfu-2.0.6/bme688-node-2.0.6-legacy-dfu.zip`.
2. Stop the experiment and ensure all ESP queue/spool data has received an
   application ACK. Power down the ESP gateway so it does not try to reconnect
   to the diagnostic firmware.
3. Record the V1940 PCB markings and take clear photographs of both sides.
4. Upload this diagnostic over the existing USB serial bootloader. Do not use
   BLE DFU for the diagnostic and do not erase the SoftDevice or bootloader.
5. Open Serial Monitor at 115200 baud. Record at least ten lines with the LiPo
   disconnected, then with the LiPo connected while USB remains attached.
6. In nRF Connect, connect to `V1940-ADC-TEST`, open Nordic UART Service and
   enable notifications on its TX characteristic. Confirm the two diagnostic
   lines arrive about every five seconds.
7. Disconnect USB and power-cycle the board from the LiPo only. Reconnect from
   nRF Connect and record at least ten notification pairs.
8. Measure the LiPo directly at B+/B- with a multimeter at the same time. Do
   not inject voltage into P0.04, P0.24, or any exposed pin.
9. Compare only stable raw and ADC-input values. Do not infer a divider ratio
   from one reading. Repeat at a second known battery voltage after normal use.
10. Compare the USB-powered and battery-only readings. USB may alter VDDH or
    charger state, so only simultaneous multimeter measurements can establish
    whether the battery-only VDDH estimate tracks B+/B-.

Expected output shape:

```text
P0.04/AIN2 raw=... adc_input_mv=...
VDDH/5 raw=... adc_input_mv=... source_estimate_mv=...
```

BLE notification lines use compact names:

```text
P004,R=...,MV=...
V5,R=...,MV=...,VDDH=...
```

`MV` is the ADC input in millivolts. `VDDH` is the internal VDDH/5 input
multiplied by five. It matched the LiPo only in the battery-only test above;
USB-powered values are not battery readings.

If P0.04 is floating, unstable, fixed near zero, or unrelated to the measured
LiPo voltage, do not integrate it. If VDDH follows USB instead of B+/B-, do not
use it as a battery measurement.

## Restore the production firmware

After collecting the diagnostic data, use Arduino IDE with the same Feather
nRF52840/S140 6.1.1 target to upload the current production
`firmware/nrf52840/bme688_node/bme688_node.ino`. If USB serial upload fails,
double-reset into the existing UF2 bootloader and use the established USB
recovery procedure. Do not replace the bootloader or SoftDevice. The preserved
Legacy DFU ZIP remains a known recovery application package and must not be
overwritten by this diagnostic.

## Production integration

The production node samples VDDH/5 every 60 seconds and adds optional
`BV`, `BOK`, and `USB` fields to `STAT`. `BV` is nonzero only when VBUS is absent
and the result is within the accepted electrical range. The gateway accepts
both the old STAT line and the suffix, then forwards it in health heartbeat.
No sample UID, ACK, 120-byte SPIFFS record, experiment row, or DFU format is
changed. Update the gateway before the sensor so a deployed old parser never
sees the new suffix. Percentage, capacity and charge-complete inference remain
out of scope.
