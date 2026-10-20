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
// 1. "Bosch BME68x Library" (스케치 src에 포함된 고정본)
//    - Bosch 공식 BME68x Sensor API wrapper, HP-354 parallel mode
//    - Bosch 라이브러리는 별도 설치하지 않는다. 스케치 폴더 전체를 복사한다.
//
// 2. "Adafruit SSD1306"
//    - I2C OLED 디스플레이 드라이버
//
// 3. "Adafruit GFX Library"
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
// HP-354 초기 실험 프로파일: Bosch parallel_mode 공식 예제의
// 10개 온도 및 gas-wait multiplier. 140ms 기준을 이용해 shared heater
// duration을 getMeasDur(PARALLEL_MODE)로 계산한다. 실제 scan 주기와
// 음식 분류 성능은 실기 실험 전에는 보장하지 않는다.
//
// ------------------------------------------------------------
// [센서 이상 및 자동 복구 정책]
// ------------------------------------------------------------
//
// Bosch fetchData()/status 실패:
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
#if defined(_MSC_VER)  // Windows host sensor test uses the Bosch API stub.
#include <bme68xLibrary.h>
#else
#include "src/Bosch-BME68x-Library/src/bme68xLibrary.h"
#endif
#include "Scan.h"
#include <math.h>
#if !defined(_MSC_VER)  // Windows host sensor test에는 OLED 하드웨어가 없다.
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif
#include "Battery.h"
#if !defined(_MSC_VER)
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
using namespace Adafruit_LittleFS_Namespace;
#endif

constexpr uint8_t BME_ADDRESS = 0x76;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr int16_t OLED_WIDTH = 128;
constexpr int16_t OLED_HEIGHT = 32;
constexpr int8_t OLED_RESET_PIN = -1;
constexpr uint32_t OLED_REFRESH_INTERVAL_MS = 750;
constexpr uint32_t ENV_INTERVAL_MS = 2000;
constexpr uint32_t BATTERY_INTERVAL_MS = 60000;
constexpr uint32_t USB_POWER_CHECK_INTERVAL_MS = 1000;
constexpr uint32_t ADC_FULL_SCALE_MV = 3600;
constexpr uint32_t ADC_MAX_CODE = (1UL << 14) - 1UL;
constexpr uint32_t GAS_SCAN_INTERVAL_MS = 600000;
constexpr uint32_t GAS_SCAN_TIMEOUT_MS = 60000;
constexpr uint32_t COOLING_MS = 60000; // provisional stabilization delay; validate on hardware
constexpr uint32_t ENV_CONVERSION_TIMEOUT_MS = 200;
constexpr uint32_t FAILURE_THRESHOLD = 3;
constexpr uint32_t RECOVERY_MIN_MS = 5000;
constexpr uint32_t RECOVERY_MAX_MS = 60000;
constexpr char SENSOR_FIRMWARE_VERSION[] = "2.0.8";
Bme68x bme;
gas_scan::Tracker scanTracker;
bme68xData lastEnvData={};
#if !defined(_MSC_VER)
BLEDfu bledfu; // Buttonless entry into the existing Adafruit legacy BLE DFU bootloader.
BLEDis bledis; // Standard Device Information Service exposes the firmware version without changing NUS frames.
#endif
BLEUart bleuart;
#if !defined(_MSC_VER)
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);
#endif
uint32_t bootId, validSequence = 0, failures = 0, consecutiveFailures = 0;
uint32_t envSequence = 0, envFailures = 0, envConsecutiveFailures = 0;
uint32_t recoveryAttempts = 0, recoveries = 0, txErrors = 0;
uint32_t nextMeasurement = 0, nextRecovery = 0, lastHealth = 0;
uint32_t nextBatteryMeasurement = 0, nextUsbPowerCheck = 0, batteryMillivolts = 0;
battery::Tracker batteryTracker;
#if !defined(_MSC_VER)
bool batteryStorageReady = false;
constexpr char BATTERY_STATE_A[] = "/battery-a.bin";
constexpr char BATTERY_STATE_B[] = "/battery-b.bin";
#endif
uint32_t lastSensorField = 0;
uint32_t sensorReadyAt = 0;
uint32_t nextEnvMeasurement = 0, envConversionAt = 0, nextGasScan = 0;
uint32_t scanStartedAt = 0, coolingStartedAt = 0, scanCompleteBefore = 0, scanIncompleteBefore = 0;
uint32_t recoveryDelay = RECOVERY_MIN_MS;
bool sensorReady = false, recoveryPending = false, lastReadOK = false;
bool envReadOK = false, envConversionPending = false;
bool batteryMeasurementValid = false, usbPowerPresent = false;
#if defined(_MSC_VER)
bool testUsbPowerPresent = false;
uint32_t testVddhRaw = 3723; // Approximately 4.09 V after VDDH/5 conversion.
#endif
bool haveEnvMeasurementIndex=false;
uint8_t lastEnvMeasurementIndex=0;
bool recoveringGas = false;
enum SensorMode : uint8_t { MODE_ENV, MODE_GAS, MODE_COOL, MODE_REC };
SensorMode sensorMode = MODE_REC;

