#include <bluefruit.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>

// ============================================================
// [설정]
// ============================================================

#define DEVICE_NAME "BME688-NRF"

// nice!nano 계열 기본 I2C 핀-중요!! 반드시 올바르게 연결할 것
#define BME_SDA_PIN 31
#define BME_SCL_PIN 29

// ============================================================
// BME688
// ============================================================

Adafruit_BME680 bme;

// ============================================================
// BLE UART (Nordic UART Service)
// ============================================================

BLEUart bleuart;

// ============================================================
// 센서 데이터
// ============================================================

float temperature   = 0.0;
float humidity      = 0.0;
float pressure      = 0.0;
float gasResistance = 0.0;

bool bmeReady = false;

// ============================================================
// BLE 연결 콜백
// ============================================================

void connect_callback(uint16_t conn_handle)
{
  (void)conn_handle;

  Serial.println("BLE connected!");
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  (void)conn_handle;
  (void)reason;

  Serial.println("BLE disconnected");
}

// ============================================================
// BME688 초기화
// ============================================================

void initBME688()
{
  Serial.println();
  Serial.println("BME688 초기화 시작...");

  // nRF52840 I2C
  Wire.begin();

  // 0x76 먼저 시도
  if (!bme.begin(0x76, &Wire))
  {
    Serial.println("0x76에서 BME688을 찾지 못했습니다.");
    Serial.println("0x77 주소로 다시 시도합니다...");

    if (!bme.begin(0x77, &Wire))
    {
      Serial.println("BME688 연결 실패!");
      Serial.println("I2C 배선 / 포고핀 접촉 / 주소를 확인하세요.");

      bmeReady = false;
      return;
    }
  }

  Serial.println("BME688 연결 성공!");

  // ----------------------------------------------------------
  // 측정 설정
  // ESP32에서 정상 동작 확인했던 설정을 그대로 사용
  // ----------------------------------------------------------

  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);

  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);

  // 가스 측정
  bme.setGasHeater(320, 150);

  bmeReady = true;

  Serial.println("BME688 설정 완료!");
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("nRF52840 BME688 BLE Sensor");
  Serial.println("================================");

  // ----------------------------------------------------------
  // BME688 초기화
  // ----------------------------------------------------------

  initBME688();

  // ----------------------------------------------------------
  // BLE 초기화
  // ----------------------------------------------------------

  Bluefruit.begin();

  Bluefruit.setTxPower(4);
  Bluefruit.setName(DEVICE_NAME);

  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  bleuart.begin();

  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.Advertising.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);

  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);

  Bluefruit.Advertising.start(0);

  Serial.println("BLE advertising started!");
  Serial.print("Device name: ");
  Serial.println(DEVICE_NAME);

  if (bmeReady)
  {
    Serial.println("BME688 ready!");
  }
  else
  {
    Serial.println("BME688 unavailable - BLE only mode");
  }
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
  // ----------------------------------------------------------
  // BME688 실제 측정
  // ----------------------------------------------------------

  if (bmeReady)
  {
    if (bme.performReading())
    {
      temperature   = bme.temperature;
      humidity      = bme.humidity;
      pressure      = bme.pressure / 100.0;
      gasResistance = bme.gas_resistance / 1000.0;

      Serial.println("========================");
      Serial.print("온도: ");
      Serial.print(temperature);
      Serial.println(" °C");

      Serial.print("습도: ");
      Serial.print(humidity);
      Serial.println(" %");

      Serial.print("압력: ");
      Serial.print(pressure);
      Serial.println(" hPa");

      Serial.print("가스 저항: ");
      Serial.print(gasResistance);
      Serial.println(" kOhms");

      Serial.println("========================");
    }
    else
    {
      Serial.println("BME688 측정 실패!");
    }
  }

  // ----------------------------------------------------------
  // 데이터 문자열 생성
  // ESP32 기존 parser와 동일한 형식 유지
  // ----------------------------------------------------------

  char data[128];

  snprintf(
    data,
    sizeof(data),
    "T=%.2f,H=%.2f,P=%.2f,G=%.2f",
    temperature,
    humidity,
    pressure,
    gasResistance
  );

  // ----------------------------------------------------------
  // BLE 전송
  // ----------------------------------------------------------

  if (Bluefruit.connected())
  {
    /*
      20바이트를 넘는 데이터를 직접 분할 전송.
      기존 ESP32 코드의 parser와 호환되는 방식.
    */

    size_t len = strlen(data);

    for (size_t i = 0; i < len; i += 20)
    {
      size_t chunkSize = min(
        (size_t)20,
        len - i
      );

      bleuart.write(
        (uint8_t*)&data[i],
        chunkSize
      );

      delay(5);
    }

    // 한 줄의 끝
    bleuart.write(
      (uint8_t*)"\r\n",
      2
    );

    Serial.print("TX: ");
    Serial.println(data);
  }
  else
  {
    Serial.println("Waiting for BLE connection...");
  }

  delay(2000);
}