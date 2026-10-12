// ============================================================
// FoodSpoilageMonitor ESP32-S3 Gateway
//
// [Arduino IDE 보드 설정]
// - Board: ESP32S3 Dev Module
// - Flash Size: 16MB
// - Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS)
// - PSRAM: Disabled
// - USB CDC On Boot: Enabled
// - USB Mode: Hardware CDC and JTAG
// - Erase All Flash: Disabled
//
// [주의]
// - FATFS 파티션 스킴을 선택하면 SPIFFS를 사용할 수 없음.
// - SPIFFS에는 미전송 spool 데이터가 있으므로 포맷/전체 삭제 금지.
// - USB 포트와 COM 포트의 시리얼 출력 경로가 다를 수 있음.
// - LCD/NeoPixel은 optional 진단 UI이며 데이터 수집 경로와 분리.

//ESP32-S3 Wi-Fi 설정 접속 정보

//설정용 Wi-Fi 이름 (SSID)FoodMonitor-PW12345678-192.168.4.1


//Wi-Fi 비밀번호12345678


//설정 페이지 주소 http://192.168.4.1

//ESP32-S3의 BOOT 버튼을 3초 이상 누르면 기존 등록을 보존하고 설정 포털을 실행한다.

// ============================================================

#include <WiFi.h>

#include <Wire.h>

#include <LiquidCrystal_I2C.h>

#include <Adafruit_NeoPixel.h>

#include <WiFiClientSecure.h>

#include <HTTPClient.h>

#include <WiFiManager.h>
#include <Preferences.h>

#include <BLEDevice.h>

#include <BLEUtils.h>

#include <BLEScan.h>

#include <BLEAdvertisedDevice.h>

#include <BLEClient.h>

#include <BLERemoteService.h>

#include <BLERemoteCharacteristic.h>

#include <esp_random.h>

#include <esp_timer.h>
#include <esp_partition.h>

#include <sys/time.h>

#include <stdarg.h>

#include <cJSON.h>

#include "Integrity.h"

#include "Spool.h"

#include "GatewayUi.h"



const char* GOOGLE_SCRIPT_URL =

  "https://script.google.com/macros/s/AKfycbyrDRFYcw6KKLK2fae04ZW1nxsuBVlW9LuAaUawLq3Cxim1Qri7uZe5AEzsPwf3txLd/exec";

const char* AP_NAME     = "FoodMonitor-PW12345678";

const char* AP_PASSWORD = "12345678";



using namespace integrity;

using namespace gateway_ui;

constexpr uint32_t UPLOAD_INTERVAL_MS=5000, OUTAGE_FLUSH_MS=60000;

constexpr uint32_t MAX_RETRY_MS=30000, RATE_LIMIT_RETRY_MS=150000, PERSIST_INFLIGHT_MS=10000;

constexpr uint8_t WIFI_RESET_PIN=0;
constexpr uint32_t WIFI_CONNECT_MS=20000, WIFI_RETRY_MS=30000;

constexpr uint8_t LCD_SDA_PIN=8, LCD_SCL_PIN=9, LCD_ADDRESS=0x27;

constexpr uint8_t NEOPIXEL_PIN=48, NEOPIXEL_BRIGHTNESS=24;

constexpr uint32_t LCD_REFRESH_MS=750;

constexpr bool UART_DIAGNOSTICS=true;

BLEUUID NUS_SERVICE_UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");

BLEUUID NUS_TX_UUID("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");

QueueHandle_t samplesQueue, jobsQueue, resultsQueue;

SemaphoreHandle_t logMutex;

portMUX_TYPE stateMux=portMUX_INITIALIZER_UNLOCKED;

Framer framer;

Continuity continuity;
EnvironmentState environmentState;

Health sensorHealth={};

Sample latest={};

bool haveLatest=false, haveHealth=false, sensorOK=false;

volatile bool bleConnected=false;

uint32_t rxSequence=0, parseErrors=0, queueDrops=0;

char gatewayBoot[33]={};

OutageSpool spool;

BLEAdvertisedDevice* targetDevice=nullptr;

BLEClient* bleClient=nullptr;

WiFiManager wifiManager;
Preferences wifiPreferences;
struct SavedWifi { char ssid[33]; char password[65]; };
SavedWifi wifiNetworks[2]={};
bool wifiPreferencesReady=false, wifiTrying=false;
uint8_t wifiNetworkCount=0, wifiNextSlot=0, wifiTried=0;
int8_t wifiActiveSlot=-1;
uint64_t wifiAttemptAt=0, wifiRetryAt=0;

LiquidCrystal_I2C lcd(LCD_ADDRESS,16,2);

Adafruit_NeoPixel gatewayPixel(1,NEOPIXEL_PIN,NEO_GRB+NEO_KHZ800);

bool lcdReady=false;

LcdMode lcdMode=LCD_DEBUG;

ActiveLowButton bootButton;

PixelState shownPixel=PIXEL_BOOT;

uint64_t lastLcdRefresh=0;

char shownLcd[2][LCD_COLUMNS+1]={{0},{0}};

uint32_t uploadFailures=0, unassignedCount=0;

int64_t lastCloudUpload=0;



struct UploadJob { Sample samples[BATCH_SIZE]; uint32_t count; char* json; };

struct UploadResult { bool ok; int http; uint32_t unassigned; char recording[16]; char session[64]; };
uint64_t wifiConnectedAt=0, bleConnectedAt=0, firstFrameAt=0;
uint64_t firstPostAt=0, lastPostAt=0, lastRedirectAt=0, lastAckAt=0, backlogDrainedAt=0;



uint64_t uptimeMs(){return esp_timer_get_time()/1000ULL;}

