#include <bluefruit.h>
#include <Wire.h>
#include <Adafruit_BME680.h>
#include <math.h>

constexpr uint8_t BME_ADDRESS = 0x76;
constexpr uint32_t SAMPLE_INTERVAL_MS = 2000;
constexpr uint32_t FAILURE_THRESHOLD = 3;
constexpr uint32_t RECOVERY_MIN_MS = 5000;
constexpr uint32_t RECOVERY_MAX_MS = 60000;
Adafruit_BME680 bme(&Wire);  // begin(address, bool initSettings), NOT a Wire pointer.
BLEUart bleuart;
uint32_t bootId, validSequence = 0, failures = 0, consecutiveFailures = 0;
uint32_t recoveryAttempts = 0, recoveries = 0, txErrors = 0;
uint32_t nextMeasurement = 0, nextRecovery = 0, lastHealth = 0;
uint32_t recoveryDelay = RECOVERY_MIN_MS;
bool sensorReady = false, recoveryPending = false, lastReadOK = false;

bool sendLine(const char* line) {
  if (!Bluefruit.connected() || !bleuart.notifyEnabled()) return false;
  // A delimiter before every frame also terminates a previous interrupted TX.
  if (bleuart.write((const uint8_t*)"\n", 1) != 1) { ++txErrors; return false; }
  const size_t len = strlen(line);
  for (size_t i = 0; i < len; i += 20) {
    size_t n = min((size_t)20, len - i);
    if (bleuart.write((const uint8_t*)line + i, n) != n) {
      ++txErrors;
      Serial.println("ERROR BLE partial TX; sample not retried, gap remains visible");
      return false;
    }
    delay(5);
  }
  if (bleuart.write((const uint8_t*)"\n", 1) != 1) { ++txErrors; return false; }
  return true;
}

void sendHealth() {
  char line[160];
  snprintf(line, sizeof(line), "STAT,BID=%08lX,MS=%lu,FAIL=%lu,REC=%lu,TRY=%lu,TXERR=%lu,OK=%u",
           (unsigned long)bootId, (unsigned long)millis(), (unsigned long)failures,
           (unsigned long)recoveries, (unsigned long)recoveryAttempts,
           (unsigned long)txErrors, lastReadOK ? 1 : 0);
  sendLine(line);
}

bool initializeSensor() {
  // Verified Feather variant: Arduino D21 -> P0.31 SDA; D20 -> P0.29 SCL.
  Wire.end();
  Wire.setPins(21, 20);
  Wire.begin();
  Wire.beginTransmission(BME_ADDRESS);
  if (Wire.endTransmission() != 0) return false;
  Wire.beginTransmission(BME_ADDRESS);
  Wire.write(0xE0); Wire.write(0xB6);  // BME68x soft reset, verified diagnostic sequence.
  if (Wire.endTransmission() != 0) return false;
  delay(20);
  return bme.begin(BME_ADDRESS, false) &&
         bme.setTemperatureOversampling(BME680_OS_8X) &&
         bme.setHumidityOversampling(BME680_OS_2X) &&
         bme.setPressureOversampling(BME680_OS_4X) &&
         bme.setIIRFilterSize(BME680_FILTER_SIZE_3) &&
         bme.setGasHeater(320, 150);
}

void recoverSensor() {
  // DATA INTEGRITY: persistent read failures occurred on 2026-09-28 and reset/
  // reinitialization restored operation. Keep recovery AND backoff, even if
  // ordinary tests pass. A successful begin is not yet a successful recovery.
  ++recoveryAttempts;
  sensorReady = initializeSensor();
  recoveryPending = true;
  lastReadOK = false;
  nextRecovery = millis() + recoveryDelay;
  recoveryDelay = min(recoveryDelay * 2, RECOVERY_MAX_MS);
  Serial.print("RECOVERY init="); Serial.print(sensorReady);
  Serial.print(" attempts="); Serial.println(recoveryAttempts);
  nextMeasurement = millis();
}

void setup() {
  Serial.begin(115200);
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName("BME688-NRF");
  Bluefruit.autoConnLed(false);
  bootId = NRF_FICR->DEVICEID[0] ^ micros();
  // SoftDevice RNG supplies a boot discriminator; gateway UID is cloud identity.
  for (uint8_t i = 0; i < 20; ++i) {
    if (sd_rand_application_vector_get((uint8_t*)&bootId, sizeof(bootId)) == NRF_SUCCESS) break;
    delay(5);
  }
  bleuart.begin();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
  Serial.println("BOOT reliability-v2; 0x76; D21/D20; heater 320C/150ms");
  recoverSensor();
}

void loop() {
  const uint32_t now = millis();
  if (now - lastHealth >= 10000) { lastHealth = now; sendHealth(); }
  if (!sensorReady) {
    if ((int32_t)(now - nextRecovery) >= 0) recoverSensor();
    delay(10);
    return;
  }
  if ((int32_t)(now - nextMeasurement) < 0) { delay(5); return; }
  nextMeasurement = now + SAMPLE_INTERVAL_MS;  // no catch-up burst after a delay
  const bool readOK = bme.performReading();
  const bool finiteValues = readOK && isfinite(bme.temperature) && isfinite(bme.humidity) &&
                            isfinite(bme.pressure) && isfinite((double)bme.gas_resistance);
  if (!finiteValues) {
    // DATA INTEGRITY: failed reads leave previous T/H/P/G in memory. The real
    // 2026-09-28 incident produced stale rows. A failed read is a missing sample:
    // no sensor frame, no valid-sequence increment, only diagnostics/recovery.
    ++failures; ++consecutiveFailures; lastReadOK = false;
    Serial.print("SENSOR ERROR missing sample; failures="); Serial.println(failures);
    if (consecutiveFailures >= FAILURE_THRESHOLD) {
      sensorReady = false;
      // nextRecovery was set by the previous attempt; never reset every 2 s.
      if ((int32_t)(millis() - nextRecovery) >= 0) recoverSensor();
    }
    sendHealth();
    return;
  }
  consecutiveFailures = 0;
  lastReadOK = true;
  recoveryDelay = RECOVERY_MIN_MS;
  if (recoveryPending) { ++recoveries; recoveryPending = false; Serial.println("RECOVERY measurement OK"); }
  ++validSequence;
  char frame[180];
  snprintf(frame, sizeof(frame), "BID=%08lX,SEQ=%lu,MS=%lu,T=%.2f,H=%.2f,P=%.2f,G=%.2f",
           (unsigned long)bootId, (unsigned long)validSequence, (unsigned long)millis(),
           bme.temperature, bme.humidity, bme.pressure / 100.0, bme.gas_resistance / 1000.0);
  if (sendLine(frame)) { Serial.print("TX "); Serial.println(frame); }
}