battery::LoadMode batteryLoadMode() {
  return sensorMode==MODE_ENV ? battery::LOAD_ENV : sensorMode==MODE_GAS ? battery::LOAD_GAS :
         sensorMode==MODE_COOL ? battery::LOAD_COOL : battery::LOAD_REC;
}
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

uint32_t vddhMillivoltsFromRaw(uint32_t raw) {
  const uint32_t inputMv = (raw * ADC_FULL_SCALE_MV + ADC_MAX_CODE / 2) /
                           ADC_MAX_CODE;
  return inputMv * 5;
}

bool readUsbPowerPresent(bool& present) {
#if defined(_MSC_VER)
  present = testUsbPowerPresent;
  return true;
#else
  uint32_t status = 0;
  if (sd_power_usbregstatus_get(&status) != NRF_SUCCESS) return false;
  present = (status & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
  return true;
#endif
}

uint32_t readVddhRaw() {
#if defined(_MSC_VER)
  return testVddhRaw;
#else
  uint32_t total = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    total += analogReadVDDHDIV5();
    delay(10);
  }
  return (total + 4) / 8;
#endif
}

void measureBatteryVoltage() {
  bool usb = true; // Fail closed: an unknown supply must not be called a battery.
  if (!readUsbPowerPresent(usb)) {
    usbPowerPresent = true;
    batteryMeasurementValid = false;
    batteryMillivolts = 0;
    return;
  }
  usbPowerPresent = usb;
  if (usb) {
    batteryMeasurementValid = false;
    batteryMillivolts = 0;
    return;
  }
  const uint32_t mv = vddhMillivoltsFromRaw(readVddhRaw());
  batteryMeasurementValid = mv >= 2500 && mv <= 5500;
  batteryMillivolts = batteryMeasurementValid ? mv : 0;

  // 유효한 배터리 전압을 측정했을 때만 기록
  if (batteryMeasurementValid) {
    batteryTracker.observe(batteryMillivolts, millis(), batteryLoadMode());
  }
}

#if !defined(_MSC_VER)
bool readBatteryState(const char* path, battery::PersistedState& state) {
  File file(path, FILE_O_READ, InternalFS);
  if (!file || file.size() != sizeof(state)) { if (file) file.close(); return false; }
  const bool readOK = file.read(&state, sizeof(state)) == (int)sizeof(state);
  file.close(); return readOK && battery::intact(state);
}

void initializeBatteryStorage() {
  // Bluefruit.begin() has already mounted the core's bonding filesystem.
  // Do not call InternalFS.begin() here: that BSP API formats on mount failure.
  batteryStorageReady = InternalFS.exists("/");
  if (!batteryStorageReady) {
    Serial.println("BAT persistence disabled: existing InternalFS unavailable; no format requested");
    return;
  }
  battery::PersistedState a, b;const bool aOK=readBatteryState(BATTERY_STATE_A,a);
  const bool bOK=readBatteryState(BATTERY_STATE_B,b);
  const battery::PersistedState* newest = aOK&&bOK ? (a.generation>=b.generation?&a:&b) : aOK?&a:bOK?&b:nullptr;
  if (newest) {
    batteryTracker.restore(*newest);
    Serial.print("BAT state restored generation=");Serial.println(newest->generation);
  } else if (InternalFS.exists(BATTERY_STATE_A) || InternalFS.exists(BATTERY_STATE_B)) {
    batteryTracker.state=battery::INVALID;
    Serial.println("BAT state invalid; default curve retained");
  } else Serial.println("BAT state new; default curve retained");
}

