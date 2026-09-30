// ============================================================
// Food Spoilage Monitor - nRF52840 + BME688 Sensor Node
// ============================================================
//
// [실제 하드웨어]
// - MCU 보드 : V1940 Pro Micro nRF52840
//              (nice!nano V2 호환 계열 클론)
// - 센서     : Bosch BME688
// - 통신     : BME688 -> I2C -> nRF52840 -> BLE NUS -> ESP32-S3
//
// ------------------------------------------------------------
// [Arduino IDE 보드 설정]
// ------------------------------------------------------------
//
// 실제 보드는 V1940 Pro Micro nRF52840이지만,
// 현재 프로젝트에서는 Adafruit nRF52 BSP의 아래 보드 정의를 사용한다.
//
//   Tools -> Board -> Adafruit nRF52 Boards
//         -> Adafruit Feather nRF52840 Express
//
// ※ "Adafruit Feather nRF52840 Express"는 실제 보드명이 아니라
//    현재 V1940 보드를 컴파일/업로드하기 위해 사용 중인 호환 타깃이다.
//
// ------------------------------------------------------------
// [Adafruit nRF52 보드 패키지 설치]
// ------------------------------------------------------------
//
// Arduino IDE
//
//   File -> Preferences
//        -> Additional Boards Manager URLs
//
// 에 Adafruit nRF52 BSP 공식 Board Manager URL을 추가한다.
// (프로젝트 문서/코드 설명에 적힌 공식 URL 참조)
//
// 그 다음:
//
//   Tools -> Board -> Boards Manager
//
// 검색:
//
//   Adafruit nRF52
//
// 설치:
//
//   "Adafruit nRF52 by Adafruit"
//
// 설치 후 Arduino IDE에서:
//
//   Adafruit Feather nRF52840 Express
//
// 를 선택한다.
//
// ※ ESP32 설치 방법과 기본 구조는 동일하지만,
//    ESP32는 Espressif BSP,
//    이 nRF52840 코드는 Adafruit nRF52 BSP를 사용한다.
//
// ------------------------------------------------------------
// [필요 라이브러리]
// ------------------------------------------------------------
//
// Arduino IDE:
//   Sketch -> Include Library -> Manage Libraries
//
// 1. "Adafruit BME680 Library"
//    - BME680 / BME688 센서 드라이버
//    - 이 프로젝트에서는 BME688에 사용
//
// 2. "Adafruit Unified Sensor"
//    - Adafruit 센서 공통 인터페이스
//
// 3. "Adafruit BusIO"
//    - Adafruit 센서의 I2C/SPI 통신 지원
//    - BME680 Library 설치 시 의존성으로 함께 설치될 수 있음
//
// 4. "Adafruit SSD1306"
//    - I2C OLED 디스플레이 드라이버
//
// 5. "Adafruit GFX Library"
//    - SSD1306 화면의 문자/그래픽 출력 지원
//    - SSD1306 Library 설치 시 의존성으로 함께 설치될 수 있음
//
// 라이브러리 설치 창에서 의존 라이브러리 설치 여부를 물으면
// "Install All"을 선택해도 된다.
//
// ------------------------------------------------------------
// [별도 설치할 필요가 없는 것]
// ------------------------------------------------------------
//
// #include <bluefruit.h>
//
//   -> Adafruit nRF52 BSP에 포함되어 있음.
//      별도의 Bluefruit 라이브러리를 Library Manager에서
//      추가 설치할 필요 없음.
//
// #include <Wire.h>
//
//   -> Arduino/nRF52 Core에 포함되어 있음.
//      별도 설치 불필요.
//
// ------------------------------------------------------------
// [BME688 I2C 설정 - 현재 하드웨어]
// ------------------------------------------------------------
//
// BME688 I2C Address : 0x76
//
// 실제 nRF52840 핀:
//   SDA = P0.31
//   SCL = P0.29
//
// 현재 Adafruit Feather nRF52840 Express variant 기준 Arduino 핀:
//   SDA = D21 -> P0.31
//   SCL = D20 -> P0.29
//
// 따라서 이 프로젝트에서는:
//
//   Wire.setPins(21, 20);
//   Wire.begin();
//
// 을 사용한다.
//
// ※ 031, 029를 C++ 숫자로 직접 쓰지 말 것.
//    코드에서는 현재 BSP의 Arduino pin number인 21, 20을 사용한다.
//
// ------------------------------------------------------------
// [SSD1306 OLED 설정 - BME688과 I2C 버스 공유]
// ------------------------------------------------------------
//
// OLED도 Wire.setPins(21, 20)으로 설정한 동일 버스를 사용한다.
// 사용자 하드웨어에서 정상 동작을 확인한 현재 설정:
//   I2C Address : 0x3C
//   Resolution  : 128 x 32
// OLED 초기화 실패는 센서 측정/BLE 전송 실패로 취급하지 않는다.
//
// ------------------------------------------------------------
// [BME688 측정 설정]
// ------------------------------------------------------------
//
// 현재 프로젝트의 주요 설정:
// - 측정 주기       : 약 2초
// - Gas Heater     : 320 °C
// - Heater Duration: 150 ms
//
// initializeSensor()에서 begin() 성공 후
// oversampling / filter / gas heater 설정을 다시 적용한다.
//
// ------------------------------------------------------------
// [센서 이상 및 자동 복구 정책]
// ------------------------------------------------------------
//
// performReading() 실패:
//   -> 해당 샘플 폐기
//   -> SEQ 증가하지 않음
//   -> BLE 전송하지 않음
//
// NaN / Inf / gas_resistance <= 0:
//   -> 유효하지 않은 측정으로 취급
//   -> 해당 샘플 폐기
//
// 연속 측정 실패:
//   -> BME688 Soft Reset
//   -> 센서 재초기화
//   -> 측정 설정 재적용
//   -> 정상 측정 확인 후 자동 복귀
//
// BME688 Soft Reset:
//   Register 0xE0 <- Command 0xB6
//
// 데이터 신뢰성 원칙:
//   "잘못된 값을 전송하는 것보다 missing sample이 낫다."
//
// ------------------------------------------------------------
// [BLE 설정]
// ------------------------------------------------------------
//
// BLE 통신:
// - Nordic UART Service (NUS)
// - ESP32-S3가 BLE Central/Client
// - nRF52840이 BLE Peripheral/Sensor Node
//
// 한 프레임을 20-byte chunk로 전송한다.
//
// 중요:
// BLE Notify TX buffer가 일시적으로 가득 찰 수 있으므로
// bleuart.write() 실패 시 즉시 샘플을 포기하지 않는다.
//
// 현재 구현:
//   write 실패
//      -> 잠시 대기
//      -> 같은 chunk 재시도
//      -> 성공 후 다음 chunk
//
// 기존 "BLE partial TX" 데이터 유실 문제 때문에
// 이 retry 로직을 제거하지 말 것.
//
// ------------------------------------------------------------
// [주의]
// ------------------------------------------------------------
//
// 1. 실제 보드는 Adafruit Feather가 아니라 V1940 클론이다.
//    보드 정의만 Feather nRF52840 Express를 사용한다.
//
// 2. 현재 정상 동작이 확인된 I2C 핀 설정을 임의 변경하지 말 것.
//
// 3. BME688가 G=0 등의 비정상 상태에 고착될 수 있으므로
//    측정 성공 여부만 믿지 말고 measurement validity를 검사한다.
//
// 4. 센서 읽기 실패 시 이전 정상값을 다시 전송하지 말 것.
//
// 5. BLE TX retry 로직을 제거하면 sequence gap / malformed frame이
//    다시 발생할 수 있다.
//
// ============================================================

