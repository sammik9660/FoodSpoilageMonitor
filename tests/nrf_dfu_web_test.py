from pathlib import Path
import unittest


SOURCE = (Path(__file__).parents[1] / "firmware/esp32s3/gateway/gateway.ino").read_text(encoding="utf-8")


class NrfDfuWebFlowTest(unittest.TestCase):
    def test_serial_and_web_share_start_function(self):
        self.assertGreaterEqual(SOURCE.count("requestNrfDfuStart("), 3)
        self.assertIn('otaServer.on("/nrf-dfu/start",HTTP_POST,handleNrfDfuStart)', SOURCE)

    def test_pairing_requires_connected_identity_and_persists(self):
        self.assertIn("strcmp(sensorId,connectedSensorId)", SOURCE)
        self.assertIn('pairingPreferences.putString("target",sensorId)', SOURCE)

    def test_mutating_web_routes_retain_auth_and_origin_checks(self):
        for handler in ("handleNrfDfuPair", "handleNrfDfuStart", "handleNrfDfuCancel"):
            start = SOURCE.index(f"void {handler}()")
            body = SOURCE[start: SOURCE.index("\n}", start) + 2]
            self.assertIn('otaServer.authenticate("admin",otaPassword)', body)
            self.assertIn("otaSameOrigin()", body)

    def test_status_survives_browser_refresh_without_starting_dfu(self):
        self.assertIn('otaServer.on("/nrf-dfu/status",HTTP_GET,handleNrfDfuStatus)', SOURCE)
        self.assertIn("setInterval(status,1000)", SOURCE)
        upload = SOURCE[SOURCE.index("void handleNrfPackageUploadComplete"):
                        SOURCE.index("void handleNrfDfuStatus")]
        self.assertNotIn("requestNrfDfuStart", upload)


if __name__ == "__main__":
    unittest.main()
