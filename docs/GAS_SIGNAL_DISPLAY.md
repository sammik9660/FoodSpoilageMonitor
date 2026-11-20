# Experimental GAS change display

The installed ESP32 v2.0.29 already computes `gas_anomaly.index` from each valid complete 10-step scan after six baseline scans for the same sensor and STO storage session. This is a baseline-relative gas-pattern anomaly, **not a trained spoilage classifier**. The only observed deterioration endpoints in `data/edge-ai-package-v2/spoilage_observations_20261110.json` are ambient banana and rice. Their onset times are unknown and there is no verified negative per-scan class. Thus no measured spoilage sensitivity, specificity, or probability exists.

The dashboard uses historical early post-baseline scores only for a **visual change scale**. From the unchanged `data/gas-ai-20261116/scans.json`, 127 quality-accepted scans within two hours after the sixth scan of their exposure yielded median 0.845, 75th percentile 1.120, and 95th percentile 4.199. These observations are not independent food-safety labels. Display cutoffs are rounded to 1.12 and 4.20:

- Below 1.12: neutral, small measured pattern change.
- 1.12 to below 4.20: yellow, measured pattern change.
- 4.20 or above: red, large measured pattern change.

The cutoffs do **not** diagnose spoilage. A score of 1.87 is yellow in this experimental display. The dashboard only uses a recent inference from the current sensor and active STO session, with a recent cloud contact; old, mismatched or missing results are neutral and explicitly unavailable. The food-name candidate, storage-condition rules, and unvalidated spoilage prediction remain separate.

This display has passed local mock tests but has not been deployed or visually checked in the operating dashboard. Field review should record each actual food, GAS score, color, time, and independently observed condition, including false alerts and missed changes. Its colors must not trigger food-safety notifications until validated.