bool writeBatteryState(const char* path, const battery::PersistedState& state) {
  // Alternate slots: a power loss cannot invalidate the previously verified slot.
  if (InternalFS.exists(path) && !InternalFS.remove(path)) return false;
  File file(path, FILE_O_WRITE, InternalFS);
  if (!file) return false;
  const bool wrote=file.write((const uint8_t*)&state,sizeof(state))==sizeof(state);
  file.flush();file.close();if(!wrote)return false;
  battery::PersistedState check;return readBatteryState(path,check)&&check.generation==state.generation;
}

void serviceBatteryStorage(uint32_t now) {
  if (!batteryStorageReady || !batteryTracker.shouldPersist(now)) return;
  battery::PersistedState state=batteryTracker.snapshot();
  const char* path=(state.generation&1)?BATTERY_STATE_A:BATTERY_STATE_B;
  if(writeBatteryState(path,state)){
    batteryTracker.persisted(now,state.generation);
    Serial.print("BAT state saved generation=");Serial.println(state.generation);
  } else Serial.println("BAT state save failed; previous slot retained");
}
#endif

void serviceBatteryVoltage(uint32_t now) {
  if ((int32_t)(now - nextUsbPowerCheck) >= 0) {
    nextUsbPowerCheck = now + USB_POWER_CHECK_INTERVAL_MS;
    bool usb = true;
    const bool wasUsb = usbPowerPresent;
    if (!readUsbPowerPresent(usb)) usb = true;
    usbPowerPresent = usb;
    if (usb) {
      batteryMeasurementValid = false;
      batteryMillivolts = 0;
      return;
    }
    if (wasUsb) nextBatteryMeasurement = now;
  }
  if (usbPowerPresent) return;
  if ((int32_t)(now - nextBatteryMeasurement) < 0) return;
  nextBatteryMeasurement = now + BATTERY_INTERVAL_MS;
  measureBatteryVoltage();
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
  if (value < 1000)
    snprintf(out, outSize, "%lu", (unsigned long)value);
  else if (value < 1000000)
    snprintf(out, outSize, "%luk", (unsigned long)(value / 1000));
  else if (value < 1000000000)
    snprintf(out, outSize, "%luM", (unsigned long)(value / 1000000));
  else
    snprintf(out, outSize, "%luG", (unsigned long)(value / 1000000000));
}