int64_t utcMs(){struct timeval tv;gettimeofday(&tv,nullptr);return (int64_t)tv.tv_sec*1000+tv.tv_usec/1000;}

bool clockValid(){return utcMs()>1767225600000LL;}

void logEvent(const char* format,...) {

  char line[240];va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);

  if(logMutex)xSemaphoreTake(logMutex,portMAX_DELAY);

  if(Serial)Serial.println(line);

  if(UART_DIAGNOSTICS)Serial0.println(line);

  if(logMutex)xSemaphoreGive(logMutex);

}

bool saveWifiNetwork(const String& ssid,const String& password) {
  if(ssid.isEmpty()||ssid.length()>32||password.length()>64)return false;
  int slot=-1;
  for(uint8_t i=0;i<wifiNetworkCount;++i)if(ssid==wifiNetworks[i].ssid){slot=i;break;}
  if(slot<0)slot=wifiNetworkCount<2?wifiNetworkCount:(wifiActiveSlot==0?1:0);
  SavedWifi entry={};ssid.toCharArray(entry.ssid,sizeof(entry.ssid));
  password.toCharArray(entry.password,sizeof(entry.password));
  const char* key=slot==0?"ap0":"ap1";
  if(!wifiPreferencesReady||wifiPreferences.putBytes(key,&entry,sizeof(entry))!=sizeof(entry)){
    logEvent("ERROR Wi-Fi credential storage failed; previous registration preserved");return false;
  }
  wifiNetworks[slot]=entry;
  if(slot==wifiNetworkCount)++wifiNetworkCount;
  wifiActiveSlot=slot;wifiNextSlot=slot;wifiTrying=false;wifiTried=0;
  logEvent("Wi-Fi network registered slot=%d count=%u",slot,(unsigned)wifiNetworkCount);
  return true;
}

void loadWifiNetworks() {
  wifiPreferencesReady=wifiPreferences.begin("fsm-wifi",false);
  if(!wifiPreferencesReady)logEvent("ERROR Wi-Fi credential storage unavailable");
  for(uint8_t i=0;wifiPreferencesReady&&i<2;++i){
    const char* key=i==0?"ap0":"ap1";
    SavedWifi entry={};
    if(wifiPreferences.getBytesLength(key)!=sizeof(entry)||
       wifiPreferences.getBytes(key,&entry,sizeof(entry))!=sizeof(entry)||
       !memchr(entry.ssid,0,sizeof(entry.ssid))||
       !memchr(entry.password,0,sizeof(entry.password))||!entry.ssid[0])break;
    wifiNetworks[wifiNetworkCount++]=entry;
  }
  // Migrate the one legacy WiFiManager/ESP32 STA credential before any begin().
  String oldSsid=wifiManager.getWiFiSSID(true);
  if(!oldSsid.isEmpty()){
    bool found=false;
    for(uint8_t i=0;i<wifiNetworkCount;++i)if(oldSsid==wifiNetworks[i].ssid)found=true;
    if(!found&&wifiNetworkCount<2){
      String oldPassword=wifiManager.getWiFiPass(true);
      if(!saveWifiNetwork(oldSsid,oldPassword)&&oldSsid.length()<=32&&oldPassword.length()<=64){
        SavedWifi entry={};oldSsid.toCharArray(entry.ssid,sizeof(entry.ssid));
        oldPassword.toCharArray(entry.password,sizeof(entry.password));
        wifiNetworks[wifiNetworkCount++]=entry;
        logEvent("Wi-Fi legacy network retained in RAM only; reboot persistence unavailable");
      }
    }
  }
  wifiActiveSlot=-1;
  logEvent("Wi-Fi saved networks=%u",(unsigned)wifiNetworkCount);
}

void startWifiPortal() {
  if(wifiManager.getConfigPortalActive())return;
  // In nonblocking mode startConfigPortal() returns false even when active.
  wifiManager.startConfigPortal(AP_NAME,AP_PASSWORD);
  if(!wifiManager.getConfigPortalActive())logEvent("ERROR Wi-Fi configuration portal inactive after start");
  else if(!(WiFi.getMode()&WIFI_AP))logEvent("ERROR Wi-Fi portal active without AP mode");
  else logEvent("Wi-Fi configuration portal active (AP mode)");
}

void serviceWifi(uint64_t now) {
  if(WiFi.status()==WL_CONNECTED){
    wifiTrying=false;wifiTried=0;
    for(uint8_t i=0;i<wifiNetworkCount;++i)if(WiFi.SSID()==wifiNetworks[i].ssid)wifiActiveSlot=i;
    return; // Never scan or switch a healthy connection.
  }
  if(wifiManager.getConfigPortalActive())return; // Portal owns STA while configuring.
  if(wifiTrying){
    if(now-wifiAttemptAt<WIFI_CONNECT_MS)return;
    logEvent("Wi-Fi connect timeout slot=%u",(unsigned)wifiNextSlot);
    wifiTrying=false;++wifiTried;
    wifiNextSlot=(wifiNextSlot+1)%wifiNetworkCount;
    if(wifiTried>=wifiNetworkCount){
      wifiRetryAt=now+WIFI_RETRY_MS;wifiTried=0;
      logEvent("Wi-Fi networks unavailable; opening configuration portal");
      startWifiPortal();
      return;
    }
  }
  if(!wifiNetworkCount){
    if(now>=wifiRetryAt){
      startWifiPortal();
      wifiRetryAt=now+WIFI_RETRY_MS;
    }
    return;
  }
  if(now<wifiRetryAt)return;
  WiFi.begin(wifiNetworks[wifiNextSlot].ssid,wifiNetworks[wifiNextSlot].password);
  wifiAttemptAt=now;wifiTrying=true;
  logEvent("Wi-Fi connect attempt slot=%u",(unsigned)wifiNextSlot);
}

