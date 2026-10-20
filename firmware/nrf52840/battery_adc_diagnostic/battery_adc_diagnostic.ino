// V1940 Pro Micro nRF52840 battery ADC diagnostic
//
// Build target: Adafruit Feather nRF52840 Express (Adafruit nRF52 core 1.7.0)
// This sketch does not identify a battery voltage or state of charge. It only
// reports ADC-domain voltages for inputs that are safe to inspect without
// changing the FoodSpoilageMonitor production firmware.
//
// IMPORTANT:
// - A0 on the selected Feather variant maps to nRF52840 P0.04 / AIN2.
// - Do not use PIN_VBAT: that Feather alias maps to D20/P0.29, which is the
//   verified BME688 I2C SCL pin on the V1940 hardware.
// - P0.24 is not an nRF52840 SAADC input and is intentionally never read.
// - No external voltage should be applied to any candidate pin.

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>  // Provides the native USB Serial implementation.
#include <bluefruit.h>

namespace {

constexpr uint32_t kP004ArduinoPin = A0;  // Feather variant D14 -> P0.04/AIN2.
constexpr uint32_t kAdcFullScaleMillivolts = 3600;
constexpr uint32_t kAdcMaximumCode = (1UL << 14) - 1UL;
constexpr uint32_t kSampleIntervalMs = 5000;
constexpr uint8_t kSamplesPerReading = 8;
constexpr size_t kBleChunkBytes = 20;
constexpr char kBleDeviceName[] = "V1940-ADC-TEST";

BLEUart bleuart;

// P0.04 is the only external candidate enabled by default. Official
// nice!nano v1 documentation and clone schematics identify this net, but the
// V1940 PCB revision and divider ratio remain unverified.
constexpr bool kReadP004 = true;

// This internal SAADC source reports VDDH divided by five. It is safe to read,
// but USB/charger topology can make VDDH differ from the LiPo voltage. The
// multiplied value is therefore labelled VDDH estimate, never battery voltage.
constexpr bool kReadVddhDiv5 = true;

uint32_t averageExternal(uint32_t pin) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < kSamplesPerReading; ++i) {
    total += analogRead(pin);
    delay(10);
  }
  return (total + kSamplesPerReading / 2) / kSamplesPerReading;
}

uint32_t averageVddhDiv5() {
  uint32_t total = 0;
  for (uint8_t i = 0; i < kSamplesPerReading; ++i) {
    total += analogReadVDDHDIV5();
    delay(10);
  }
  return (total + kSamplesPerReading / 2) / kSamplesPerReading;
}

uint32_t rawToInputMillivolts(uint32_t raw) {
  return (raw * kAdcFullScaleMillivolts + kAdcMaximumCode / 2) /
         kAdcMaximumCode;
}

void printReading(const __FlashStringHelper* label, uint32_t raw,
                  uint32_t multiplier) {
  if (!Serial) {
    return;
  }
  const uint32_t inputMillivolts = rawToInputMillivolts(raw);
  Serial.print(label);
  Serial.print(F(" raw="));
  Serial.print(raw);
  Serial.print(F(" adc_input_mv="));
  Serial.print(inputMillivolts);
  if (multiplier != 1) {
    Serial.print(F(" source_estimate_mv="));
    Serial.print(inputMillivolts * multiplier);
  }
  Serial.println();
}

bool bleWriteRetry(const uint8_t* data, size_t len) {
  constexpr uint8_t kMaximumAttempts = 10;
  for (uint8_t attempt = 0; attempt < kMaximumAttempts; ++attempt) {
    if (!Bluefruit.connected() || !bleuart.notifyEnabled()) {
      return false;
    }
    if (bleuart.write(data, len) == len) {
      return true;
    }
    delay(20);
  }
  return false;
}

bool sendBleLine(const char* line) {
  if (!Bluefruit.connected() || !bleuart.notifyEnabled()) {
    return false;
  }

  const size_t len = strlen(line);
  for (size_t offset = 0; offset < len; offset += kBleChunkBytes) {
    const size_t remaining = len - offset;
    const size_t chunk = remaining < kBleChunkBytes ? remaining : kBleChunkBytes;
    if (!bleWriteRetry(reinterpret_cast<const uint8_t*>(line) + offset, chunk)) {
      return false;
    }
    delay(10);
  }

  const uint8_t newline = '\n';
  return bleWriteRetry(&newline, 1);
}

void startBleUart() {
  Bluefruit.begin();
  Bluefruit.setName(kBleDeviceName);
  bleuart.begin();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

}  // namespace

void setup() {
  Serial.begin(115200);

  startBleUart();

  const uint32_t waitStarted = millis();
  while (!Serial && millis() - waitStarted < 5000) {
    delay(10);
  }

  analogReference(AR_INTERNAL);  // 0.6 V reference, gain 1/6: 0..3.6 V.
  analogReadResolution(14);
  analogOversampling(8);
  analogSampleTime(40);  // Suitable for a possible high-impedance divider.
  analogCalibrateOffset();

  if (kReadP004) {
    pinMode(kP004ArduinoPin, INPUT);
  }

  Serial.println(F("V1940 battery ADC diagnostic"));
  Serial.print(F("BLE UART name: "));
  Serial.println(kBleDeviceName);
  Serial.println(F("ADC: 14-bit, internal 0.6 V reference, gain 1/6, 0..3600 mV"));
  Serial.println(F("Formula: adc_input_mv = raw * 3600 / 16383"));
  Serial.println(F("P0.24 disabled: not an nRF52840 SAADC input"));
  Serial.println(F("PIN_VBAT disabled: Feather alias conflicts with V1940 I2C SCL"));
  Serial.println(F("Values below are ADC-domain observations, not battery voltage or percent"));
}

void loop() {
  uint32_t p004Raw = 0;
  uint32_t p004InputMillivolts = 0;
  uint32_t vddhDiv5Raw = 0;
  uint32_t vddhDiv5InputMillivolts = 0;

  if (kReadP004) {
    p004Raw = averageExternal(kP004ArduinoPin);
    p004InputMillivolts = rawToInputMillivolts(p004Raw);
    printReading(F("P0.04/AIN2"), p004Raw, 1);
  }

  if (kReadVddhDiv5) {
    vddhDiv5Raw = averageVddhDiv5();
    vddhDiv5InputMillivolts = rawToInputMillivolts(vddhDiv5Raw);
    printReading(F("VDDH/5"), vddhDiv5Raw, 5);
  }

  if (Bluefruit.connected() && bleuart.notifyEnabled()) {
    char line[64];
    if (kReadP004) {
      snprintf(line, sizeof(line), "P004,R=%lu,MV=%lu",
               (unsigned long)p004Raw,
               (unsigned long)p004InputMillivolts);
      sendBleLine(line);
    }
    if (kReadVddhDiv5) {
      snprintf(line, sizeof(line), "V5,R=%lu,MV=%lu,VDDH=%lu",
               (unsigned long)vddhDiv5Raw,
               (unsigned long)vddhDiv5InputMillivolts,
               (unsigned long)(vddhDiv5InputMillivolts * 5));
      sendBleLine(line);
    }
  }

  if (Serial) {
    Serial.println(F("---"));
  }
  delay(kSampleIntervalMs);
}