void updateOled(uint32_t now) {
  if (!oledReady || (uint32_t)(now - lastOledRefresh) < OLED_REFRESH_INTERVAL_MS) {
    return;
  }
  lastOledRefresh = now;

  char line[22];  // 기본 6 px 글꼴 기준 128 px에 최대 21자.
  char seqText[6], failText[6], recoveryText[6], txText[6];
  formatOledCounter(
    sensorMode == MODE_ENV ? envSequence : validSequence,
    seqText, sizeof(seqText)
  );
  formatOledCounter(failures, failText, sizeof(failText));
  formatOledCounter(recoveries, recoveryText, sizeof(recoveryText));
  formatOledCounter(txErrors, txText, sizeof(txText));
  const char* sensorText = !sensorReady ? "REC" : sensorMode==MODE_GAS ? "GAS" :
    sensorMode==MODE_COOL ? "COOL" : envReadOK ? "OK" : "ERR";

  oled.clearDisplay();
  oled.setCursor(0, 0);
  snprintf(line, sizeof(line), "BLE:%s BME:%s X=%s", Bluefruit.connected() ? "ON" : "OFF", sensorText, txText);
  oled.println(line);

  // Bosch의 무효 real-gas 측정 또는 복구 중에는 이전 값을 현재값처럼 표시하지 않는다.
  // 화면에서도 stale 값을 현재 측정값으로 오인하지 않게 명시적으로 비운다.
  if (sensorMode==MODE_ENV && envReadOK) {
    snprintf(line, sizeof(line), "Tem:%.1fC Hum:%.1f%%", lastEnvData.temperature, lastEnvData.humidity);
    oled.println(line);
    
    if (usbPowerPresent) {
      snprintf(line, sizeof(line),
               "hPa:%.1f B:USB",
               lastEnvData.pressure / 100.0);

    } else if (batteryMeasurementValid) {
      snprintf(line, sizeof(line),
               "hPa:%.1f B:%u%%",
               lastEnvData.pressure / 100.0,
               batteryTracker.estimatedPercent());

    } else {
      snprintf(line, sizeof(line),
               "hPa:%.1f B:--%%",
               lastEnvData.pressure / 100.0);
    }

    oled.println(line);
  } else {
    oled.println(F("T:--.-C H:--.-%"));
    oled.println(sensorMode==MODE_GAS ? F("HP354 scan active") :
                 sensorMode==MODE_COOL ? F("Cooling sensor") : F("P:----.-"));
  }

  snprintf(line, sizeof(line), "%s:%s F:%s R:%s",
         sensorMode == MODE_ENV ? "ENV" : "GAS",
         seqText, failText, recoveryText);
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
  char line[224];
  const char* mode=sensorMode==MODE_GAS?"GAS":sensorMode==MODE_COOL?"COOL":
                   sensorMode==MODE_REC?"REC":"ENV";
  snprintf(line, sizeof(line), "STAT,BID=%08lX,MS=%lu,FAIL=%lu,REC=%lu,TRY=%lu,TXERR=%lu,OK=%u,MODE=%s,EOK=%u,GOK=%u,BV=%lu,BOK=%u,USB=%u,BP=%u,BS=%u,MN=%lu,MX=%lu",
           (unsigned long)bootId, (unsigned long)millis(), (unsigned long)failures,
           (unsigned long)recoveries, (unsigned long)recoveryAttempts,
           (unsigned long)txErrors, sensorMode==MODE_ENV ? (envReadOK ? 1 : 0) :
           sensorMode==MODE_GAS ? (lastReadOK ? 1 : 0) : 0, mode,
           envReadOK ? 1 : 0, lastReadOK ? 1 : 0,
           (unsigned long)batteryMillivolts, batteryMeasurementValid ? 1 : 0,
           usbPowerPresent ? 1 : 0,
           batteryMeasurementValid ? batteryTracker.estimatedPercent() : 0,
           (unsigned)batteryTracker.state,
           (unsigned long)(batteryTracker.minMv==UINT32_MAX?0:batteryTracker.minMv),
           (unsigned long)batteryTracker.maxMv);
  sendLine(line);
}

bool stopGasHeater() {
  // Bosch set_heatr_conf(enable=0) clears HCTRL and RUN_GAS. The wrapper has
  // no disable method, so perform the equivalent read/modify/write in sleep.
  bme.setOpMode(BME68X_SLEEP_MODE);
  if(bme.checkStatus()==BME68X_ERROR)return false;
  const uint8_t gas0=bme.readReg(BME68X_REG_CTRL_GAS_0);
  if(bme.checkStatus()==BME68X_ERROR)return false;
  const uint8_t gas1=bme.readReg(BME68X_REG_CTRL_GAS_1);
  if(bme.checkStatus()==BME68X_ERROR)return false;
  bme.writeReg(BME68X_REG_CTRL_GAS_0,gas0|BME68X_HCTRL_MSK);
  if(bme.checkStatus()==BME68X_ERROR)return false;
  bme.writeReg(BME68X_REG_CTRL_GAS_1,gas1&~BME68X_RUN_GAS_MSK);
  if(bme.checkStatus()==BME68X_ERROR)return false;
  const uint8_t off0=bme.readReg(BME68X_REG_CTRL_GAS_0);
  if(bme.checkStatus()==BME68X_ERROR)return false;
  const uint8_t off1=bme.readReg(BME68X_REG_CTRL_GAS_1);
  return bme.checkStatus()!=BME68X_ERROR && (off0&BME68X_HCTRL_MSK) &&
         !(off1&BME68X_RUN_GAS_MSK);
}

