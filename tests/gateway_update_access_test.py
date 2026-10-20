from pathlib import Path
import unittest


ROOT = Path(__file__).parents[1]
SOURCE = (ROOT / "firmware/esp32s3/gateway/gateway.ino").read_text(encoding="utf-8")
DASHBOARD = (ROOT / "cloud/apps_script/Dashboard.html").read_text(encoding="utf-8")


class GatewayUpdateAccessTest(unittest.TestCase):
    def test_esp_ota_and_nrf_dfu_are_mutually_exclusive(self):
        ota_safe = SOURCE[SOURCE.index("bool otaSafeToStart()"):
                          SOURCE.index("bool otaActivateIfDrained()")]
        self.assertIn("nrfDfuMode==NRF_DFU_NORMAL", ota_safe)
        nrf_start = SOURCE[SOURCE.index("bool requestNrfDfuStart"):
                           SOURCE.index("bool requestNrfDfuCancel")]
        self.assertIn("otaMode!=OTA_NORMAL", nrf_start)

    def test_heartbeat_still_blocks_concurrent_io_but_not_led_warning(self):
        self.assertIn("otaHttpBusy=busy", SOURCE)
        self.assertIn("busy&&!activeJobHeartbeat", SOURCE)
        self.assertIn("activeJobHeartbeat=true", SOURCE)
        self.assertIn("pixelState(ble,wifi,cloud,backlog,updateActive)", SOURCE)

    def test_cloud_dashboard_links_to_real_local_handlers(self):
        self.assertIn("espOtaLink.href=local+'/'", DASHBOARD)
        self.assertIn("nrfDfuLink.href=local+'/nrf-dfu'", DASHBOARD)
        self.assertIn('otaServer.on("/",HTTP_GET,handleOtaPage)', SOURCE)
        self.assertIn('otaServer.on("/nrf-dfu",HTTP_GET,handleNrfPackagePage)', SOURCE)

    def test_default_console_is_not_mirrored(self):
        self.assertIn("constexpr bool UART_DIAGNOSTICS=false", SOURCE)


if __name__ == "__main__":
    unittest.main()
