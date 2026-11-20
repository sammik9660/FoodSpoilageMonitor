# Rule-based storage indicator — experimental UI, not a food-safety verdict

The measured temperature is air near the BME688, not the food core. The
`0..100` value is a **project display index**, not a probability or a score
published by a food-safety authority. A low score cannot certify food as safe.
GAS and humidity are never added to this score.

## Source thresholds (as reviewed 2026-11-16)

| Explicit food/preparation label | Refrigeration | Conservative duration used | Source |
| --- | --- | --- | --- |
| Raw chicken | 4°C or below | 24 h, lower end of official 1–2 days | [FoodSafety.gov cold chart](https://www.foodsafety.gov/food-safety-charts/cold-food-storage-charts) |
| Cooked chicken | 4°C or below | 72 h, lower end of official cooked poultry leftovers 3–4 days | Same chart |
| Hard-boiled egg | 4°C or below | 168 h (one week) | Same chart |
| Raw shell egg explicitly still in shell | 4°C or below | 504 h, lower end of 3–5 weeks | Same chart |
| Cooked rice | 4°C or below | 24 h after preparation/cooling | [UK FSA rice guidance](https://www.food.gov.uk/print/pdf/node/7611) |
| Cooked sprouts as leftovers | 4°C or below | 72 h, lower end of general cooked-leftover 3–4 days | [FoodSafety.gov leftovers](https://www.foodsafety.gov/food-safety-charts/cold-food-storage-charts) |
| Raw sprouts | Refrigeration required | **No specific day count entered** | [FDA Food Code](https://www.fda.gov/downloads/food/guidanceregulation/retailfoodprotection/foodcode/ucm595140.pdf) |
| Chicken/egg/sprouts without preparation state, tofu without packaging state, banana | Only applicable temperature indication, if known | **null** | No equivalent, verified product-specific period |

The FoodSafety.gov room-temperature two-hour rule informs the UI exposure
alert, but an air temperature just above 4°C is not equivalent to food being
at room temperature. This alert is a conservative monitoring prompt. It does
not instruct disposal. The cooked-rice 24-hour reference starts after cooking
and prompt cooling; an STO start later than cooking undercounts elapsed age.

## Project score conversion

- Duration component: 0 at STO start, 70 at the conservative source duration,
  rising to 100 at twice that duration. This is a visualization function.
- Temperature component: observed time with container-air T >4°C divided by
  two hours and capped at 100. The two-hour scaling is a project convention;
  it is **not** an official risk probability or a microbial growth equation.
- Overall score: rounded mean of duration and temperature components **only**
  if the product preparation has an applicable duration, a valid STO start,
  at least 80% of the period is covered by environmental observations,
  no adjacent observations are more than 10 minutes apart, and the latest
  valid ENV point is at most 10 minutes old. Otherwise `score=null` with
  explicit reasons. Known components remain visible.
- The official 4°C reference and cumulative >4°C exposure remain unchanged.
  A separate project air-sensor alert begins only after three distinct heater-off
  ENV samples over at least 30 continuous minutes above
  `4°C + STORAGE_ALERT_MARGIN_C_`. The default margin is 2°C, so the alert is
  above 6°C. This margin is a display/notification setting, not a revised food
  storage recommendation. The food-status card color follows the independent
  storage-progression stage; a temperature alert stays in its own row. Missing
  intervals remain unobserved, never counted as cold.

The engine reads a bounded window of the separate `StorageTelemetry` sheet
by exact device, sensor and STO session. It never modifies SensorData,
recording sessions, ACKs, spool, OTA/DFU, gas settings, or historical rows.
An `EXP-...` start alone cannot substitute for the food-placement `STO-...`
time. A past experiment cannot inherit the currently active STO session.