bool startGasScan() {
  bme.setOpMode(BME68X_SLEEP_MODE);
  if(bme.checkStatus()==BME68X_ERROR)return false;
  const uint32_t tphMs=bme.getMeasDur(BME68X_PARALLEL_MODE)/1000;
  if(tphMs>=gas_scan::POLL_MS)return false;
  const uint16_t sharedMs=(uint16_t)(gas_scan::POLL_MS-tphMs);
  bme.setHeaterProf(gas_scan::temperature,gas_scan::multiplier,sharedMs,gas_scan::STEPS);
  if(bme.checkStatus()==BME68X_ERROR)return false;
  bme.setOpMode(BME68X_PARALLEL_MODE);
  if(bme.checkStatus()==BME68X_ERROR)return false;
  sensorMode=MODE_GAS;lastReadOK=false;envReadOK=false;
  scanStartedAt=millis();sensorReadyAt=scanStartedAt;lastSensorField=scanStartedAt;
  scanCompleteBefore=scanTracker.complete;scanIncompleteBefore=scanTracker.incomplete;
  nextMeasurement=scanStartedAt;
  nextGasScan=scanStartedAt+GAS_SCAN_INTERVAL_MS;
  Serial.print("HP354 parallel shared heater ms=");Serial.println(sharedMs);
  sendHealth();
  return true;
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
  bme.begin(BME_ADDRESS, Wire);
  if (bme.checkStatus() == BME68X_ERROR) return false;
  bme.setTPH(BME68X_OS_8X, BME68X_OS_4X, BME68X_OS_2X);
  if (bme.checkStatus() == BME68X_ERROR) return false;
  bme.setFilter(BME68X_FILTER_SIZE_3);
  if (bme.checkStatus() == BME68X_ERROR) return false;
  return stopGasHeater();
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
  const bool wasGas=sensorMode==MODE_GAS || recoveringGas;
  ++recoveryAttempts;
  scanTracker.resetForRecovery(); // retain scan counter within this BID
  sensorReady = initializeSensor();
  if(sensorReady)sensorReadyAt=millis();
  recoveryPending = true;
  lastReadOK = false;consecutiveFailures=0;envConsecutiveFailures=0;
  envReadOK=false;envConversionPending=false;haveEnvMeasurementIndex=false;
  sensorMode=!sensorReady?MODE_REC:wasGas?MODE_COOL:MODE_ENV;
  if(sensorReady)recoveringGas=false;
  if(wasGas){coolingStartedAt=millis();nextGasScan=coolingStartedAt+GAS_SCAN_INTERVAL_MS;}
  nextRecovery = millis() + recoveryDelay;
  recoveryDelay = min(recoveryDelay * 2, RECOVERY_MAX_MS);
  Serial.print("RECOVERY init="); Serial.print(sensorReady);
  Serial.print(" attempts="); Serial.println(recoveryAttempts);
  nextMeasurement = millis();nextEnvMeasurement=millis();
  lastSensorField = millis();
  sendHealth();
}

void finishGasScan() {
  // Keep every received v3 step, but never fill gaps or promote a partial scan.
  scanTracker.resetForRecovery();
  if(scanTracker.complete==scanCompleteBefore && scanTracker.incomplete==scanIncompleteBefore)
    ++scanTracker.incomplete;
  const bool complete=scanTracker.complete>scanCompleteBefore;
  Serial.print(complete?"SCAN complete":"SCAN incomplete");
  Serial.print(" total incomplete=");Serial.println(scanTracker.incomplete);
  if(!stopGasHeater()) {Serial.println("SENSOR ERROR heater-off transition");recoverSensor();return;}
  sensorMode=MODE_COOL;coolingStartedAt=millis();envReadOK=false;lastReadOK=complete;
  envConversionPending=false;nextEnvMeasurement=coolingStartedAt+COOLING_MS;
  sendHealth();
}

