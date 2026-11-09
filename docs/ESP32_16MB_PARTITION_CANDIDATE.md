# ESP32-S3 N16R8 partition candidate — not installed

`firmware/esp32s3/gateway/partitions.csv` remains the live build table. The
separate `partitions.16m-spiffs3m-ai5m.csv` is a migration candidate. A normal
app OTA updates an app slot, **not** the partition table or filesystem image.
Do not send this candidate through the local or internet OTA page.

| Region | Current offset / size | Candidate offset / size | Treatment |
|---|---:|---:|---|
| NVS | `0x9000` / 20 KiB | same | preserve Wi-Fi, ID and update keys |
| otadata | `0xE000` / 8 KiB | same | preserve selected slot and rollback data |
| app0 | `0x10000` / 3 MiB | same | dual OTA slot |
| old SPIFFS | `0x310000` / 896 KiB | same bytes, label `spiffs_legacy` | preserve as migration/rollback source |
| coredump | `0x3F0000` / 64 KiB | same | preserve |
| app1 | `0x400000` / 3 MiB | same | dual OTA slot |
| new SPIFFS | unallocated | `0x700000` / 3 MiB | spool and DFU ZIPs after migration |
| AI model | unallocated | `0xA00000` / 5 MiB | reserved custom data subtype `0x40`; no model loader yet |
| spare | unallocated | `0xF00000` / 1 MiB | future allocation |

The current candidate application BIN is 1,650,192 bytes (about 1.57 MiB).
Each 3 MiB OTA slot leaves about 1.43 MiB for future code, so shrinking either
slot is not justified now.
The model bytes would live in the dedicated data region, not the app image.
The original 896 KiB SPIFFS remains physically present in the candidate.
`Spool.begin()` explicitly mounts label `spiffs`, which selects the current
partition on old tables and the 3 MiB partition on the candidate. No automatic
format is enabled. A candidate boot **without a prepared new SPIFFS image will
fail to mount and must not be used for recording**. The gateway's partition
diagnostic also selects label `spiffs` to report the active region.

## Alternatives and decision

- Current table plus the conditional remote DFU overlap policy: no migration,
  and two typical ~177 KB ZIPs fit while STOPPED, drained and BLE-paused.
  The spool remains limited to 896 KiB.
- Expand the old SPIFFS in place: moves coredump and app1, invalidates the
  current app1 address in otadata, and may invalidate the existing SPIFFS
  layout. This has a larger rollback and data-loss surface.
- **Candidate chosen for preparation:** keep all existing offsets, reserve the
  old filesystem, add a new 3 MiB SPIFFS and 5 MiB model region in the 9 MiB
  free area. This keeps dual OTA slots and their rollback addresses stable.

## USB migration gate — user approval required before touching a device

1. End the experiment. Confirm a recent application ACK and `queue_depth=0`,
   `spool_depth=0`, `spool_error=false`; if not, stop. Save a CSV export as an
   independent record, but it does not replace a raw spool backup.
2. With the correct device/COM port identified and its serial monitor closed,
   use the ESP32-S3 ROM loader to **read** the whole 16 MiB flash to a private
   PC file. Also retain separate SHA-256 digests of the existing partition
   table (`0x8000`/`0x1000`), NVS (`0x9000`/`0x5000`), otadata
   (`0xE000`/`0x2000`) and old SPIFFS (`0x310000`/`0xE0000`). The full dump
   contains credentials; keep it outside Git and never paste it in a log.
   Reading flash requires a reboot, so it must wait until recording and RAM
   work have drained. Preserve the verified stable application backup.
3. Offline, list and unpack the old SPIFFS dump with the installed `mkspiffs`
   tool using 4096-byte blocks, 256-byte pages and the 917504-byte old image
   size. Verify the spool and DFU file names, counts, sizes and SHA-256. If
   extraction or CRC checks fail, stop; do not assume the filesystem is empty.
4. Create a **new** 3 MiB SPIFFS image from those verified files with the same
   block/page settings. Unpack the new image again and compare every file's
   SHA-256. This prepares the new region; it does not format or erase the old
   region on the device. The blank AI model region needs no model data yet.
5. Verify the candidate partition binary matches the CSV and determine the
   device's **currently selected** OTA app slot from the backed-up otadata or
   reliable boot diagnostics. Do not guess app0 versus app1. Prepare a newly
   signed, uniquely versioned application built against the candidate table.
6. Only after a separate flash approval, use USB to write the candidate table
   at `0x8000`, the new SPIFFS image at `0x700000`, and the new application
   to the verified current app-slot offset. Keep NVS, otadata, old SPIFFS,
   coredump and the inactive app-slot bytes untouched. Use no whole-flash
   erase. If the active slot cannot be proven or backups fail verification,
   stop rather than flashing. Test the exact command against the backups and
   device identity before execution.
7. On boot, verify new SPIFFS offset `0x700000`, mount success, file CRC/count,
   Wi-Fi/BLE/cloud ACK, backlog replay, OTA pending self-check and rollback
   confirmation. Keep the old region and raw backups until migration and a
   later power-cycle test succeed. A rollback to an old app may mount the old
   `spiffs_legacy` first, so do not resume recording during migration until
   the new app is confirmed. Never claim bytes recorded only on the new
   filesystem are visible to an old app.

The partition table and other data partitions are **not** updated atomically
by the existing signed app OTA. A USB interruption during table/image flashing
requires recovery from the raw backup and the stable firmware. No device
read, format, flash or migration was performed during candidate preparation.
