# Local alert policy candidate — not deployed

The 4°C refrigeration benchmark comes from [FoodSafety.gov's cold-storage
chart](https://www.foodsafety.gov/food-safety-charts/cold-food-storage-charts).
The 30-minute sustained excursion, 5-minute recovery hold, 6-hour
summary cadence, and 0–100 display score are **project policies**, not
regulatory safety cutoffs or spoilage probabilities. BME688 ENV is air near
the container, not measured food-core temperature.

## Temperature and data provenance

`StorageRisk.gs` reads `StorageTelemetry` rows created from accepted heater-off
ENV health only. It never substitutes GAS heater temperatures, and keeps the
previous observed-hours and above-4°C cumulative totals unchanged. A separate
project alert uses the one-line `STORAGE_ALERT_MARGIN_C_=2` setting, giving an
active alert above 6°C after at least three distinct ENV observations over
30 continuous minutes, with no gap over 10 minutes. `MonitoringPolicy.gs` uses
the same margin for email incidents. The 4°C benchmark is not changed; the 6°C
air-sensor alert is not a food-safety standard. A current cool value returns
the warning to historical exposure text while leaving the cumulative total
intact. Missing data is `UNKNOWN`, not normal. Main card color is reserved for
the separate food-progression stage, not temperature alert state.

`MonitoringPolicy.gs` derives a separate temperature incident from new ENV
capture times in authenticated health snapshots. The monitor is opt-in and
requires an ACTIVE STO plus an explicitly refrigeration-applicable food label.
It does not depend on a browser viewer. It groups BLE, GAS and ENV progress
faults into one `SENSOR_PATH_STALLED` incident; spool and cloud faults remain
separate. `NORMAL → SUSPECTED → ACTIVE → RECOVERING → RECOVERED` uses 60 seconds
for first notification and five minutes of observed recovery before one
recovery notice. An active incident gets a summary at most every six hours.
Two continuous hours of measured warm exposure can issue a distinct urgent
notice once per temperature incident.

Only `MailApp.sendEmail` is implemented in this repository. There is no SMS
provider call in the inspected source. External carrier email-to-SMS or
another deployed project cannot be audited from this checkout.

## Delivery boundary

The script lock protects incident and outbox state, then releases before
MailApp network I/O. An event is marked CLAIMED durably before sending.
Synchronous send failure returns it to PENDING for bounded retry. A process
crash after MailApp accepted the message but before success was recorded
cannot be resolved exactly once by Apps Script: automatic duplicate delivery
is suppressed and the claim remains visible as `delivery_uncertain` for
manual review. Never silently call this state DELIVERED or RECOVERED.

Replay of 854 real Device 1 StorageTelemetry points from the supplied XLSX
found 850 old-rule warning snapshots versus 485 sustained-rule snapshots.
These are UI eligibility counts, **not historical sent emails**; the workbook
does not contain MailApp delivery history. A deterministic synthetic sensor
fault replay produced three old derived alerts versus one grouped new alert.
No operating trigger, Script Property, Sheets row, device or firmware was
changed during this local work.
