#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLERemoteService.h>
#include <BLERemoteCharacteristic.h>

// ============================================================
// FOOD SPOILAGE MONITOR - ESP32-S3 Wi-Fi + BLE Gateway
//
// nRF52840 --(BLE NUS)--> ESP32-S3 --(Wi-Fi)--> Google Apps Script
// ============================================================


// ============================================================
// Google Apps Script
// ============================================================

const char* GOOGLE_SCRIPT_URL =
  "https://script.google.com/macros/s/AKfycbzCdFmBCyBp6oqUBu2DwYQwwJa3JCJpgfH0AIgbM99_OmwLERKe13K20F_7zehVV0mq/exec";


// ============================================================
// Wi-Fi 설정 AP (SSID 최대 32자)
// ============================================================

const char* AP_NAME     = "FoodMonitor-PW12345678";
const char* AP_PASSWORD = "12345678";

// BOOT 버튼 길게 누르면 저장된 Wi-Fi 초기화
#define WIFI_RESET_PIN 0


// ============================================================
// BLE NUS UUID
// ============================================================

static BLEUUID NUS_SERVICE_UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static BLEUUID NUS_TX_UUID     ("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");


// ============================================================
// BLE 상태
// ============================================================

BLEAdvertisedDevice*     targetDevice     = nullptr;
BLEClient*               bleClient        = nullptr;
BLERemoteCharacteristic* txCharacteristic = nullptr;

volatile bool bleReady = false;

String rxBuffer;   // BLE 수신 버퍼


// ============================================================
// 센서 데이터 (BLE 태스크 ↔ loop 공유)
// ============================================================

struct SensorData { float t, h, p, g; };

SensorData    latest;
bool          newData    = false;
unsigned long lastRxTime = 0;
portMUX_TYPE  dataMux    = portMUX_INITIALIZER_UNLOCKED;


// ============================================================
// 타이머
// ============================================================

const unsigned long UPLOAD_INTERVAL = 30000;  // 업로드 주기 (테스트 땐 줄여도 됨)
const unsigned long DATA_TIMEOUT    = 10000;  // 이 시간 동안 수신 없으면 stale
unsigned long lastUploadTime = 0;


// ============================================================
// 전방 선언
// ============================================================

bool parseSensorData(const String& data);


// ============================================================
// 센서 데이터 파싱
// ============================================================

bool parseSensorData(const String& data)
{
  SensorData d;

  if (sscanf(data.c_str(), "T=%f,H=%f,P=%f,G=%f",
             &d.t, &d.h, &d.p, &d.g) != 4)
  {
    Serial.println("Parse failed");
    return false;
  }

  unsigned long now = millis();

  portENTER_CRITICAL(&dataMux);
  latest     = d;
  lastRxTime = now;
  newData    = true;
  portEXIT_CRITICAL(&dataMux);

  Serial.printf("Parsed → T=%.2f, H=%.2f, P=%.2f, G=%.2f\n",
                d.t, d.h, d.p, d.g);
  return true;
}


// ============================================================
// BLE Notify Callback
// ============================================================

void notifyCallback(BLERemoteCharacteristic* characteristic,
                    uint8_t* data, size_t length, bool isNotify)
{
  for (size_t i = 0; i < length; i++)
  {
    char c = (char)data[i];

    if (c == '\n')                       // 줄바꿈 = 한 줄 완성
    {
      if (rxBuffer.length() > 0)
      {
        Serial.print("RX: ");
        Serial.println(rxBuffer);
        parseSensorData(rxBuffer);
        rxBuffer = "";
      }
    }
    else if (c != '\r')
    {
      rxBuffer += c;
      if (rxBuffer.length() > 200) rxBuffer = "";   // 비정상 버퍼 방지
    }
  }
}


// ============================================================
// BLE 연결 끊김 감지
// ============================================================

class MyClientCallbacks : public BLEClientCallbacks
{
  void onConnect(BLEClient* c) override {}

  void onDisconnect(BLEClient* c) override
  {
    Serial.println("\n===== BLE DISCONNECTED → rescan =====");
    bleReady = false;
    txCharacteristic = nullptr;
  }
};

MyClientCallbacks clientCallbacks;


// ============================================================
// BLE Scan Callback
// ============================================================

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice dev) override
  {
    if (targetDevice != nullptr) return;

    if (dev.haveServiceUUID() &&
        dev.isAdvertisingService(NUS_SERVICE_UUID))
    {
      Serial.println("\n===== BLE SENSOR FOUND =====");
      Serial.printf("Name: %s\n",    dev.getName().c_str());
      Serial.printf("Address: %s\n", dev.getAddress().toString().c_str());

      targetDevice = new BLEAdvertisedDevice(dev);
      BLEDevice::getScan()->stop();      // 찾으면 바로 스캔 종료
    }
  }
};

MyAdvertisedDeviceCallbacks scanCallbacks;   // 전역 1개만 사용


// ============================================================
// BLE 검색
// ============================================================

void scanForSensor()
{
  Serial.println("\nScanning for nRF52840...");

  if (targetDevice != nullptr)
  {
    delete targetDevice;
    targetDevice = nullptr;
  }

  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&scanCallbacks);
  scan->setActiveScan(true);
  scan->start(5, false);
  scan->clearResults();

  if (targetDevice == nullptr)
  {
    Serial.println("nRF52840 not found.");
  }
}


// ============================================================
// BLE 연결
// ============================================================