#include <bluefruit.h>
#include <Wire.h>
#include <Adafruit_BME680.h>
#include <math.h>
#if !defined(_MSC_VER)  // Windows host sensor test에는 OLED 하드웨어가 없다.
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif

constexpr uint8_t BME_ADDRESS = 0x76;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr int16_t OLED_WIDTH = 128;
constexpr int16_t OLED_HEIGHT = 32;
constexpr int8_t OLED_RESET_PIN = -1;
constexpr uint32_t OLED_REFRESH_INTERVAL_MS = 750;
constexpr uint32_t SAMPLE_INTERVAL_MS = 2000;
constexpr uint32_t FAILURE_THRESHOLD = 3;
constexpr uint32_t RECOVERY_MIN_MS = 5000;
constexpr uint32_t RECOVERY_MAX_MS = 60000;
Adafruit_BME680 bme(&Wire);  // begin(address, bool initSettings), NOT a Wire pointer.
BLEUart bleuart;
#if !defined(_MSC_VER)
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);
#endif
uint32_t bootId, validSequence = 0, failures = 0, consecutiveFailures = 0;
uint32_t recoveryAttempts = 0, recoveries = 0, txErrors = 0;
uint32_t nextMeasurement = 0, nextRecovery = 0, lastHealth = 0;
uint32_t recoveryDelay = RECOVERY_MIN_MS;
bool sensorReady = false, recoveryPending = false, lastReadOK = false;
#if !defined(_MSC_VER)
uint32_t lastOledRefresh = 0;
bool oledReady = false;
#endif

