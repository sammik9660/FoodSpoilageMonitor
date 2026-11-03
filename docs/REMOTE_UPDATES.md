# Internet remote ESP32 OTA and nRF Legacy DFU

## Scope and bootstrap

The dashboard can queue an update for a selected device on another network. The ESP32 makes outbound HTTPS connections to Apps Script and an artifact store; the browser never contacts the device's private IP. No VPN, public device IP or router forwarding is required. Existing local OTA/DFU at port 8080 remains available for on-site recovery.

**Existing firmware has no remote-command receiver. One initial installation of the remote-capable firmware is required using the existing local OTA (or manually approved USB recovery).** A firmware that has no download receiver cannot be remotely taught that receiver just by changing Apps Script. Device command credentials are then provisioned once through the authenticated local `/remote-config` page. Subsequent updates keep those NVS credentials and can run from the internet.

This implementation has host tests and a separate firmware build record in `PROJECT_STATE.md`. It has not been deployed, has not used live Sheets, and has not performed a real internet OTA/DFU. Download/boot/DFU success on physical hardware is not established by a host test.

## Data flow

1. An operator enters a separately provisioned remote management token in the existing Apps Script dashboard, selects a registered package and a device, and explicitly approves it.
2. Under the existing script lock, Apps Script verifies STOPPED, writes a durable per-device command/reservation, and allocates its next sequence. Start Recording cannot bypass that reservation, including after cache eviction.
3. Every 30 seconds, an ordinary existing data/health POST can include a signed remote request. A command is added to its normal response. Sample ACK contents and release rules are unchanged. There is no idle extra HTTP poll.
4. The ESP verifies device ID, request nonce, HMAC-SHA256, expiry, URL allowlist and size. It persists the sequence/command in a new `fsm-remote` NVS namespace **before** download or flash side effects. Reboot does not replay that command.
5. Existing PREPARE pauses BLE ingress and drains RAM/spool/HTTP work. The existing cloud STOPPED lease is acquired. Download uses strict TLS with the installed core's built-in CA bundle, the original hostname/SNI and the project's thread-safe DNS workaround.
6. Each bounded range request downloads at most 128 KiB with a 4 KiB RAM buffer. Content-Length and Content-Range must match exactly. Redirects, HTTP downgrade, query-bearing URLs and compressed/chunked bodies are rejected. SHA-256 is computed over the complete file.
7. ESP: download and authenticate the unchanged FSGWOTA1 manifest; confirm its version/build/hash match the command; reuse the local OTA begin/write/seal/commit engine. Persist VERIFYING before selecting the new image. SUCCESS requires the new firmware identity and the existing boot self-check, BLE sample, SPIFFS, Wi-Fi and cloud ACK.
8. nRF: stream ZIP to the existing temporary file, preserving spool reserve and any old ZIP. Reuse ZIP/CRC/DAT validation and package promotion, then the existing explicitly paired Legacy DFU engine (20-byte payload, PRN 8). SUCCESS additionally requires reconnection to the same sensor, the expected firmware version and fresh sensor data.
9. Signed progress/result reports are piggybacked on ordinary traffic or use the established cloud POST/redirect path during maintenance. TLS download and status requests never overlap. The browser's existing dashboard poll displays the last authenticated report; file receipt alone is not success.

## Required settings — not created automatically

Three independent credentials have different roles. None belongs in Git, an OTA BIN, the catalog URL or a screenshot/log:

| Credential | Where it is configured | Purpose |
|---|---|---|
| Remote operator token, 64 hex characters | Operator password manager; only its SHA-256 digest in Script Property `REMOTE_OPERATOR_SHA256` | Authorizes catalog access and update approvals over `google.script.run` |
| Per-device command key, 64 hex characters | Script Property `REMOTE_KEY_<12-hex-device-id>` and that device's `/remote-config` form | HMAC authenticates commands and progress; each device uses a different key |
| Optional storage bearer token | HTTPS store secret and `/remote-config` download-token field | Reads private artifact objects; never sent to Apps Script or the browser catalog |

Use independently generated cryptographically random values, copied with exact case. The agent has not generated real credentials. The operator token's SHA-256 is calculated over its UTF-8 text, not decoded hex bytes. A local PowerShell 7 command the operator may run is:

```powershell
$remoteOperatorToken = Read-Host -MaskInput 'Existing remote operator token'
[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData(
  [Text.Encoding]::UTF8.GetBytes($remoteOperatorToken))).ToLowerInvariant()
Remove-Variable remoteOperatorToken
```

The displayed digest goes into `REMOTE_OPERATOR_SHA256`. Device keys use HMAC with their exact ASCII hex text. Do not use the operator token as a device key. Token rotation requires coordinated settings; the device refuses silently replacing an existing command key. A configured download bearer token requires exactly one allowlisted host, so it cannot be forwarded to a second host.