void setup() {
  Serial.begin(115200);
#if !defined(_MSC_VER)
  analogReference(AR_INTERNAL); // 0.6 V reference, gain 1/6: 0..3.6 V ADC input.
  analogReadResolution(14);
  analogOversampling(8);
  analogSampleTime(40);
  analogCalibrateOffset();
#endif
  Bluefruit.begin();
#if !defined(_MSC_VER)
  initializeBatteryStorage();
#endif
  Bluefruit.setTxPower(4);
  char sensorName[24];
  snprintf(sensorName,sizeof(sensorName),"BME688-%08lx%08lx",
    (unsigned long)NRF_FICR->DEVICEID[0],(unsigned long)NRF_FICR->DEVICEID[1]);
  Bluefruit.setName(sensorName);
  Serial.print("BLE sensor ID: ");Serial.println(sensorName+7);
  Bluefruit.autoConnLed(false);
  bootId = NRF_FICR->DEVICEID[0] ^ micros();
  // SoftDevice RNG supplies a boot discriminator; gateway UID is cloud identity.
  for (uint8_t i = 0; i < 20; ++i) {
    if (sd_rand_application_vector_get((uint8_t*)&bootId, sizeof(bootId)) == NRF_SUCCESS) break;
    delay(5);
  }
#if !defined(_MSC_VER)
  // Adafruit requires the OTA DFU service to be registered before other GATT services.
  // This only enters the existing bootloader; it does not replace bootloader/SoftDevice.
  bledfu.begin();
  bledis.setFirmwareRev(SENSOR_FIRMWARE_VERSION);
  bledis.begin();
#endif
  bleuart.begin();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
  Serial.print("BOOT gas-scan-v3 version=");Serial.print(SENSOR_FIRMWARE_VERSION);
  Serial.println("; 0x76; D21/D20; HP354 parallel; BLE DFU service");
  nextBatteryMeasurement = millis();
  nextGasScan = millis() + 10000;  // 부팅 10초 후 첫 가스 스캔
  recoverSensor();
#if !defined(_MSC_VER)
  initializeOled();
#endif
}

void environmentFailure(const char* reason) {
  ++failures;++envFailures;++envConsecutiveFailures;envReadOK=false;
  Serial.print("SENSOR ERROR environment ");Serial.println(reason);
  if(envConsecutiveFailures>=FAILURE_THRESHOLD) {
    sensorReady=false;sensorMode=MODE_REC;envConversionPending=false;
    if((int32_t)(millis()-nextRecovery)>=0)recoverSensor();
    sendHealth();
  }
}