bool isMeasurementValid(bool readOK, double temperature, double humidity,
                        double pressure, double gasResistance) {
  // H=100% or low pressure can be physically real; do not hard-code the old
  // incident signature. G<=0 is the confirmed invalid condition.
  return readOK && isfinite(temperature) && isfinite(humidity) &&
         isfinite(pressure) && isfinite(gasResistance) && gasResistance > 0;
}

#if !defined(_MSC_VER)
void initializeOled() {
  // Wire는 initializeSensor()가 검증된 D21/D20 핀으로 이미 시작했다.
  // 주소 ACK를 먼저 확인해 OLED가 없어도 센서 노드를 계속 실행한다.
  Wire.beginTransmission(OLED_ADDRESS);
  if (Wire.endTransmission() != 0) {
    Serial.println("OLED unavailable; sensor/BLE continue");
    return;
  }

  // periphBegin=false: SSD1306가 Wire.begin()을 다시 호출하지 않게 한다.
  oledReady = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false);
  if (!oledReady) {
    Serial.println("OLED init failed; sensor/BLE continue");
    return;
  }

  oled.cp437(true);
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setTextWrap(false);
  oled.setCursor(0, 0);
  oled.println(F("BME688 node starting"));
  oled.display();
  Serial.print("OLED ready: SSD1306 0x"); Serial.print(OLED_ADDRESS, HEX);
  Serial.print(" "); Serial.print(OLED_WIDTH); Serial.print("x"); Serial.println(OLED_HEIGHT);
}

void formatOledCounter(uint32_t value, char* out, size_t outSize) {
  if (value < 1000) snprintf(out, outSize, "%lu", (unsigned long)value);
  else if (value < 1000000) snprintf(out, outSize, "%luk", (unsigned long)(value / 1000));
  else if (value < 1000000000) snprintf(out, outSize, "%luM", (unsigned long)(value / 1000000));
  else snprintf(out, outSize, "%luG", (unsigned long)(value / 1000000000));
}

void updateOled(uint32_t now) {
  if (!oledReady || (uint32_t)(now - lastOledRefresh) < OLED_REFRESH_INTERVAL_MS) {
    return;
  }
  lastOledRefresh = now;

  char line[22];  // 기본 6 px 글꼴 기준 128 px에 최대 21자.
  char seqText[6], failText[6], recoveryText[6], txText[6];
  formatOledCounter(validSequence, seqText, sizeof(seqText));
  formatOledCounter(failures, failText, sizeof(failText));
  formatOledCounter(recoveries, recoveryText, sizeof(recoveryText));
  formatOledCounter(txErrors, txText, sizeof(txText));
  const char* sensorText = !sensorReady ? "REC" : recoveryPending ? "CHK" : lastReadOK ? "OK" : "ERR";

  oled.clearDisplay();
  oled.setCursor(0, 0);
  snprintf(line, sizeof(line), "BLE:%s BME:%s X=%s", Bluefruit.connected() ? "ON" : "OFF", sensorText, txText);
  oled.println(line);

  // 실패한 performReading() 뒤에는 bme 객체의 이전 값을 표시하지 않는다.
  // 화면에서도 stale 값을 현재 측정값으로 오인하지 않게 명시적으로 비운다.
  if (lastReadOK) {
    snprintf(line, sizeof(line), "Tem:%.1fC Hum:%.1f%%", bme.temperature, bme.humidity);
    oled.println(line);
    snprintf(line, sizeof(line), "hPa:%.1f G:%.1fk", bme.pressure / 100.0, bme.gas_resistance / 1000.0);
    oled.println(line);
  } else {
    oled.println(F("T:--.-C H:--.-%"));
    oled.println(F("P:----.- G:---.-k"));
  }

  snprintf(line, sizeof(line), "Seq:%s Fail:%s Re:%s", seqText, failText, recoveryText);
  oled.println(line);
  oled.display();
}
#endif

bool bleWriteRetry(const uint8_t* data, size_t len) {
  // BLE Notify TX 슬롯이 잠깐 꽉 차더라도
  // 즉시 샘플을 버리지 않고 최대 약 300 ms 동안 재시도한다.
  constexpr int MAX_RETRIES = 15;
  constexpr int RETRY_DELAY_MS = 20;

  for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {

    if (!Bluefruit.connected() ||
        !bleuart.notifyEnabled()) {
      return false;
    }

    size_t written = bleuart.write(data, len);

    if (written == len) {
      return true;
    }

    // 이전 Notify가 완료되어 TX packet slot이 반환될 시간을 준다.
    delay(RETRY_DELAY_MS);
  }

  ++txErrors;

  Serial.print("ERROR BLE TX timeout len=");
  Serial.println(len);

  return false;
}