void initGatewayPixel() {

  gatewayPixel.begin();gatewayPixel.setBrightness(NEOPIXEL_BRIGHTNESS);

  gatewayPixel.setPixelColor(0,gatewayPixel.Color(0,0,255));gatewayPixel.show();

  shownPixel=PIXEL_BOOT;logEvent("NeoPixel init GPIO=%u brightness=%u state=BOOT/BLUE",NEOPIXEL_PIN,NEOPIXEL_BRIGHTNESS);

}

void setGatewayPixel(PixelState state) {

  if(state==shownPixel)return;

  uint32_t color=0;

  if(state==PIXEL_BLE_OFF)color=gatewayPixel.Color(255,0,0);

  else if(state==PIXEL_WIFI_OFF)color=gatewayPixel.Color(160,0,255);

  else if(state==PIXEL_WAIT)color=gatewayPixel.Color(255,160,0);

  else if(state==PIXEL_OK)color=gatewayPixel.Color(0,255,0);

  else color=gatewayPixel.Color(0,0,255);

  gatewayPixel.setPixelColor(0,color);gatewayPixel.show();shownPixel=state;

}

void writeLcdLine(uint8_t row,const char line[LCD_COLUMNS+1]) {

  if(!lcdReady||row>1||!strncmp(shownLcd[row],line,LCD_COLUMNS))return;

  lcd.setCursor(0,row);lcd.print(line);memcpy(shownLcd[row],line,LCD_COLUMNS+1);

}

void initGatewayLcd() {

  logEvent("I2C pins SDA=%u SCL=%u",LCD_SDA_PIN,LCD_SCL_PIN);

  Wire.begin(LCD_SDA_PIN,LCD_SCL_PIN);

  bool expectedAddressFound=false;

  for(uint8_t address=1;address<127;++address) {

    Wire.beginTransmission(address);uint8_t error=Wire.endTransmission();

    if(error==0){logEvent("I2C device found: 0x%02X",address);if(address==LCD_ADDRESS)expectedAddressFound=true;}

  }

  if(!expectedAddressFound){logEvent("LCD init: unavailable (expected 0x%02X); gateway continues",LCD_ADDRESS);return;}

  lcd.init();lcd.backlight();lcdReady=true;

  char line1[LCD_COLUMNS+1],line2[LCD_COLUMNS+1];fixedLine(line1,"Gateway starting");fixedLine(line2,"MODE DEBUG");

  writeLcdLine(0,line1);writeLcdLine(1,line2);

  logEvent("LCD init: OK address=0x%02X 16x2",LCD_ADDRESS);logEvent("LCD MODE DEBUG");

}

void updateGatewayUi(uint64_t now,bool wifi,CloudState cloud,uint32_t queueDepth,

                     uint32_t spoolDepth,bool recordingKnown,bool recording,bool backlog) {

  bool ble;Sample sample;bool sampleKnown;

  portENTER_CRITICAL(&stateMux);ble=bleConnected;sample=latest;sampleKnown=haveLatest;portEXIT_CRITICAL(&stateMux);

  setGatewayPixel(pixelState(ble,wifi,cloud,backlog));

  if(!lcdReady||now-lastLcdRefresh<LCD_REFRESH_MS)return;

  lastLcdRefresh=now;char line1[LCD_COLUMNS+1],line2[LCD_COLUMNS+1];

  if(lcdMode==LCD_SENSOR)formatSensor(sampleKnown,sample.t,sample.h,sample.p,sample.g,line1,line2);

  else formatDebug(ble,wifi,cloud,queueDepth,spoolDepth,recordingKnown,recording,line1,line2);

  writeLcdLine(0,line1);writeLcdLine(1,line2);

}

String uid(const Sample& s){char b[33];memcpy(b,s.boot,32);b[32]=0;return String(b)+"-"+String(s.rx);}

void addNumber(cJSON* o,const char* k,double n){cJSON_AddNumberToObject(o,k,n);}