void serviceEnvironment(uint32_t now) {
  if(!envConversionPending) {
    if(!recoveryPending && (int32_t)(now-nextGasScan)>=0) {
      if(!startGasScan()){Serial.println("SENSOR ERROR start gas scan");recoveringGas=true;recoverSensor();}
      return;
    }
    if((int32_t)(now-nextEnvMeasurement)<0)return;
    nextEnvMeasurement=now+ENV_INTERVAL_MS; // start-to-start; never catch up in a burst
    bme.setOpMode(BME68X_FORCED_MODE); // heater remains disabled in CTRL_GAS_0/1
    if(bme.checkStatus()==BME68X_ERROR){environmentFailure("forced mode");return;}
    envConversionAt=millis();
    nextMeasurement=envConversionAt+(bme.getMeasDur(BME68X_FORCED_MODE)+999)/1000+5;
    envConversionPending=true;
    return;
  }
  if((int32_t)(now-nextMeasurement)<0)return;
  const uint8_t fields=bme.fetchData();
  if(bme.checkStatus()==BME68X_ERROR) {
    envConversionPending=false;environmentFailure("fetchData");return;
  }
  if(!fields) {
    if((uint32_t)(millis()-envConversionAt)<ENV_CONVERSION_TIMEOUT_MS)return;
    envConversionPending=false;environmentFailure("conversion timeout");return;
  }
  bme68xData data={};bme.getData(data);envConversionPending=false;
  const double hpa=data.pressure/100.0;
  if(!(data.status&BME68X_NEW_DATA_MSK) ||
     //(haveEnvMeasurementIndex && data.meas_index==lastEnvMeasurementIndex) ||
     !isfinite(data.temperature) ||
     !isfinite(data.humidity) || !isfinite(data.pressure) ||
     data.temperature<-40 || data.temperature>85 || data.humidity<0 ||
     data.humidity>100 || hpa<300 || hpa>1100) {
    environmentFailure("invalid T/H/P");return;
  }
  if(envSequence==UINT32_MAX){sensorReady=false;sensorMode=MODE_REC;
    Serial.println("ERROR environment SEQ exhausted");return;}
  ++envSequence;lastEnvData=data;envReadOK=true;envConsecutiveFailures=0;
  lastEnvMeasurementIndex=data.meas_index;haveEnvMeasurementIndex=true;
  recoveryDelay=RECOVERY_MIN_MS;
  if(recoveryPending){++recoveries;recoveryPending=false;Serial.println("RECOVERY environment measurement OK");}
  char frame[120];
  const int n=snprintf(frame,sizeof(frame),
    "ENV,BID=%08lX,SEQ=%lu,MS=%lu,T=%.2f,H=%.2f,P=%.2f",
    (unsigned long)bootId,(unsigned long)envSequence,(unsigned long)millis(),
    data.temperature,data.humidity,hpa);
  if(n<=0 || n>=(int)sizeof(frame)){++txErrors;Serial.println("ERROR BLE environment frame overflow");return;}
  if(sendLine(frame)){Serial.print("TX ");Serial.println(frame);}
}

