from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GATEWAY = (ROOT / "firmware/esp32s3/gateway/gateway.ino").read_text(encoding="utf-8")
POLICY = (ROOT / "firmware/esp32s3/gateway/NrfDfuPackagePolicy.h").read_text(encoding="utf-8")


def temporary_capacity(total: int, used: int, reserve: int, headroom: int = 8192) -> int:
    if used >= total or total - used <= reserve + headroom:
        return 0
    return min(total - used - reserve - headroom, 815104 + 16384)


assert temporary_capacity(917504, 162102, 550502) >= 162102
assert temporary_capacity(917504, 400000, 550502) == 0

required = [
    'FINAL_PATH[] = "/nrf-dfu.zip"',
    'TEMP_PATH[] = "/nrf-dfu.upload.tmp"',
    'BACKUP_PATH[] = "/nrf-dfu.backup"',
    'otaServer.authenticate("admin",otaPassword)',
    "otaSameOrigin()",
    "nrfPackageUploadSafe()",
    "temporaryCapacity(SPIFFS.totalBytes(),SPIFFS.usedBytes(),spool.budget)",
    "validateNrfPackageAt(nrf_dfu_package::TEMP_PATH",
    "promoteNrfPackage(error,sizeof(error))",
    'failNrfPackageUpload("upload timed out",408)',
    'otaServer.on("/nrf-dfu/upload",HTTP_POST',
    "DFU was not started; use the web Start update button or NRF DFU START explicitly.",
]
combined = POLICY + GATEWAY
for needle in required:
    assert needle in combined, needle

upload_handler = GATEWAY[GATEWAY.index("void handleNrfPackageUploadProgress()") :]
upload_handler = upload_handler[: upload_handler.index("void startOtaWebServer()")]
assert "nrfDfuRelay.run(" not in upload_handler
assert "SPIFFS.format" not in upload_handler
assert "SPIFFS.begin(true" not in upload_handler

print("nrf dfu upload policy tests passed")