Other Script Properties:

- `REMOTE_DOWNLOAD_HOSTS`: exact lowercase hostnames separated by commas.
- `REMOTE_UPDATE_CATALOG`: JSON array containing up to eight release entries produced by `tools/prepare_remote_release.ps1`.
- Keep the existing `SPREADSHEET_ID`, deployment URL and all existing properties/data.
- `REMOTE_JOB_<device>` is managed by the code: command counter, approval replay history, reservation and latest result. Back up these properties; never reset counters independently of devices.

The management token is kept only in the password input of the current page and sent over Google's HTTPS RPC. It is not put in localStorage, URLs or logs. This avoids relying on `Session.getActiveUser().getEmail()`, which may be blank for an execute-as-owner web app. See [Google Session documentation](https://developers.google.com/apps-script/reference/base/session). Existing legacy telemetry auth is not redesigned; only authenticated remote reports can update remote results and capability status.

## External HTTPS storage

Use an immutable path per release and keep the original bytes. Requirements: HTTPS with a certificate trusted by the bundled roots, GET Range/206, exact Content-Length/Content-Range, no redirect or transparent compression. Signed-query URLs are deliberately unsupported; an optional Authorization bearer header supports private download endpoints. A public read-only bucket is technically possible, but existing firmware binaries may contain project constants, so private download access is preferable.

An optional minimal private implementation is `tools/remote_storage_worker.mjs`:

- Create an R2 bucket manually and bind it to a Cloudflare Worker as `RELEASES`.
- Add a separately chosen 64-hex secret as `DOWNLOAD_TOKEN` using the provider's secret settings.
- Deploy that source manually after review. It exposes only authenticated GET/HEAD of `releases/...`; it cannot write or delete objects.
- Upload the existing BIN/manifest or application-only DFU ZIP into an immutable `releases/<release-id>/` folder. Do not overwrite an object already approved for installation.
- Put the Worker hostname in both allowlists and its bearer token in each device's local configuration.