bool sendLine(const char* line) {
  if (!Bluefruit.connected() ||
      !bleuart.notifyEnabled()) {
    return false;
  }

  // ------------------------------------------------------------
  // 이전에 잘린 frame이 남아 있더라도 newline으로 먼저 끊는다.
  // ------------------------------------------------------------
  const uint8_t newline = '\n';

  if (!bleWriteRetry(&newline, 1)) {
    return false;
  }

  // ------------------------------------------------------------
  // BLE 기본 ATT payload에 맞춰 20 byte씩 전송.
  //
  // 기존에는 5 ms 후 다음 chunk를 바로 전송했기 때문에
  // Notify packet slot이 반환되기 전에 다음 write가 들어가면서
  // 전송 실패가 발생할 수 있었다.
  //
  // 이제 실패하면 동일 chunk를 기다렸다가 재시도한다.
  // ------------------------------------------------------------
  const size_t len = strlen(line);

  for (size_t i = 0; i < len; i += 20) {

    size_t n = min((size_t)20, len - i);

    if (!bleWriteRetry(
          (const uint8_t*)line + i,
          n
        )) {

      Serial.println(
        "ERROR BLE frame TX failed after retries"
      );

      return false;
    }

    // 다음 Notify 전에 약간의 여유
    delay(10);
  }

  // frame 종료
  if (!bleWriteRetry(&newline, 1)) {
    return false;
  }

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

// ============================================================
// BME688 강제 소프트 리셋
// - 이상값(G=0 등)이 지속될 때 센서 내부 상태를 초기화
// - BME688 I2C 주소: 0x76
// ============================================================

bool softResetBME688() {
  Wire.beginTransmission(0x76);
  Wire.write(0xE0);   // Soft-reset register
  Wire.write(0xB6);   // Soft-reset command

  uint8_t err = Wire.endTransmission();

  // Bosch reset 이후 충분히 대기
  delay(20);

  if (err != 0) {
    Serial.print("RECOVERY soft reset I2C error=");
    Serial.println(err);
    return false;
  }

  Serial.println("RECOVERY BME688 soft reset OK");
  return true;
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
#if !defined(_MSC_VER)
  initializeOled();
#endif
}

void loop() {
  const uint32_t now = millis();
  if (now - lastHealth >= 10000) { lastHealth = now; sendHealth(); }
  if (!sensorReady) {
    if ((int32_t)(now - nextRecovery) >= 0) recoverSensor();
#if !defined(_MSC_VER)
    updateOled(millis());
#endif
    delay(10);
    return;
  }
  if ((int32_t)(now - nextMeasurement) < 0) {
#if !defined(_MSC_VER)
    updateOled(now);
#endif
    delay(5);
    return;
  }
  nextMeasurement = now + SAMPLE_INTERVAL_MS;  // no catch-up burst after a delay
  const bool readOK = bme.performReading();
  const double gasResistance = (double)bme.gas_resistance;
  const bool validMeasurement = isMeasurementValid(readOK, bme.temperature, bme.humidity,
                                                    bme.pressure, gasResistance);
  if (!validMeasurement) {
    // DATA INTEGRITY: failed reads leave previous T/H/P/G in memory. The real
    // G=0 lockup can also return numeric values with performReading()==true.
    // Either case is a missing sample: no frame/SEQ, only diagnostics/recovery.
    ++failures; ++consecutiveFailures; lastReadOK = false;
    if (!readOK) {
      Serial.print("SENSOR ERROR performReading failed");
    } else if (!isfinite(bme.temperature) || !isfinite(bme.humidity) ||
               !isfinite(bme.pressure) || !isfinite(gasResistance)) {
      Serial.print("SENSOR ERROR non-finite measurement");
    } else {
      Serial.print("SENSOR ERROR invalid gas resistance G="); Serial.print(gasResistance);
    }
    Serial.print("; failures="); Serial.println(failures);
    if (consecutiveFailures >= FAILURE_THRESHOLD) {
      sensorReady = false;
      // nextRecovery was set by the previous attempt; never reset every 2 s.
      if ((int32_t)(millis() - nextRecovery) >= 0) recoverSensor();
    }
    sendHealth();
#if !defined(_MSC_VER)
    updateOled(millis());
#endif
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
#if !defined(_MSC_VER)
  updateOled(millis());
#endif
}