void receiveLine(const char* line) {

  if(!strncmp(line,"STAT,",5)) {

    Health h;

    if(!parseHealth(line,h)){portENTER_CRITICAL(&stateMux);++parseErrors;portEXIT_CRITICAL(&stateMux);return;}

    portENTER_CRITICAL(&stateMux);sensorHealth=h;haveHealth=true;sensorOK=h.ok!=0;portEXIT_CRITICAL(&stateMux);

    logEvent("SENSOR metadata ok=%u fail=%lu recover=%lu",h.ok,(unsigned long)h.failures,(unsigned long)h.recoveries);

    return;

  }

  Frame f;

  if(!parseFrame(line,f)) {

    portENTER_CRITICAL(&stateMux);++parseErrors;portEXIT_CRITICAL(&stateMux);

    logEvent("ERROR malformed BLE frame rejected");return;

  }

  if(f.version==4) {
    const uint64_t received=uptimeMs();
    Sample env={};bool historyDue=false,accepted=false;
    portENTER_CRITICAL(&stateMux);
    accepted=environmentState.accept(f,received,historyDue);
    if(accepted) {
      if(sensorHealth.mode==1)sensorOK=true;
      if(historyDue && rxSequence!=0xffffffffu) {
        env.rx=++rxSequence;
      } else historyDue=false;
    }
    portEXIT_CRITICAL(&stateMux);
    if(!accepted)return;
    if(!firstFrameAt){firstFrameAt=received;logEvent("MILESTONE first valid BLE frame uptime=%llu ms",(unsigned long long)received);}
    if(!historyDue)return; // 2-second telemetry never floods RAM/SPIFFS
    memcpy(env.boot,gatewayBoot,32);env.nrfBoot=f.boot;env.seq=f.seq;env.ms=f.ms;
    env.schema=4;env.receivedUptime=received;
    env.timeQuality=clockValid()?1:0;env.capturedAt=env.timeQuality?utcMs():0;
    env.t=f.t;env.h=f.h;env.p=f.p; // g/STEP are absent on BLE and JSON
    seal(env);
    if(xQueueSend(samplesQueue,&env,0)!=pdTRUE) {
      portENTER_CRITICAL(&stateMux);++queueDrops;environmentState.historyRejected();portEXIT_CRITICAL(&stateMux);
      logEvent("DATA LOSS environment history queue full; UID=%s rejected",uid(env).c_str());
    }
    return;
  }

  Sample s={};memcpy(s.boot,gatewayBoot,32);s.nrfBoot=f.boot;s.seq=f.seq;s.ms=f.ms;
  s.schema=f.version;
  if(f.version==3) {s.scan=f.scan;s.step=f.step;s.gasIndex=f.gasIndex;
    s.measIndex=f.measIndex;s.sensorStatus=f.status;s.profile=354;}

  s.receivedUptime=uptimeMs();s.timeQuality=clockValid()?1:0;s.capturedAt=s.timeQuality?utcMs():0;

  s.t=f.t;s.h=f.h;s.p=f.p;s.g=f.g;

  portENTER_CRITICAL(&stateMux);

  uint32_t before=continuity.gaps;

  bool accept=continuity.accept(f);

  if(accept && rxSequence!=0xffffffffu) {s.rx=++rxSequence;latest=s;haveLatest=true;sensorOK=true;}

  else if(accept) {accept=false;++queueDrops;}

  uint32_t gaps=continuity.gaps;

  portEXIT_CRITICAL(&stateMux);

  if(!accept){logEvent("ERROR duplicate/backward sequence or UID exhaustion");return;}

  if(!firstFrameAt){firstFrameAt=uptimeMs();logEvent("MILESTONE first valid BLE frame uptime=%llu ms",(unsigned long long)firstFrameAt);}

  seal(s);

  if(xQueueSend(samplesQueue,&s,0)!=pdTRUE) {

    portENTER_CRITICAL(&stateMux);++queueDrops;portEXIT_CRITICAL(&stateMux);

    logEvent("DATA LOSS RAM queue full; newest UID=%s rejected",uid(s).c_str());

  }

  if(gaps!=before)logEvent("BLE sequence gap total=%lu",(unsigned long)gaps);

  logEvent("RX seq=%lu queue=%u",(unsigned long)s.seq,(unsigned)uxQueueMessagesWaiting(samplesQueue));

}

void notifyCallback(BLERemoteCharacteristic*,uint8_t* data,size_t length,bool) {

  for(size_t i=0;i<length;++i) {

    char complete[MAX_FRAME+1];

    portENTER_CRITICAL(&stateMux);

    int result=framer.feed((char)data[i]);

    if(result==1)strcpy(complete,framer.line);

    if(result<0)++parseErrors;

    portEXIT_CRITICAL(&stateMux);

    if(result==1)receiveLine(complete);

    if(result<0)logEvent("ERROR BLE overflow/control byte; discard until newline");

  }

}

class ClientCallbacks:public BLEClientCallbacks {

  void onConnect(BLEClient*)override{}

  void onDisconnect(BLEClient*)override{

    portENTER_CRITICAL(&stateMux);bleConnected=false;framer.reset();portEXIT_CRITICAL(&stateMux);

    logEvent("BLE DISCONNECTED; partial frame cleared");

  }

};

ClientCallbacks clientCallbacks;

class ScanCallbacks:public BLEAdvertisedDeviceCallbacks {

  void onResult(BLEAdvertisedDevice dev)override {

    if(targetDevice)return;

    if(dev.haveServiceUUID()&&dev.isAdvertisingService(NUS_SERVICE_UUID)) {

      targetDevice=new BLEAdvertisedDevice(dev);BLEDevice::getScan()->stop();

    }

  }

};

ScanCallbacks scanCallbacks;

void bleTask(void*) {

  BLEDevice::init("FoodMonitor-ESP32");

  bleClient=BLEDevice::createClient();bleClient->setClientCallbacks(&clientCallbacks);

  while(true) {

    if(!bleConnected) {

      delete targetDevice;targetDevice=nullptr;

      logEvent("BLE scan");

      BLEScan* scan=BLEDevice::getScan();scan->setAdvertisedDeviceCallbacks(&scanCallbacks);scan->setActiveScan(true);

      scan->start(3,false);scan->clearResults();

      if(targetDevice && bleClient->connect(targetDevice)) {

        BLERemoteService* service=bleClient->getService(NUS_SERVICE_UUID);

        BLERemoteCharacteristic* tx=service?service->getCharacteristic(NUS_TX_UUID):nullptr;

        if(tx&&tx->canNotify()) {

          portENTER_CRITICAL(&stateMux);framer.reset();portEXIT_CRITICAL(&stateMux);

          tx->registerForNotify(notifyCallback);bleConnected=bleClient->isConnected();

          if(!bleConnectedAt)bleConnectedAt=uptimeMs();
          logEvent("BLE connected and subscribed uptime=%llu ms",(unsigned long long)uptimeMs());

        } else bleClient->disconnect();

      }

    }

    vTaskDelay(pdMS_TO_TICKS(1000));

  }

}