No Cloudflare account, bucket, Worker, credential or deployment was created here. The adapter uses the documented [R2 range API](https://developers.cloudflare.com/r2/api/workers/workers-api-reference/). An existing server satisfying the same HTTPS contract can be used instead without changing firmware.

Prepare catalog entries locally (commands do not upload files):

```powershell
pwsh -File tools/prepare_remote_release.ps1 -Kind esp32 `
  -PackagePath dist/ota-<version>-<build> -ReleaseId esp-<version>-<build> `
  -BaseUrl https://<storage-host>/releases/esp-<version>-<build>

pwsh -File tools/prepare_remote_release.ps1 -Kind nrf `
  -PackagePath data/nrf-dfu-<version>/bme688-node-<version>-legacy-dfu.zip `
  -Version <version> -ReleaseId nrf-<version> `
  -BaseUrl https://<storage-host>/releases/nrf-<version>
```

The ESP catalog builder reuses the existing public-key signature/image checker; the nRF builder reuses the existing Legacy DFU verifier. No new signing key or package format is introduced. nRF legacy packages themselves are not signed; the authenticated command pins the complete ZIP SHA-256, while the existing DFU engine validates ZIP CRC, init packet and application-only target requirements.

ESP targets without remote-protocol markers require `-AllowLegacyTarget`. Such a downgrade may boot correctly but lose remote control and result reporting. Treat its result as UNCONFIRMED unless independently verified; do not assume it can be upgraded again remotely. Normal remote deployments should target remote-capable builds.

## Deployment sequence and use

1. Review and back up the existing source, packages and Script Properties. Existing Sheets schemas are unchanged.
2. Update the **same** Apps Script deployment with `Code.gs`, `Dashboard.html` and **new `RemoteUpdates.gs`**. Add the properties above; do not create a different spreadsheet or reset data.
3. Install the first remote-capable ESP build using the currently working local OTA path. The agent does not perform installation.
4. On a trusted LAN, open `http://<device-ip>:8080/remote-config`, authenticate with the existing local OTA token, and set the matching device key, allowed hostnames and optional storage token. No key is returned by this endpoint. Preserve existing pairing; nRF remote DFU will not automatically pair a different sensor.
5. Wait for an authenticated remote heartbeat in the cloud dashboard. Old firmware or mismatched keys cannot enable the remote approval button.
6. Stop the selected experiment. Load the catalog, select ESP32 or nRF release, review device/version/size/hash, and approve. ESP downgrade/build rollback requires the existing explicit approval checkbox. A browser refresh does not restart the command.
7. Observe PREPARING → DOWNLOADING → VERIFYING (ESP boot) or UPDATING/RECONNECTING (nRF) → SUCCESS/FAILED. `updated_at` is the last report, not a claim of continuous connectivity.

Changing Apps Script alone cannot enable an already deployed old gateway to pull commands. A nearby person must perform the one-time bootstrap, unless a previously configured network path already reaches its local updater.

## Failure and data guarantees

- Expiry is checked before starting the destructive stage; total execution is bounded to 20 minutes. Commands expire after ten minutes, and the server conservatively reserves recording for the expiry plus execution window if no authenticated result arrives. Afterwards the result is UNCONFIRMED, never fabricated SUCCESS.
- Failed downloads are not automatically restarted in a loop. A user can approve a fresh command after the failure is reported. A command sequence already consumed in NVS cannot run twice after retry/reboot.
- Interrupted ESP writes do not select the partial slot. Signed manifest, exact received size, both SHA checks and the existing image validation must all succeed before `Update.end(true)`.
- nRF interruptions may leave the sensor in its bootloader; use the existing phone Legacy DFU or USB recovery procedure. The gateway cannot guarantee atomic rollback of an Adafruit Legacy DFU application; it does not rewrite the bootloader/SoftDevice.
- Old ZIP and measurement files are preserved until validated promotion. The 896 KiB SPIFFS must fit the previous ZIP, new temporary ZIP, reserved spool budget and headroom simultaneously; a larger ZIP can be refused even with an empty spool. This implementation does not delete the old package or reduce spool reserve to force it through. No partition, spool record format, UID, ACK or automatic-format policy changes.
- Persistent NVS corruption/write failure is fail-closed. If authenticated result reporting cannot continue, the cloud eventually shows UNCONFIRMED rather than inventing a success.
- First boot still uses the existing BLE sample + cloud ACK self-check. An absent sensor can keep ESP boot verification pending even with working internet; the dashboard must not claim success in that state.
- The current 120-byte sample timestamps still follow the existing capture-time behavior; this feature does not migrate or rewrite data.
- Local OTA and remote operations are mutually exclusive. Local requests cannot take over a remotely owned writer or remove its temporary ZIP.

Useful reported errors include `TARGET_NOT_PAIRED`, `PREPARATION_STOPPED`, `COMMAND_EXPIRED`, `CLOUD_STOPPED_UNCONFIRMED`, `SPOOL_RESERVE_SPACE`, `MANIFEST_HASH`, `SIGNED_MANIFEST_REJECTED`, `IMAGE_HASH`, `IMAGE_COMMIT`, `DFU_PACKAGE_INVALID`, `DFU_VERSION_MISMATCH`, `BOOT_INTERRUPTED`, `NVS_WRITE` and `UPDATE_TIMEOUT`. An authentication mismatch has no trusted device result: the page stays at authenticated-heartbeat waiting rather than showing a forged error.

## Load and verification

Normal sensor health remains five seconds, and the dashboard timer remains five seconds. Remote command processing is piggybacked at most once per 30 seconds in idle operation. Active updates report at most once per 15 seconds between bounded downloads; a slow range may delay the next report. Each gateway uses one HTTP operation at a time. No cloud-side sleeps, recursive polling or whole firmware downloads run in Apps Script. New control locks use `tryLock(250)`; failed remote processing does not discard an already committed sample ACK.

Dashboard device-list requests cannot overlap; list/device RPC failures back off up to 60 seconds. Remote state display reuses the normal dashboard response. Catalog requests happen only on the operator's button click. Cache eviction never removes the durable reservation: Start Recording checks the property under the existing lock.

Google currently documents simultaneous-execution limits of 30 per user and 1,000 per script; quotas are account-dependent and may change. See [official quotas](https://developers.google.com/apps-script/guides/services/quotas). Keeping existing five-second telemetry means baseline request volume still exists; this is not a production load-test claim. ContentService's [response redirect](https://developers.google.com/apps-script/guides/content) is reused, with HTTPS and Google-host checks for remotely configured devices.

Local tests use virtual devices and stubbed services only:

- `node tests/cloud.test.cjs`
- `node tests/remote_updates.test.cjs` (also writes deterministic, non-production interop vectors under ignored `data/remote-test-vectors`)
- `node tests/remote_storage_test.mjs`
- MSVC C++17 `tests/remote_client_test.cpp` and `tests/remote_runtime_test.cpp`, include `tests/remote_stubs`, write OBJ/EXE into `data/`.
- Existing parser, spool, local OTA package/partition and Legacy DFU protocol tests.

Still requires real deployment validation: TLS chain/time on the actual device, host Range behavior, mobile/external-network dashboard approval, power-loss/reboot during both update paths, real boot rollback, final nRF DIS version plus new data, and two physical devices. No production endpoint was exercised by the host suite.
