# V1940 battery estimate

## Evidence and limits

The user measured 4.09 V at LiPo B+/B- while the battery-only internal VDDH/5 result was 4.09--4.10 V. P0.04 did not respond to battery removal. Production therefore uses VDDH/5 every 60 seconds and rejects it as battery voltage whenever the SoftDevice reports USB VBUS. The assumed 4.2 V charge voltage and 3.5 V display floor have not been verified from the cell datasheet. Percentage is an estimate, never measured mAh, exact SOC, health, or a charge-complete indication.

## Estimator

`Battery.h` keeps the manually established nonlinear 3500--4200 mV curve. A five-sample median prevents one heater/load dip from immediately changing the displayed percentage. Min/max include valid battery-only observations, while the learned residence histogram accepts only heater-off ENV intervals of 30--120 seconds. GAS, COOL, REC, USB and invalid measurements cannot train the model.

The state is `DEFAULT` for the first valid samples, `COLLECTING` after six samples, and `INSUFFICIENT_DATA` after roughly 24 hours of samples if the calibration criteria are still absent. `CALIBRATED` requires at least eight hours of stable ENV history, observations near both fixed ends (max >= 4.10 V, min <= 3.65 V), at least five populated 100 mV bands, and a mostly falling trajectory. Observed extrema never redefine 100% or 0%. The residence estimate is blended with the fixed curve and limited to +/-10 percentage points. This is a limited use-history correction, not a learned capacity curve.

## Persistence

The Adafruit nRF52 1.7.0 Feather target reserves the existing 28 KiB InternalFS at `0xED000`; Bluefruit already mounts it for bonding. Battery code does not call the BSP's auto-formatting `InternalFS.begin()`. If the existing filesystem is unavailable, persistence is disabled and sensing continues with the default curve.

Two CRC/version/generation slots (`/battery-a.bin`, `/battery-b.bin`) alternate so one verified slot remains after an interrupted write. The first save occurs after six valid samples. Later writes require a state transition, a >=50 mV new extremum, or six additional hours of stable history, and are rate-limited to once per six hours. A corrupt or incompatible record is ignored and the default curve remains usable. Actual power-cut recovery and flash endurance remain hardware validation items.

## Transport and deployment order

The existing `BV`, `BOK`, `USB` STAT fields remain. Optional `BP`, `BS`, `MN`, `MX` carry the nRF-computed percentage, calibration state code, and observed extrema. The worst-case STAT line is 169 bytes, below the gateway's 200-byte newline frame limit, and NUS still writes 20-byte chunks. Old sensor STAT messages are accepted.

Deploy Apps Script and the gateway parser before the new nRF firmware. The dashboard displays the sensor's percentage; it does not calculate another voltage table. USB displays `BAT USB`, and old/missing battery telemetry displays `BAT --`. Battery telemetry stays in `LIVE_V2`; it does not change SensorData, EnvironmentHistory, UID/ACK, or SPIFFS record layouts. Historical battery graphs are not implemented because no separate battery-history schema has been validated.

## Remaining hardware checks

1. Confirm `BAT state new/restored/saved` without a format or boot failure.
2. Compare OLED and dashboard voltage/percentage while battery-only.
3. Connect USB and confirm both show USB rather than a percentage.
4. Run a normal gas scan and confirm a transient dip does not immediately change percentage.
5. Reboot after a successful save and confirm generation/min/max restoration.