void loop() {
  const uint32_t now = millis();
  serviceBatteryVoltage(now);
#if !defined(_MSC_VER)
  serviceBatteryStorage(now);
#endif
  if (now - lastHealth >= 10000) { lastHealth = now; sendHealth(); }
  if (!sensorReady) {
    if ((int32_t)(now - nextRecovery) >= 0) recoverSensor();
#if !defined(_MSC_VER)
    updateOled(millis());
#endif
    delay(10);
    return;
  }
  if(sensorMode==MODE_COOL) {
    if((uint32_t)(now-coolingStartedAt)>=COOLING_MS) {
      sensorMode=MODE_ENV;nextEnvMeasurement=now;envReadOK=false;sendHealth();
    }
#if !defined(_MSC_VER)
    updateOled(millis());
#endif
    delay(5);return;
  }
  if(sensorMode==MODE_ENV) {
    serviceEnvironment(now);
#if !defined(_MSC_VER)
    updateOled(millis());
#endif
    delay(5);return;
  }
  if((uint32_t)(now-scanStartedAt)>=GAS_SCAN_TIMEOUT_MS) {
    Serial.println("SCAN timeout; incomplete fingerprint retained");
    if((uint32_t)(now-lastSensorField)>=20000) {
      ++failures;++consecutiveFailures;lastReadOK=false;recoveringGas=true;
      if(!scanTracker.started && scanTracker.complete==scanCompleteBefore &&
         scanTracker.incomplete==scanIncompleteBefore)++scanTracker.incomplete;
      stopGasHeater();recoverSensor(); // a no-real-gas stall needs the existing reset path
    } else finishGasScan();
#if !defined(_MSC_VER)
    updateOled(millis());
#endif
    delay(5);return;
  }
  if ((int32_t)(now - nextMeasurement) < 0) {
#if !defined(_MSC_VER)
    updateOled(now);
#endif
    delay(5);
    return;
  }
  nextMeasurement = now + gas_scan::POLL_MS;  // Bosch example fetch cadence, no catch-up burst
  const uint8_t fields=bme.fetchData();
  bool sawLastStep=false;
  const bool fetchError=bme.checkStatus() == BME68X_ERROR;
  if (fetchError) {
    ++failures; ++consecutiveFailures; lastReadOK=false;
    Serial.println("SENSOR ERROR Bosch fetchData");
  } else if (fields) {
    bme68xData data={};
    for(uint8_t i=0;i<fields;++i) {
      bme.getData(data);
      // Bosch parallel mode intentionally emits dummy TPHG conversions while
      // holding a heater step. GASM_VALID=0 is not an invalid gas reading.
      if(!(data.status&BME68X_NEW_DATA_MSK) ||
         !(data.status&BME68X_GASM_VALID_MSK)) continue;
      if(data.gas_index==gas_scan::STEPS-1)sawLastStep=true;
      lastSensorField=millis();
      const bool statusValid=(data.status & (BME68X_NEW_DATA_MSK |
        BME68X_GASM_VALID_MSK | BME68X_HEAT_STAB_MSK)) ==
        (BME68X_NEW_DATA_MSK | BME68X_GASM_VALID_MSK | BME68X_HEAT_STAB_MSK);
      const bool valuesValid=isMeasurementValid(true,data.temperature,data.humidity,
                                                data.pressure,data.gas_resistance);
      const bool valid=statusValid && valuesValid && data.gas_index<gas_scan::STEPS;
      if(data.gas_index>=gas_scan::STEPS)scanTracker.invalidate();
      const bool grouped=scanTracker.observe(data.gas_index,data.meas_index,valid);
      if(!valid) {
        // DATA INTEGRITY: a real numeric G=0 lockup recovered after reset.
        // Never transmit this or a stale field as a normal heater step.
        ++failures; lastReadOK=false;
        // A real but heater-unstable gas field during warm-up is not yet a
        // persistent fault; later repeated invalid fields use the threshold.
        if(statusValid ? !valuesValid : (uint32_t)(millis()-sensorReadyAt)>=20000)
          ++consecutiveFailures;
        Serial.print("SENSOR invalid step GI="); Serial.print(data.gas_index);
        Serial.print(" ST="); Serial.print(data.status,HEX);
        Serial.print(" G(ohm)="); Serial.println(data.gas_resistance);
        continue;
      }
      consecutiveFailures=0; lastReadOK=true;
      recoveryDelay=RECOVERY_MIN_MS;
      if(recoveryPending) { ++recoveries; recoveryPending=false; Serial.println("RECOVERY measurement OK"); }
      if(!grouped) { Serial.println("SCAN waiting for step 0"); continue; }
      if(validSequence==UINT32_MAX) { sensorReady=false; Serial.println("ERROR sensor SEQ exhausted"); break; }
      ++validSequence;
      char frame[180];
      const int n=snprintf(frame,sizeof(frame),
        "V=3,BID=%08lX,SEQ=%lu,SCAN=%lu,STEP=%u,GI=%u,MI=%u,MS=%lu,ST=%u,PID=354,T=%.2f,H=%.2f,P=%.2f,G=%.2f",
        (unsigned long)bootId,(unsigned long)validSequence,(unsigned long)scanTracker.scan,
        data.gas_index,data.gas_index,data.meas_index,(unsigned long)millis(),data.status,
        data.temperature,data.humidity,data.pressure/100.0,data.gas_resistance/1000.0);
      if(n<=0 || n>=(int)sizeof(frame)) { ++txErrors; Serial.println("ERROR BLE v3 frame overflow"); continue; }
      if(sendLine(frame)) { Serial.print("TX "); Serial.println(frame); }
    }
  }
  const uint32_t watchdogNow = millis(); // sensor processing may update lastSensorField after now
  if (!fetchError && (uint32_t)(watchdogNow-lastSensorField)>=20000) {
    // Dummy conversions can continue during a stuck gas measurement. A real
    // gas field should occur within the ~10.8 s reference scan time.
    ++failures; ++consecutiveFailures; lastReadOK=false; lastSensorField=watchdogNow;
    Serial.println("SENSOR ERROR no real gas result for 20 s");
  }
  if(consecutiveFailures>=FAILURE_THRESHOLD) {
    sensorReady=false;
    recoveringGas=true;stopGasHeater();
    if((int32_t)(millis()-nextRecovery)>=0) recoverSensor();
    sendHealth();
  }
  else if(sawLastStep)finishGasScan();
#if !defined(_MSC_VER)
  updateOled(millis());
#endif
}