cJSON* sampleJson(const Sample& s) {

  cJSON* o=cJSON_CreateObject();cJSON_AddStringToObject(o,"uid",uid(s).c_str());

  addNumber(o,"seq",s.seq);addNumber(o,"ms",s.ms);addNumber(o,"nrf_boot",s.nrfBoot);

  addNumber(o,"rx_ms",s.receivedUptime);

  if(s.timeQuality)addNumber(o,"captured_at",s.capturedAt);else cJSON_AddNullToObject(o,"captured_at");

  cJSON_AddStringToObject(o,"quality",s.timeQuality?"ntp":"unknown");

  addNumber(o,"t",s.t);addNumber(o,"h",s.h);addNumber(o,"p",s.p);
  if(s.schema!=4)addNumber(o,"g",s.g);
  addNumber(o,"sample_schema",s.schema);
  if(s.schema==3) {
    addNumber(o,"scan_seq",s.scan);addNumber(o,"heater_step",s.step);
    addNumber(o,"gas_index",s.gasIndex);addNumber(o,"meas_index",s.measIndex);
    addNumber(o,"sensor_status",s.sensorStatus);addNumber(o,"profile_id",s.profile);
    cJSON_AddBoolToObject(o,"gas_valid",(s.sensorStatus&0x20)!=0);
    cJSON_AddBoolToObject(o,"heater_stable",(s.sensorStatus&0x10)!=0);
    cJSON_AddBoolToObject(o,"measurement_valid",true);
  }
  return o;

}

cJSON* healthJson(uint32_t activeCount,uint32_t stagedCount) {

  Sample s;Health h;Continuity cont;bool has,known,ok;uint32_t errors,drops;
  Frame env;uint64_t envAt;bool hasEnv;

  portENTER_CRITICAL(&stateMux);s=latest;h=sensorHealth;cont=continuity;has=haveLatest;known=haveHealth;
  env=environmentState.latest;envAt=environmentState.receivedAt;hasEnv=environmentState.known;

  ok=sensorOK;errors=parseErrors;drops=queueDrops;portEXIT_CRITICAL(&stateMux);

  cJSON* o=cJSON_CreateObject();

  cJSON_AddBoolToObject(o,"ble_connected",bleConnected);

  if(known||has)cJSON_AddBoolToObject(o,"sensor_ok",ok);

  if(has){addNumber(o,"last_valid_age_ms",uptimeMs()-s.receivedUptime);cJSON* v=cJSON_CreateObject();

    addNumber(v,"t",s.t);addNumber(v,"h",s.h);addNumber(v,"p",s.p);addNumber(v,"g",s.g);cJSON_AddItemToObject(o,"latest",v);}
  const char* mode=h.mode==1?"ENV":h.mode==2?"GAS":h.mode==3?"COOL":h.mode==4?"REC":"LEGACY";
  cJSON_AddStringToObject(o,"sensor_mode",mode);
  if(known&&h.mode){cJSON_AddBoolToObject(o,"environment_ok",h.envOK!=0);
    cJSON_AddBoolToObject(o,"gas_ok",h.gasOK!=0);}
  if(hasEnv){cJSON* v=cJSON_CreateObject();addNumber(v,"t",env.t);addNumber(v,"h",env.h);
    addNumber(v,"p",env.p);addNumber(v,"age_ms",uptimeMs()-envAt);
    cJSON_AddItemToObject(o,"environment",v);}

  addNumber(o,"nrf_seq",cont.seq);addNumber(o,"queue_depth",uxQueueMessagesWaiting(samplesQueue)+activeCount+stagedCount);

  addNumber(o,"spool_depth",spool.count);addNumber(o,"sensor_failures",h.failures);addNumber(o,"sensor_recoveries",h.recoveries);

  addNumber(o,"sensor_attempts",h.attempts);addNumber(o,"ble_gaps",cont.gaps);addNumber(o,"ble_restarts",cont.restarts);

  addNumber(o,"parse_errors",errors);addNumber(o,"queue_drops",drops);addNumber(o,"upload_failures",uploadFailures);

  addNumber(o,"last_upload_at",lastCloudUpload);addNumber(o,"unassigned_count",unassignedCount);
  addNumber(o,"gateway_uptime_ms",uptimeMs());
  addNumber(o,"wifi_connected_ms",wifiConnectedAt);addNumber(o,"ble_connected_ms",bleConnectedAt);
  addNumber(o,"first_frame_ms",firstFrameAt);addNumber(o,"first_post_ms",firstPostAt);
  addNumber(o,"last_post_ms",lastPostAt);addNumber(o,"last_redirect_ms",lastRedirectAt);
  addNumber(o,"last_ack_ms",lastAckAt);addNumber(o,"backlog_drained_ms",backlogDrainedAt);

  addNumber(o,"spool_capacity_bytes",spool.budget);addNumber(o,"spool_used_bytes",spool.bytes);

  cJSON_AddBoolToObject(o,"spool_error",spool.fault);cJSON_AddBoolToObject(o,"spool_full",spool.full);return o;

}

