#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>

// ============================================================
// BME688 단계별 진단
//
// 실제 검증된 배선:
// Arduino D21 -> nRF P0.31 -> SDA
// Arduino D20 -> nRF P0.29 -> SCL
//
// I2C address: 0x76
// ============================================================

#define BME_ADDR 0x76

Adafruit_BME680 bme(&Wire);

int phase = 1;
int successCount = 0;

const int PHASE1_SUCCESS_TARGET = 10;

// ------------------------------------------------------------
// BME688 Soft Reset
// 0xE0 <- 0xB6
// ------------------------------------------------------------
bool softResetBME()
{
  Wire.beginTransmission(BME_ADDR);
  Wire.write(0xE0);
  Wire.write(0xB6);

  uint8_t result = Wire.endTransmission();

  delay(20);

  return result == 0;
}

// ------------------------------------------------------------
// 센서 초기화
// ------------------------------------------------------------
bool initBME()
{
  Serial.println();
  Serial.println("================================");
  Serial.println("BME688 INITIALIZATION");
  Serial.println("================================");

  // 먼저 I2C ACK 확인
  Wire.beginTransmission(BME_ADDR);
  uint8_t ack = Wire.endTransmission();

  Serial.print("I2C ACK = ");
  Serial.println(ack);

  if (ack != 0)
  {
    Serial.println("ERROR: BME688 not responding!");
    return false;
  }

  // Soft reset
  Serial.println("Soft resetting BME688...");

  if (!softResetBME())
  {
    Serial.println("ERROR: Soft reset failed!");
    return false;
  }

  Serial.println("Soft reset OK");

  /*
    initSettings = false

    Adafruit 라이브러리의 기본
    320C / 150ms gas heater 설정을
    처음부터 켜지 않도록 한다.

    이후 아래에서 직접 설정한다.
  */
  if (!bme.begin(BME_ADDR, false))
  {
    Serial.println("ERROR: bme.begin() failed!");
    return false;
  }

  Serial.println("bme.begin() OK");

  // 원래 프로젝트와 동일한 T/H/P 설정
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);

  // ----------------------------------------------------------
  // PHASE 1에서는 Gas Heater 완전 OFF
  // ----------------------------------------------------------
  if (!bme.setGasHeater(0, 0))
  {
    Serial.println("WARNING: Failed to disable gas heater");
  }

  Serial.println();
  Serial.println("================================");
  Serial.println("PHASE 1");
  Serial.println("T / H / P ONLY");
  Serial.println("Gas heater = OFF");
  Serial.println("================================");

  phase = 1;
  successCount = 0;

  return true;
}

// ------------------------------------------------------------
// 측정값 출력
// ------------------------------------------------------------
void printReading()
{
  Serial.println("--------------------------------");

  Serial.print("Temperature : ");
  Serial.print(bme.temperature, 2);
  Serial.println(" C");

  Serial.print("Humidity    : ");
  Serial.print(bme.humidity, 2);
  Serial.println(" %");

  Serial.print("Pressure    : ");
  Serial.print(bme.pressure / 100.0, 2);
  Serial.println(" hPa");

  if (phase == 2)
  {
    Serial.print("Gas         : ");
    Serial.print(bme.gas_resistance / 1000.0, 2);
    Serial.println(" kOhm");
  }
  else
  {
    Serial.println("Gas         : DISABLED");
  }

  Serial.println("--------------------------------");
}

// ============================================================
// SETUP
// ============================================================
void setup()
{
  Serial.begin(115200);

  // Serial Monitor를 늦게 열어도 놓치지 않도록 대기
  delay(3000);

  Serial.println();
  Serial.println();
  Serial.println("################################");
  Serial.println("# BME688 DIAGNOSTIC TEST");
  Serial.println("################################");

  // 실제 검증된 핀 매핑
  Wire.setPins(21, 20);
  Wire.begin();

  if (!initBME())
  {
    Serial.println();
    Serial.println("INITIALIZATION FAILED");
    Serial.println("Diagnostic stopped.");
  }
}

// ============================================================
// LOOP
// ============================================================
void loop()
{
  // ----------------------------------------------------------
  // 실제 측정
  // ----------------------------------------------------------

  bool ok = bme.performReading();

  if (!ok)
  {
    Serial.println();
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.print("PHASE ");
    Serial.print(phase);
    Serial.println(" : performReading() FAILED!");
    Serial.println("NO STALE DATA WILL BE PRINTED");
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");

    // 중요:
    // 실패했을 때 bme.temperature 등의 이전 값을
    // 정상 측정값처럼 출력하지 않는다.

    delay(2000);
    return;
  }

  // ----------------------------------------------------------
  // 성공
  // ----------------------------------------------------------

  successCount++;

  Serial.println();
  Serial.print("PHASE ");
  Serial.print(phase);
  Serial.print(" - SUCCESS #");
  Serial.println(successCount);

  printReading();

  // ----------------------------------------------------------
  // PHASE 1 성공 10회 후 Gas Heater ON
  // ----------------------------------------------------------

  if (phase == 1 &&
      successCount >= PHASE1_SUCCESS_TARGET)
  {
    Serial.println();
    Serial.println("================================");
    Serial.println("PHASE 1 PASSED");
    Serial.println("Enabling gas heater...");
    Serial.println("320 C / 150 ms");
    Serial.println("================================");

    if (!bme.setGasHeater(320, 150))
    {
      Serial.println("ERROR: setGasHeater() failed!");
    }
    else
    {
      Serial.println("Gas heater configuration OK");
    }

    phase = 2;
    successCount = 0;

    delay(2000);

    Serial.println();
    Serial.println("================================");
    Serial.println("PHASE 2");
    Serial.println("T / H / P / GAS");
    Serial.println("Gas heater = 320 C / 150 ms");
    Serial.println("================================");
  }

  delay(2000);
}