bool connectToSensor()
{
  if (targetDevice == nullptr) return false;

  Serial.println("\nConnecting to nRF52840...");

  // 클라이언트는 한 번만 생성해서 재사용
  if (bleClient == nullptr)
  {
    bleClient = BLEDevice::createClient();
    bleClient->setClientCallbacks(&clientCallbacks);
  }

  if (!bleClient->connect(targetDevice))
  {
    Serial.println("BLE connection FAILED");
    return false;
  }
  Serial.println("GATT CONNECTION SUCCESS!");

  BLERemoteService* svc = bleClient->getService(NUS_SERVICE_UUID);
  if (svc == nullptr)
  {
    Serial.println("NUS service NOT FOUND");
    bleClient->disconnect();
    return false;
  }

  txCharacteristic = svc->getCharacteristic(NUS_TX_UUID);
  if (txCharacteristic == nullptr || !txCharacteristic->canNotify())
  {
    Serial.println("TX characteristic NOT FOUND / cannot notify");
    bleClient->disconnect();
    return false;
  }

  txCharacteristic->registerForNotify(notifyCallback);

  bleReady = true;
  Serial.println("\n===== BLE SENSOR READY =====");
  return true;
}


// ============================================================
// Google Apps Script 업로드
// ============================================================

bool uploadToGoogle()
{
  // 최신 데이터 스냅샷 (새 데이터 + 최근 수신일 때만 업로드)
  SensorData d;
  unsigned long now = millis();
  bool fresh;

  portENTER_CRITICAL(&dataMux);
  d       = latest;
  fresh   = newData && (now - lastRxTime <= DATA_TIMEOUT);
  newData = false;
  portEXIT_CRITICAL(&dataMux);

  if (!fresh)
  {
    Serial.println("No fresh sensor data. Upload skipped.");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi disconnected. Upload skipped.");
    return false;
  }

  String json = "{";
  json += "\"temperature\":" + String(d.t, 2);
  json += ",\"humidity\":"   + String(d.h, 2);
  json += ",\"pressure\":"   + String(d.p, 2);
  json += ",\"gas\":"        + String(d.g, 2);
  json += "}";

  Serial.print("\nPOST: ");
  Serial.println(json);

  WiFiClientSecure client;
  client.setInsecure();                 // 테스트 단계: 인증서 검증 생략

  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(15000);               // Apps Script 콜드스타트 대비

  if (!http.begin(client, GOOGLE_SCRIPT_URL))
  {
    Serial.println("HTTP begin FAILED");
    return false;
  }

  http.addHeader("Content-Type", "application/json");

  int    httpCode = http.POST(json);
  String response = http.getString();
  http.end();

  Serial.printf("HTTP: %d\n", httpCode);
  Serial.print("Response: ");
  Serial.println(response);

  if (httpCode >= 200 && httpCode < 400 &&
      response.indexOf("\"success\":true") >= 0)
  {
    Serial.println("GOOGLE UPLOAD SUCCESS!");
    return true;
  }

  Serial.println("GOOGLE UPLOAD FAILED");
  return false;
}


// ============================================================
// Wi-Fi 설정 (핸드폰으로 SSID/PW 입력)
// ============================================================

void setupWiFi()
{
  Serial.println("\n================================");
  Serial.println("Wi-Fi setup");
  Serial.println("================================");

  WiFi.mode(WIFI_STA);

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);   // 3분 내 설정 안 하면 재부팅
  wm.setConnectTimeout(20);         // 저장된 Wi-Fi 접속 시도 20초

  Serial.println("Trying saved Wi-Fi...");
  Serial.println("실패 시 설정 AP 생성 → 핸드폰으로 접속해서 설정");
  Serial.printf("AP: %s / PW: %s / http://192.168.4.1\n",
                AP_NAME, AP_PASSWORD);

  if (!wm.autoConnect(AP_NAME, AP_PASSWORD))
  {
    Serial.println("Wi-Fi configuration FAILED → restart");
    delay(3000);
    ESP.restart();
  }

  Serial.println("\n================================");
  Serial.println("Wi-Fi CONNECTED!");
  Serial.println("================================");
  Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
}


// ============================================================
// BOOT 버튼 길게 → Wi-Fi 설정 초기화
// ============================================================

void checkWiFiResetButton()
{
  static unsigned long pressedAt = 0;

  if (digitalRead(WIFI_RESET_PIN) == LOW)
  {
    if (pressedAt == 0) pressedAt = millis();

    if (millis() - pressedAt > 3000)
    {
      Serial.println("\nWi-Fi 설정 초기화 → 재부팅");
      WiFiManager wm;
      wm.resetSettings();
      delay(500);
      ESP.restart();
    }
  }
  else
  {
    pressedAt = 0;
  }
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  // 네이티브 USB(CDC)일 때 시리얼 모니터 연결 대기 (최대 3초)
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(500);

  pinMode(WIFI_RESET_PIN, INPUT_PULLUP);

  Serial.println("\n\n================================");
  Serial.println(" FOOD SPOILAGE MONITOR");
  Serial.println(" ESP32-S3 GATEWAY");
  Serial.println("================================");

  setupWiFi();

  Serial.println("\nInitializing BLE...");
  BLEDevice::init("FoodMonitor-ESP32");

  scanForSensor();
  connectToSensor();
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
  checkWiFiResetButton();

  // BLE 센서 미연결 → 5초마다 재검색
  if (!bleReady)
  {
    static unsigned long lastScan = 0;

    if (millis() - lastScan > 5000)
    {
      lastScan = millis();
      scanForSensor();
      connectToSensor();
    }

    delay(10);
    return;
  }

  // Google 업로드
  if (millis() - lastUploadTime >= UPLOAD_INTERVAL)
  {
    lastUploadTime = millis();
    uploadToGoogle();
  }

  delay(10);
}