bool validAck(const char* response,const UploadJob& job,UploadResult& result) {

  const char* end=nullptr;cJSON* root=cJSON_ParseWithOpts(response,&end,true);

  if(!root)return false;

  cJSON* acks=cJSON_GetObjectItemCaseSensitive(root,"acks");

  cJSON* version=cJSON_GetObjectItemCaseSensitive(root,"version");

  bool ok=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"success"))&&cJSON_IsNumber(version)&&version->valuedouble==3&&

    cJSON_IsArray(acks)&&cJSON_GetArraySize(acks)==(int)job.count;

  for(uint32_t i=0;ok&&i<job.count;++i) {

    cJSON* a=cJSON_GetArrayItem(acks,i);cJSON* u=cJSON_GetObjectItemCaseSensitive(a,"uid");cJSON* st=cJSON_GetObjectItemCaseSensitive(a,"status");

    ok=cJSON_IsString(u)&&uid(job.samples[i])==u->valuestring&&cJSON_IsString(st);

    if(ok){String status(st->valuestring);ok=status=="stored"||status=="duplicate"||status=="not_recording"||status=="unassigned_time";

      if(status=="unassigned_time")++result.unassigned;}

  }

  cJSON* rec=cJSON_GetObjectItemCaseSensitive(root,"recording");

  cJSON* state=cJSON_GetObjectItemCaseSensitive(rec,"state");cJSON* session=cJSON_GetObjectItemCaseSensitive(rec,"session_id");

  if(cJSON_IsString(state))snprintf(result.recording,sizeof(result.recording),"%s",state->valuestring);

  if(cJSON_IsString(session))snprintf(result.session,sizeof(result.session),"%s",session->valuestring);

  cJSON_Delete(root);return ok;

}

void networkTask(void*) {

  UploadJob* job;



  while (true) {

    if (xQueueReceive(jobsQueue, &job, portMAX_DELAY) != pdTRUE)

      continue;



    UploadResult result = {};

    result.http = -1;



    if (WiFi.status() == WL_CONNECTED) {



      String redirectUrl;

      int postCode = -1;



      // ========================================================

      // 1단계: Apps Script에 POST

      //

      // 반드시 별도 scope에서 실행한다.

      // 이 scope가 끝나면 POST용 HTTP/TLS 객체가 완전히 파괴된다.

      // ========================================================

      {

        WiFiClientSecure postClient;

        postClient.setInsecure();



        HTTPClient post;

        post.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

        post.setConnectTimeout(5000);

        post.setTimeout(15000);

        post.setReuse(false);



        if (post.begin(postClient, GOOGLE_SCRIPT_URL)) {



          post.addHeader("Content-Type", "application/json");



          if(!firstPostAt)firstPostAt=uptimeMs();
          lastPostAt=uptimeMs();
          logEvent("HTTP POST begin uptime=%llu ms bytes=%u",(unsigned long long)lastPostAt,(unsigned)strlen(job->json));
          postCode =

            post.POST(

              (uint8_t*)job->json,

              strlen(job->json)

            );



          result.http = postCode;
          logEvent("HTTP POST result=%d elapsed=%llu ms uptime=%llu ms",postCode,
                   (unsigned long long)(uptimeMs()-lastPostAt),(unsigned long long)uptimeMs());



          // Google Apps Script ContentService:

          // POST 처리 후 302/303으로 실제 응답 위치를 알려준다.

          if (postCode == HTTP_CODE_FOUND ||

              postCode == HTTP_CODE_SEE_OTHER) {



            // end() 전에 반드시 복사

            redirectUrl = post.getLocation();



            logEvent(

              "HTTP POST %d redirect=%u len=%u heap=%lu",

              postCode,

              redirectUrl.length() ? 1 : 0,

              (unsigned)redirectUrl.length(),

              (unsigned long)ESP.getFreeHeap()

            );



          } else if (postCode >= 200 &&

                     postCode < 300 &&

                     post.getSize() <= 16000) {



            // 향후 Google이 redirect 없이 직접 응답할 경우

            String response = post.getString();



            // ACK 진단용: Google Apps Script가 실제로 반환한 JSON 확인

            logEvent(

              "ACK BODY len=%u %.180s",

              (unsigned)response.length(),

              response.c_str()

            );



            if (response.length() <= 16000) {

              result.ok =

              validAck(

              response.c_str(),

              *job,

              result

            );

          }



          } else if (postCode < 0) {



            logEvent(

              "HTTP POST error=%s",

              HTTPClient::errorToString(postCode).c_str()

            );

          }



          post.end();

        } else {

          logEvent("HTTP POST begin failed");

        }



        // 명시적으로 TLS socket 종료

        postClient.stop();

      }



      // 여기 도달했을 때 post/postClient는 완전히 소멸한 상태.

      // 두 HTTPS 연결을 연속으로 너무 바짝 만들지 않는다.

      delay(100);



      // ========================================================

      // 2단계: Google redirect 응답을 새 TLS 연결로 GET

      // ========================================================

      if ((postCode == HTTP_CODE_FOUND ||

           postCode == HTTP_CODE_SEE_OTHER) &&

          redirectUrl.length()) {

        lastRedirectAt=uptimeMs();



        logEvent(

          "HTTP redirect GET begin uptime=%llu ms heap=%lu",

          (unsigned long long)lastRedirectAt,

          (unsigned long)ESP.getFreeHeap()

        );



        {

          WiFiClientSecure getClient;

          getClient.setInsecure();



          HTTPClient get;

          get.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

          get.setConnectTimeout(10000);

          get.setTimeout(15000);

          get.setReuse(false);



          if (get.begin(getClient, redirectUrl)) {



            result.http = get.GET();
            logEvent("HTTP redirect GET result=%d elapsed=%llu ms uptime=%llu ms",
                     result.http,(unsigned long long)(uptimeMs()-lastRedirectAt),
                     (unsigned long long)uptimeMs());



            if (result.http < 0) {



              logEvent(

                "HTTP redirect GET error=%d %s heap=%lu",

                result.http,

                HTTPClient::errorToString(result.http).c_str(),

                (unsigned long)ESP.getFreeHeap()

              );



            } else {



              logEvent(

                "HTTP redirect GET status=%d size=%d",

                result.http,

                get.getSize()

              );



              if (result.http >= 200 &&
                  result.http < 300) {

                // Content-Length가 없으면 getSize() == -1일 수 있으므로
                // 실제 응답 본문을 먼저 읽는다.
                String response = get.getString();

                // Apps Script가 실제 반환한 ACK JSON 진단
                logEvent(
                  "ACK BODY len=%u %.300s",
                  (unsigned)response.length(),
                  response.c_str()
                );

                if (response.length() > 0 &&
                    response.length() <= 16000) {

                  result.ok =
                    validAck(
                      response.c_str(),
                      *job,
                      result
                    );

                  logEvent(
                    "ACK PARSE result=%u",
                    result.ok ? 1 : 0
                  );

                } else {

                  logEvent(
                    "ACK BODY invalid length=%u",
                    (unsigned)response.length()
                  );
                }
              }

            }



            get.end();



          } else {

            result.http = -1;

            logEvent("HTTP redirect GET begin failed");

          }



          getClient.stop();

        }

      }

    }



    logEvent(

      "HTTP %d application ACK=%u samples=%lu",

      result.http,

      result.ok,

      (unsigned long)job->count

    );



    cJSON_free(job->json);

    delete job;



    xQueueSend(

      resultsQueue,

      &result,

      portMAX_DELAY

    );

  }

}

uint32_t drain(Sample* dest) {

  uint32_t n=0;while(n<BATCH_SIZE&&xQueueReceive(samplesQueue,&dest[n],0)==pdTRUE)++n;return n;

}

void setup() {

  Serial.begin(115200);if(UART_DIAGNOSTICS)Serial0.begin(115200);

  logMutex=xSemaphoreCreateMutex();samplesQueue=xQueueCreate(RAM_SAMPLES,sizeof(Sample));

  jobsQueue=xQueueCreate(1,sizeof(UploadJob*));resultsQueue=xQueueCreate(1,sizeof(UploadResult));

  if(!logMutex||!samplesQueue||!jobsQueue||!resultsQueue){Serial0.println("FATAL allocation failed");while(true)delay(1000);}

  pinMode(WIFI_RESET_PIN,INPUT_PULLUP);WiFi.mode(WIFI_STA);

  initGatewayPixel();initGatewayLcd();

  uint32_t random[4];esp_fill_random(random,sizeof(random));

  snprintf(gatewayBoot,sizeof(gatewayBoot),"%08lx%08lx%08lx%08lx",(unsigned long)random[0],(unsigned long)random[1],(unsigned long)random[2],(unsigned long)random[3]);

  logEvent("BOOT gas-scan-v3 gateway=%s uptime=%llu ms",gatewayBoot,(unsigned long long)uptimeMs());
  logEvent("FIRMWARE build=spiffs-diag-20261012-r1 compiled=%s %s flash_chip=%lu",
    __DATE__,__TIME__,(unsigned long)ESP.getFlashChipSize());
  const esp_partition_t* spiffsPartition=esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,nullptr);
  if(spiffsPartition)logEvent("SPIFFS partition offset=0x%lx size=0x%lx",
    (unsigned long)spiffsPartition->address,(unsigned long)spiffsPartition->size);
  else logEvent("ERROR SPIFFS partition missing");

  spool.begin();
  logEvent("SPIFFS mount %s",spool.mounted?"success":"failed");

  logEvent("SPOOL total=%u budget=%u pending=%lu fault=%u (never auto-format)",

    (unsigned)(spool.mounted?SPIFFS.totalBytes():0),(unsigned)spool.budget,(unsigned long)spool.count,spool.fault);

  if(xTaskCreate(bleTask,"ble",8192,nullptr,1,nullptr)!=pdPASS || xTaskCreate(networkTask,"upload",16384,nullptr,1,nullptr)!=pdPASS) {

    logEvent("FATAL task allocation failed");while(true)delay(1000);

  }

  wifiManager.setConfigPortalBlocking(false);wifiManager.setConfigPortalTimeout(180);wifiManager.setConnectTimeout(20);
  wifiManager.setDebugOutput(false); // never expose entered credentials through library diagnostics

  wifiManager.setSaveConfigCallback([](){
    if(!saveWifiNetwork(wifiManager.getWiFiSSID(true),wifiManager.getWiFiPass(true)))
      logEvent("ERROR Wi-Fi portal registration not retained");
  });
  loadWifiNetworks();WiFi.persistent(false);WiFi.setAutoReconnect(false);
  serviceWifi(uptimeMs());

  configTime(0,0,"pool.ntp.org","time.google.com");

}

void loop() {

  static PendingBatch pending;

  Sample* active=pending.samples;

  uint32_t& activeCount=pending.count;

  static Sample staging[BATCH_SIZE];

  static uint32_t stagedCount=0;

  static String activePath;

  static bool busy=false, failed=false, haveCloudAck=false;

  static bool recordingKnown=false, recording=false;

  static uint64_t due=0, activeSince=0, lastSpill=0;

  static uint32_t retryDelay=UPLOAD_INTERVAL_MS;

  static int wifiPrevious=-1;

  uint64_t now=uptimeMs();wifiManager.process();

  int wifiState=WiFi.status();

  if(wifiState!=wifiPrevious){
    logEvent("Wi-Fi state=%d uptime=%llu ms",wifiState,(unsigned long long)now);
    if(wifiState==WL_CONNECTED){wifiConnectedAt=now;due=now;retryDelay=UPLOAD_INTERVAL_MS;}
    wifiPrevious=wifiState;
  }

  serviceWifi(now);

  ButtonEvents button=bootButton.update(digitalRead(WIFI_RESET_PIN)==HIGH,now);

  if(button.shortPress){lcdMode=lcdMode==LCD_DEBUG?LCD_SENSOR:LCD_DEBUG;

    logEvent(lcdMode==LCD_SENSOR?"LCD MODE SENSOR":"LCD MODE DEBUG");lastLcdRefresh=0;}

  if(button.longPress){logEvent("Wi-Fi registration portal; saved networks and spool preserved");
    wifiTrying=false;

    startWifiPortal();}

  UploadResult result;

  if(xQueueReceive(resultsQueue,&result,0)==pdTRUE) {

    busy=false;

    if(result.ok) {

      // DATA INTEGRITY: only exact application ACK can release a batch. A

      // retry keeps the original UIDs; a reboot may safely replay a disk chunk.

      if(!activePath.isEmpty()&&!spool.acknowledge(activePath,activeCount))logEvent("ERROR spool ACK cleanup; replay/repair required");

      pending.release(result.ok);activePath="";failed=false;retryDelay=UPLOAD_INTERVAL_MS;

      haveCloudAck=true;
      lastAckAt=now;
      logEvent("MILESTONE application ACK uptime=%llu ms",(unsigned long long)now);

      if(!strcmp(result.recording,"RECORDING")){recordingKnown=true;recording=true;}

      else if(!strcmp(result.recording,"STOPPED")){recordingKnown=true;recording=false;}

      lastCloudUpload=clockValid()?utcMs():0;unassignedCount+=result.unassigned;

      logEvent("ACK recording=%s session=%s",result.recording,result.session);

      due=now+(spool.count?1000:UPLOAD_INTERVAL_MS);

    } else {

      ++uploadFailures;failed=true;
      const uint32_t waitMs=retryWait(retryDelay,result.http,MAX_RETRY_MS,RATE_LIMIT_RETRY_MS);
      due=now+waitMs;
      retryDelay=retryNext(retryDelay,result.http,MAX_RETRY_MS,RATE_LIMIT_RETRY_MS);

      logEvent("UPLOAD ERROR retry in %lu ms; same UIDs retained",(unsigned long)(due-now));

    }

  }

  // Persist a slow in-flight request as well as failed uploads, without racing

  // the worker: it owns a separate immutable copy of the batch.

  if(activeCount&&activePath.isEmpty()&&(failed||now-activeSince>=PERSIST_INFLIGHT_MS)) {

    String path;if(spool.save(active,activeCount,path)){activePath=path;logEvent("SPOOL saved in-flight %lu",(unsigned long)activeCount);}

  }

  bool outage=failed||spool.count||WiFi.status()!=WL_CONNECTED;

  if(!stagedCount&&outage&&(now-lastSpill>=OUTAGE_FLUSH_MS||uxQueueMessagesWaiting(samplesQueue)>=96)) {

    // Older active data must be durable first, so filenames preserve FIFO.

    if(!activeCount||!activePath.isEmpty()){stagedCount=drain(staging);lastSpill=now;}

  }

  if(stagedCount) {

    String path;if(spool.save(staging,stagedCount,path)){logEvent("SPOOL saved queued %lu",(unsigned long)stagedCount);stagedCount=0;}

  }

  static bool priorFault=false,priorFull=false;

  if(spool.fault!=priorFault||spool.full!=priorFull){logEvent("ERROR SPOOL fault=%u full=%u; no overwrite",spool.fault,spool.full);priorFault=spool.fault;priorFull=spool.full;}

  if(!busy&&now>=due) {

    // Preserve unreadable disk contents; current-boot RAM samples may still upload with stable UIDs.
    const bool ramOnlyOnline=spool.mountFailedAtBoot()&&wifiState==WL_CONNECTED;
    if(!activeCount&&(!spool.fault||ramOnlyOnline)) {

      if(spool.count)spool.oldest(active,activeCount,activePath);

      else if(stagedCount&&ramOnlyOnline) {
        memcpy(active,staging,stagedCount*sizeof(Sample));activeCount=stagedCount;
        stagedCount=0;activeSince=now;
      } else if(!stagedCount){activeCount=drain(active);activeSince=now;}

    }

    UploadJob* job=new UploadJob();job->count=activeCount;memcpy(job->samples,active,activeCount*sizeof(Sample));

    cJSON* root=cJSON_CreateObject();addNumber(root,"version",3);cJSON* arr=cJSON_AddArrayToObject(root,"samples");

    for(uint32_t i=0;i<activeCount;++i)cJSON_AddItemToArray(arr,sampleJson(active[i]));

    cJSON_AddItemToObject(root,"health",healthJson(activeCount,stagedCount));job->json=cJSON_PrintUnformatted(root);cJSON_Delete(root);

    if(job->json&&xQueueSend(jobsQueue,&job,0)==pdTRUE){busy=true;logEvent("Batch upload start n=%lu spool=%lu uptime=%llu ms",(unsigned long)activeCount,(unsigned long)spool.count,(unsigned long long)now);}

    else {if(job->json)cJSON_free(job->json);delete job;due=now+UPLOAD_INTERVAL_MS;logEvent("ERROR upload allocation/queue failed");}

  }

  uint32_t queueDepth=uxQueueMessagesWaiting(samplesQueue);

  bool wifiOnline=wifiState==WL_CONNECTED;

  CloudState cloud=!wifiOnline?CLOUD_OFF:(busy||failed||!haveCloudAck)?CLOUD_WAIT:CLOUD_OK;

  bool backlog=queueDepth||spool.count||activeCount||stagedCount||failed;

  if(haveCloudAck&&!backlog&&!backlogDrainedAt){backlogDrainedAt=now;logEvent("MILESTONE backlog drained uptime=%llu ms",(unsigned long long)now);}
  if(backlog)backlogDrainedAt=0;

  updateGatewayUi(now,wifiOnline,cloud,queueDepth,spool.count,recordingKnown,recording,backlog);

  delay(10);

}
