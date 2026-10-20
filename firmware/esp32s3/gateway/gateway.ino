// ============================================================
// FoodSpoilageMonitor ESP32-S3 Gateway
//
// [Arduino IDE 보드 설정]
// - Board: ESP32S3 Dev Module
// - Flash Size: 16MB
// - Partition Scheme: Huge APP menu; sketch partitions.csv overrides it with two 3MB OTA slots.
//   Confirm the compiled partitions.bin matches this sketch's partitions.csv before USB upload.
// - PSRAM: OPI PSRAM (required for the current N16R8 build)
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

#include <lwip/netdb.h>

#include <WiFiManager.h>
#include <ESPmDNS.h>
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
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <WebServer.h>
#include <Update.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include "ota_signing_key.h"

#include <sys/time.h>

#include <stdarg.h>

#include <cJSON.h>

#include "Integrity.h"

#include "Spool.h"

#include "GatewayUi.h"

#include "NrfLegacyDfuRelay.h"
#include "NrfDfuPackagePolicy.h"
#include "WifiRecoveryPolicy.h"

// Arduino-ESP32 3.3.12 NetworkManager::hostByName() clears lwIP's DNS cache
// directly when the interface address state changes. With lwIP thread-safety
// checks enabled, that can run SNTP DNS callbacks from this task and assert in
// udp_new_ip_type(). Resolve through the thread-safe netconn getaddrinfo path,
// then retain the original hostname for TLS SNI and certificate handling.
class GatewaySecureClient : public WiFiClientSecure {
 public:
  using WiFiClientSecure::connect;

  int connect(const char* host,uint16_t port) override {
    return connect(host,port,_timeout);
  }

  int connect(const char* host,uint16_t port,int32_t timeout) override {
    IPAddress address;
    if(!resolveHost(host,address))return 0;
    setConnectionTimeout(timeout);
    return NetworkClientSecure::connect(address,port,host,_CA_cert,_cert,_private_key);
  }

 private:
  static bool resolveHost(const char* host,IPAddress& address) {
    if(!host||!host[0])return false;
    if(address.fromString(host))return true;
    struct addrinfo hints={};
    struct addrinfo* result=nullptr;
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype=SOCK_STREAM;
    const int err=lwip_getaddrinfo(host,"0",&hints,&result);
    if(err!=ERR_OK||!result)return false;
#if CONFIG_LWIP_IPV6
    if(result->ai_family==AF_INET6) {
      const struct sockaddr_in6* ipv6=(const struct sockaddr_in6*)result->ai_addr;
      address=IPAddress(IPv6,ipv6->sin6_addr.s6_addr);
    } else
#endif
    {
      const struct sockaddr_in* ipv4=(const struct sockaddr_in*)result->ai_addr;
      address=IPAddress(ipv4->sin_addr.s_addr);
    }
    lwip_freeaddrinfo(result);
    return true;
  }
};

#ifdef CONFIG_APP_ROLLBACK_ENABLE
extern "C" bool verifyRollbackLater();
#endif


const char* GOOGLE_SCRIPT_URL =

  "https://script.google.com/macros/s/AKfycbyrDRFYcw6KKLK2fae04ZW1nxsuBVlW9LuAaUawLq3Cxim1Qri7uZe5AEzsPwf3txLd/exec";

const char* AP_NAME     = "FoodMonitor-PW12345678";

const char* AP_PASSWORD = "12345678";



using namespace integrity;

using namespace gateway_ui;

constexpr uint32_t UPLOAD_INTERVAL_MS=5000, HEALTH_HEARTBEAT_INTERVAL_MS=5000, OUTAGE_FLUSH_MS=60000;

constexpr uint32_t MAX_RETRY_MS=30000, RATE_LIMIT_RETRY_MS=150000, PERSIST_INFLIGHT_MS=10000;

constexpr uint8_t WIFI_RESET_PIN=0;
constexpr uint32_t WIFI_CONNECT_MS=wifi_recovery::CONNECT_TIMEOUT_MS;
constexpr uint32_t WIFI_RETRY_MS=wifi_recovery::RETRY_CYCLE_MS;

constexpr uint8_t LCD_SDA_PIN=8, LCD_SCL_PIN=9, LCD_ADDRESS=0x27;

constexpr uint8_t NEOPIXEL_PIN=48, NEOPIXEL_BRIGHTNESS=24;

constexpr uint32_t LCD_REFRESH_MS=750;

// Hardware CDC/JTAG is the verified console. Mirroring every line to UART0 made
// combined captures show each event twice; UART0 remains available as an opt-in.
constexpr bool UART_DIAGNOSTICS=false;
constexpr char FIRMWARE_VERSION[]="2.0.10";
constexpr uint64_t OTA_BUILD_NUMBER=2026102102ULL;

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
volatile bool pairingDisconnectRequested=false;

uint32_t rxSequence=0, parseErrors=0, queueDrops=0;

char gatewayBoot[33]={};
char deviceId[13]={};
char targetSensorId[17]={};
char connectedSensorId[17]={};
char connectedSensorAddress[18]={};
char connectedSensorFirmware[32]={};

OutageSpool spool;

BLEAdvertisedDevice* targetDevice=nullptr;

BLEClient* bleClient=nullptr;

WiFiManager wifiManager;
Preferences wifiPreferences;
Preferences pairingPreferences;
Preferences otaPreferences;
struct SavedWifi { char ssid[33]; char password[65]; };
SavedWifi wifiNetworks[2]={};
bool wifiPreferencesReady=false, wifiTrying=false;
bool pairingPreferencesReady=false;
bool otaPreferencesReady=false;
char otaPassword[33]={};
bool otaServerReady=false,otaManifestReady=false,otaBinaryReady=false,otaUpdateStarted=false;
enum OtaMode { OTA_NORMAL, OTA_PREPARE, OTA_ACTIVE };
OtaMode otaMode=OTA_NORMAL;
SemaphoreHandle_t otaIngressMutex=nullptr;
bool otaLeaseReleasePending=false,otaLeaseReleaseDeferredOnce=false;
uint64_t otaDeadlineAt=0;
size_t otaReceivedBytes=0;
uint32_t otaGapSamples=0;
const char* otaUploadFailure=nullptr;
int otaUploadFailureCode=400;
bool otaLeaseHeld=false;
bool otaHttpBusy=false,otaHttpFailed=false;
uint32_t otaActiveSamples=0,otaStagedSamples=0;
constexpr uint32_t OTA_PREPARE_TIMEOUT_MS=180000;
constexpr uint32_t OTA_ACTIVE_TIMEOUT_MS=120000;
bool otaPendingVerify=false;
uint64_t otaBootAt=0;
uint64_t otaIncomingBuild=0;
char otaIncomingVersion[24]={};
char otaIncomingDigest[65]={};
uint8_t otaSignature[80]={};
char otaManifestBuffer[512]={};
size_t otaManifestLength=0;
mbedtls_sha256_context otaShaContext;
bool otaShaStarted=false;
WebServer otaServer(80);
uint64_t otaLastCloudAckAt=0;
uint32_t otaPendingSamples=0;
bool otaUploadActive=false;
enum NrfDfuMode { NRF_DFU_NORMAL, NRF_DFU_PREPARE, NRF_DFU_ACTIVE, NRF_DFU_RECONNECT };
volatile NrfDfuMode nrfDfuMode=NRF_DFU_NORMAL;
volatile bool nrfDfuCancelRequested=false;
uint32_t nrfDfuBytesSent=0,nrfDfuBytesTotal=0;
uint64_t nrfDfuTransferStartedAt=0;
char nrfDfuLastResult[128]="No DFU has run since boot";
bool nrfDfuHasResult=false,nrfDfuLastSuccess=false;
uint64_t nrfDfuDeadlineAt=0;
uint32_t nrfDfuGapSamples=0;
constexpr uint32_t NRF_DFU_PREPARE_TIMEOUT_MS=180000;
constexpr uint32_t NRF_DFU_RECONNECT_TIMEOUT_MS=120000;
NrfLegacyDfuRelay nrfDfuRelay;
NrfLegacyDfuRelay::PackageInfo nrfDfuPackage;
constexpr const char* NRF_DFU_PACKAGE_PATH=nrf_dfu_package::FINAL_PATH;
File nrfPackageUploadFile;
bool nrfPackageUploadActive=false,nrfPackageUploadFailed=false;
size_t nrfPackageUploadBytes=0,nrfPackageUploadLimit=0;
uint64_t nrfPackageUploadLastAt=0;
int nrfPackageUploadStatus=400;
char nrfPackageUploadError[128]={};
bool cloudRecordingKnown=false,cloudRecording=false;
uint8_t wifiNetworkCount=0, wifiNextSlot=0, wifiTried=0, wifiPreferredSlot=0;
int8_t wifiActiveSlot=-1;
uint64_t wifiAttemptAt=0, wifiRetryAt=0;
bool wifiDisconnectPending=false,mdnsReady=false;
bool wifiWasConnected=false;
uint64_t wifiDisconnectAt=0;
volatile bool wifiDisconnectEventPending=false;
volatile uint8_t wifiLastDisconnectReason=0;

LiquidCrystal_I2C lcd(LCD_ADDRESS,16,2);

Adafruit_NeoPixel gatewayPixel(1,NEOPIXEL_PIN,NEO_GRB+NEO_KHZ800);

bool lcdReady=false;

LcdMode lcdMode=LCD_DEBUG;

ActiveLowButton bootButton;

PixelState shownPixel=PIXEL_BOOT;
bool otaRainbowActive=false;
uint64_t otaRainbowStartedAt=0,otaRainbowLastFrameAt=0;

uint64_t lastLcdRefresh=0;

char shownLcd[2][LCD_COLUMNS+1]={{0},{0}};

uint32_t uploadFailures=0, unassignedCount=0;

int64_t lastCloudUpload=0;



// The network task needs the serialized request plus the identities expected in
// the ACK. Keeping another full Sample[32] alive during the TLS handshake costs
// about 3.8 KiB of the same contiguous internal heap used by mbedTLS.
struct AckIdentity { char boot[32]; uint32_t rx; };
struct UploadJob { AckIdentity acks[BATCH_SIZE]; uint32_t count; bool heartbeat; char* json; };

struct UploadResult { bool ok; bool heartbeat; int http; uint32_t unassigned; char recording[16]; char session[64]; };
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

void logHttpsConnectDiagnostic(const char* phase,WiFiClientSecure& client) {
  char detail[96]={};
  const int secureError=client.lastError(detail,sizeof(detail));
  // NetworkClientSecure returns before setting last_error when hostByName() fails.
  const char* dns=secureError==0?"failed":"resolved";
  const char* stage=secureError==0?"DNS":secureError==-1?"TCP/socket":"TLS";
  logEvent("HTTPS %s diag host=script.google.com port=443 dns=%s stage=%s code=%d",
    phase,dns,stage,secureError);
  logEvent("HTTPS %s diag detail=%.80s wifi=%d rssi=%ld heap=%lu",
    phase,detail,(int)WiFi.status(),(long)WiFi.RSSI(),(unsigned long)ESP.getFreeHeap());
}

void logHttpsHeapDiagnostic(const char* phase) {
  logEvent("HTTPS heap %s free=%lu largest=%lu min=%lu free8=%lu largest8=%lu min8=%lu",
    phase,
    (unsigned long)ESP.getFreeHeap(),
    (unsigned long)ESP.getMaxAllocHeap(),
    (unsigned long)ESP.getMinFreeHeap(),
    (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
    (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
    (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
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

void rememberSuccessfulWifiSlot(uint8_t slot) {
  if(slot>=wifiNetworkCount)return;
  wifiPreferredSlot=slot;
  if(!wifiPreferencesReady)return;
  if(wifiPreferences.getUChar("last",0xff)==slot)return;
  if(wifiPreferences.putUChar("last",slot)!=sizeof(uint8_t))
    logEvent("ERROR Wi-Fi preferred slot storage failed; credentials unchanged");
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
  wifiPreferredSlot=wifi_recovery::preferredSlot(
    wifiPreferencesReady?wifiPreferences.getUChar("last",0xff):0xff,wifiNetworkCount);
  wifiNextSlot=wifiPreferredSlot;
  wifiActiveSlot=-1;
  logEvent("Wi-Fi saved networks=%u preferred_slot=%u",
    (unsigned)wifiNetworkCount,(unsigned)wifiPreferredSlot);
}

void loadSensorPairing() {
  pairingPreferencesReady=pairingPreferences.begin("fsm-pair",false);
  if(pairingPreferencesReady) {
    String saved=pairingPreferences.getString("target","");
    bool valid=saved.length()==16;
    for(uint8_t i=0;valid&&i<16;++i)valid=(saved[i]>='0'&&saved[i]<='9')||(saved[i]>='a'&&saved[i]<='f');
    if(valid)saved.toCharArray(targetSensorId,sizeof(targetSensorId));
  }
  logEvent("DEVICE id=%s BLE target=%s",deviceId,targetSensorId[0]?targetSensorId:"ANY (legacy single-device)");
}

void loadOtaSettings() {
  otaPreferencesReady=otaPreferences.begin("fsm-ota",false);
  if(!otaPreferencesReady) {logEvent("ERROR OTA credential storage unavailable");return;}
  String saved=otaPreferences.getString("token","");
  bool valid=saved.length()==32;
  for(uint8_t i=0;valid&&i<32;++i)valid=(saved[i]>='0'&&saved[i]<='9')||(saved[i]>='a'&&saved[i]<='f');
  if(valid)saved.toCharArray(otaPassword,sizeof(otaPassword));
}

const char* nrfDfuModeName(NrfDfuMode mode) {
  return mode==NRF_DFU_PREPARE?"PREPARE":mode==NRF_DFU_ACTIVE?"UPDATING":
    mode==NRF_DFU_RECONNECT?"RECONNECTING":"IDLE";
}

bool setPairedSensor(const char* sensorId,char* error,size_t errorSize) {
  if(!sensorId||strlen(sensorId)!=16) {snprintf(error,errorSize,"connected sensor ID is unavailable");return false;}
  for(uint8_t i=0;i<16;++i)if(!((sensorId[i]>='0'&&sensorId[i]<='9')||(sensorId[i]>='a'&&sensorId[i]<='f'))) {
    snprintf(error,errorSize,"connected sensor ID is invalid");return false;
  }
  if(!pairingPreferencesReady) {snprintf(error,errorSize,"pairing storage is unavailable");return false;}
  if(nrfDfuMode!=NRF_DFU_NORMAL||otaMode!=OTA_NORMAL) {snprintf(error,errorSize,"an update is active");return false;}
  if(!bleConnected||strcmp(sensorId,connectedSensorId)) {snprintf(error,errorSize,"sensor connection changed; refresh and approve again");return false;}
  if(pairingPreferences.putString("target",sensorId)!=16||pairingPreferences.getString("target","")!=sensorId) {
    snprintf(error,errorSize,"pairing target was not retained");return false;
  }
  portENTER_CRITICAL(&stateMux);memcpy(targetSensorId,sensorId,16);targetSensorId[16]=0;portEXIT_CRITICAL(&stateMux);
  logEvent("BLE pairing approved sensor_id=%s address=%s",sensorId,connectedSensorAddress[0]?connectedSensorAddress:"unknown");
  return true;
}

bool requestNrfDfuStart(char* error,size_t errorSize) {
  char connected[17],wanted[17];
  portENTER_CRITICAL(&stateMux);memcpy(connected,connectedSensorId,sizeof(connected));memcpy(wanted,targetSensorId,sizeof(wanted));portEXIT_CRITICAL(&stateMux);
  if(nrfDfuMode!=NRF_DFU_NORMAL||otaMode!=OTA_NORMAL||nrfPackageUploadActive) {snprintf(error,errorSize,"another update is active");return false;}
  if(!wanted[0]||strcmp(connected,wanted)||!bleConnected) {snprintf(error,errorSize,"explicit paired sensor is not connected");return false;}
  if(!cloudRecordingKnown||cloudRecording) {snprintf(error,errorSize,"recording state is not confirmed STOPPED");return false;}
  if(!spool.mounted||spool.fault) {snprintf(error,errorSize,"SPIFFS is unavailable");return false;}
  NrfLegacyDfuRelay::PackageInfo package;char validation[128]={};
  if(!nrfDfuRelay.validatePackage(SPIFFS,NRF_DFU_PACKAGE_PATH,package,validation,sizeof(validation))) {
    snprintf(error,errorSize,"package rejected: %.92s",validation);return false;
  }
  xSemaphoreTake(otaIngressMutex,portMAX_DELAY);
  if(nrfDfuMode!=NRF_DFU_NORMAL) {xSemaphoreGive(otaIngressMutex);snprintf(error,errorSize,"another DFU request won the start race");return false;}
  nrfDfuPackage=package;nrfDfuCancelRequested=false;nrfDfuBytesSent=0;nrfDfuBytesTotal=package.binarySize;
  nrfDfuTransferStartedAt=0;nrfDfuHasResult=false;nrfDfuLastSuccess=false;
  snprintf(nrfDfuLastResult,sizeof(nrfDfuLastResult),"Draining queued and spooled samples");
  nrfDfuMode=NRF_DFU_PREPARE;nrfDfuDeadlineAt=uptimeMs()+NRF_DFU_PREPARE_TIMEOUT_MS;
  xSemaphoreGive(otaIngressMutex);
  logEvent("nRF DFU PREPARE sensor=%s app_bytes=%lu; draining existing queue, spool, and HTTP",
    connected,(unsigned long)package.binarySize);
  return true;
}

bool requestNrfDfuCancel(char* message,size_t messageSize) {
  xSemaphoreTake(otaIngressMutex,portMAX_DELAY);
  const NrfDfuMode mode=nrfDfuMode;
  if(mode==NRF_DFU_PREPARE) {nrfDfuMode=NRF_DFU_NORMAL;nrfDfuDeadlineAt=0;snprintf(nrfDfuLastResult,sizeof(nrfDfuLastResult),"Cancelled before transfer");}
  else if(mode==NRF_DFU_ACTIVE)nrfDfuCancelRequested=true;
  xSemaphoreGive(otaIngressMutex);
  if(mode==NRF_DFU_PREPARE){snprintf(message,messageSize,"DFU preparation cancelled");logEvent("nRF DFU PREPARE cancelled; normal collection resumed");return true;}
  if(mode==NRF_DFU_ACTIVE){snprintf(message,messageSize,"DFU cancellation requested");logEvent("nRF DFU cancel requested; bootloader reset will be attempted");return true;}
  snprintf(message,messageSize,"no cancellable DFU is active");return false;
}

void handlePairingCommand() {
  static char line[40];static uint8_t length=0;
  while(Serial.available()) {
    const char c=(char)Serial.read();
    if(c!='\n'&&c!='\r') {if(length<sizeof(line)-1)line[length++]=c;continue;}
    line[length]=0;length=0;
    if(!strcmp(line,"OTA TOKEN")) {
      if(!otaPreferencesReady) {logEvent("ERROR OTA token storage unavailable");continue;}
      uint8_t random[16];esp_fill_random(random,sizeof(random));
      for(uint8_t i=0;i<sizeof(random);++i)snprintf(otaPassword+i*2,3,"%02x",random[i]);
      if(otaPreferences.putString("token",otaPassword)!=32||otaPreferences.getString("token","")!=otaPassword) {
        otaPassword[0]=0;logEvent("ERROR OTA token was not retained");continue;
      }
      logEvent("OTA local upload token generated; copy from this USB serial session once");
      Serial.println(otaPassword);if(UART_DIAGNOSTICS)Serial0.println(otaPassword);continue;
    }
    if(!strcmp(line,"OTA?")) {logEvent("OTA local upload=%s version=%s build=%llu",otaPassword[0]?"enabled":"disabled",FIRMWARE_VERSION,(unsigned long long)OTA_BUILD_NUMBER);continue;}
    if(!strcmp(line,"NRF DFU?")) {
      const char* state=nrfDfuModeName(nrfDfuMode);
      logEvent("nRF DFU state=%s package=%s upload=%s",state,SPIFFS.exists(NRF_DFU_PACKAGE_PATH)?"present":"missing",
        nrfPackageUploadActive?"active":"idle");continue;
    }
    if(!strcmp(line,"NRF DFU CANCEL")) {
      char message[64];if(!requestNrfDfuCancel(message,sizeof(message)))logEvent("nRF DFU cancel ignored; %s",message);
      continue;
    }
    if(!strcmp(line,"NRF DFU START")) {
      char error[128]={};if(!requestNrfDfuStart(error,sizeof(error)))logEvent("nRF DFU blocked: %s",error);continue;
    }
    if(!strcmp(line,"PAIR?")) {logEvent("DEVICE id=%s BLE target=%s",deviceId,targetSensorId[0]?targetSensorId:"ANY");continue;}
    if(strncmp(line,"PAIR ",5))continue;
    const char* value=line+5;
    if(!strcmp(value,"ANY")) {
      if(!pairingPreferencesReady) {
        logEvent("ERROR BLE pairing target unchanged");continue;
      }
      pairingPreferences.putString("target","ANY");
      if(pairingPreferences.getString("target","")!="ANY") {logEvent("ERROR BLE pairing target unchanged");continue;}
      portENTER_CRITICAL(&stateMux);targetSensorId[0]=0;pairingDisconnectRequested=bleConnected&&connectedSensorId[0];portEXIT_CRITICAL(&stateMux);
      logEvent("BLE pairing target cleared; legacy any-NUS mode");continue;
    }
    bool valid=strlen(value)==16;
    for(uint8_t i=0;valid&&i<16;++i)valid=(value[i]>='0'&&value[i]<='9')||(value[i]>='a'&&value[i]<='f');
    if(!valid||!pairingPreferencesReady) {
      logEvent("ERROR BLE pairing target unchanged; use 16 lowercase hex digits");continue;
    }
    pairingPreferences.putString("target",value);
    if(pairingPreferences.getString("target","")!=value) {logEvent("ERROR BLE pairing target unchanged");continue;}
    bool disconnect=false;
    portENTER_CRITICAL(&stateMux);memcpy(targetSensorId,value,16);targetSensorId[16]=0;disconnect=bleConnected&&strcmp(connectedSensorId,value)!=0;if(disconnect)pairingDisconnectRequested=true;portEXIT_CRITICAL(&stateMux);
    logEvent("BLE pairing target set=%s",targetSensorId);
  }
}

bool otaSafeState() {
  return otaPassword[0]&&spool.mounted&&!spool.fault&&!spool.count&&
    !otaActiveSamples&&!otaStagedSamples&&!otaHttpBusy&&!otaHttpFailed&&
    !uxQueueMessagesWaiting(samplesQueue)&&!uxQueueMessagesWaiting(resultsQueue)&&
    !bleConnected&&!otaPendingVerify&&WiFi.status()==WL_CONNECTED;
}

bool otaSafeToStart() {
  return otaMode==OTA_NORMAL&&nrfDfuMode==NRF_DFU_NORMAL&&!nrfPackageUploadActive&&otaPassword[0]&&spool.mounted&&!spool.fault&&
    !otaPendingVerify&&!otaLeaseReleasePending&&WiFi.status()==WL_CONNECTED;
}

bool otaActivateIfDrained() {
  xSemaphoreTake(otaIngressMutex,portMAX_DELAY);
  const bool ready=otaMode==OTA_PREPARE&&otaSafeState();
  if(ready){otaMode=OTA_ACTIVE;otaDeadlineAt=uptimeMs()+OTA_ACTIVE_TIMEOUT_MS;}
  xSemaphoreGive(otaIngressMutex);
  if(ready)logEvent("OTA ACTIVE; BLE measurement gap begins; queued samples preserved");
  return ready;
}

bool otaSameOrigin() {
  const String host=otaServer.header("Host");
  const String origin=otaServer.header("Origin");
  const String ip=WiFi.localIP().toString();
  const bool ipOrigin=host==ip&&origin==String("http://")+ip;
  const bool mdnsOrigin=host=="foodmonitor.local"&&origin=="http://foodmonitor.local";
  return ipOrigin||mdnsOrigin;
}

bool nrfPackageUploadSafe() {
  return otaMode==OTA_NORMAL&&nrfDfuMode==NRF_DFU_NORMAL&&otaPassword[0]&&
    spool.mounted&&!spool.fault&&!spool.count&&cloudRecordingKnown&&!cloudRecording&&
    !otaActiveSamples&&!otaStagedSamples&&!otaHttpBusy&&!otaHttpFailed&&
    !otaUploadActive&&!otaLeaseReleasePending&&!uxQueueMessagesWaiting(samplesQueue)&&
    !uxQueueMessagesWaiting(resultsQueue)&&WiFi.status()==WL_CONNECTED;
}

bool validateNrfPackageAt(const char* path,NrfLegacyDfuRelay::PackageInfo& info,char* error,size_t errorSize) {
  if(error&&errorSize)error[0]=0;
  return nrfDfuRelay.validatePackage(SPIFFS,path,info,error,errorSize);
}

void recoverNrfPackageFiles() {
  if(!spool.mounted||spool.fault)return;
  if(SPIFFS.exists(nrf_dfu_package::TEMP_PATH))SPIFFS.remove(nrf_dfu_package::TEMP_PATH);
  if(!SPIFFS.exists(nrf_dfu_package::BACKUP_PATH))return;
  if(!SPIFFS.exists(NRF_DFU_PACKAGE_PATH)) {
    if(SPIFFS.rename(nrf_dfu_package::BACKUP_PATH,NRF_DFU_PACKAGE_PATH))
      logEvent("nRF DFU package backup restored");
    else logEvent("ERROR nRF DFU package backup restore failed");
    return;
  }
  NrfLegacyDfuRelay::PackageInfo info;char error[128]={};
  if(validateNrfPackageAt(NRF_DFU_PACKAGE_PATH,info,error,sizeof(error))) {
    if(!SPIFFS.remove(nrf_dfu_package::BACKUP_PATH))logEvent("ERROR stale nRF DFU package backup retained");
    return;
  }
  NrfLegacyDfuRelay::PackageInfo backupInfo;char backupError[128]={};
  if(!validateNrfPackageAt(nrf_dfu_package::BACKUP_PATH,backupInfo,backupError,sizeof(backupError))) {
    logEvent("ERROR nRF DFU package recovery needs inspection; final=%s backup=%s",error,backupError);return;
  }
  if(!SPIFFS.rename(NRF_DFU_PACKAGE_PATH,nrf_dfu_package::TEMP_PATH)||
     !SPIFFS.rename(nrf_dfu_package::BACKUP_PATH,NRF_DFU_PACKAGE_PATH)) {
    if(!SPIFFS.exists(NRF_DFU_PACKAGE_PATH)&&SPIFFS.exists(nrf_dfu_package::BACKUP_PATH))
      SPIFFS.rename(nrf_dfu_package::BACKUP_PATH,NRF_DFU_PACKAGE_PATH);
    logEvent("ERROR nRF DFU package recovery rename failed");return;
  }
  SPIFFS.remove(nrf_dfu_package::TEMP_PATH);
  logEvent("nRF DFU package backup restored after interrupted replacement");
}

bool promoteNrfPackage(char* error,size_t errorSize) {
  const bool hadFinal=SPIFFS.exists(NRF_DFU_PACKAGE_PATH);
  if(SPIFFS.exists(nrf_dfu_package::BACKUP_PATH)) {
    snprintf(error,errorSize,"stale backup prevents replacement");return false;
  }
  if(hadFinal&&!SPIFFS.rename(NRF_DFU_PACKAGE_PATH,nrf_dfu_package::BACKUP_PATH)) {
    snprintf(error,errorSize,"existing package backup failed");return false;
  }
  if(!SPIFFS.rename(nrf_dfu_package::TEMP_PATH,NRF_DFU_PACKAGE_PATH)) {
    if(hadFinal)SPIFFS.rename(nrf_dfu_package::BACKUP_PATH,NRF_DFU_PACKAGE_PATH);
    snprintf(error,errorSize,"validated package promotion failed");return false;
  }
  NrfLegacyDfuRelay::PackageInfo check;char validation[128]={};
  if(!validateNrfPackageAt(NRF_DFU_PACKAGE_PATH,check,validation,sizeof(validation))) {
    SPIFFS.rename(NRF_DFU_PACKAGE_PATH,nrf_dfu_package::TEMP_PATH);
    if(hadFinal)SPIFFS.rename(nrf_dfu_package::BACKUP_PATH,NRF_DFU_PACKAGE_PATH);
    SPIFFS.remove(nrf_dfu_package::TEMP_PATH);
    snprintf(error,errorSize,"promoted package recheck failed");return false;
  }
  if(hadFinal&&!SPIFFS.remove(nrf_dfu_package::BACKUP_PATH)) {
    logEvent("ERROR nRF DFU package installed but old backup remains");
  }
  return true;
}

void failNrfPackageUpload(const char* reason,int status) {
  if(nrfPackageUploadFile)nrfPackageUploadFile.close();
  if(SPIFFS.exists(nrf_dfu_package::TEMP_PATH))SPIFFS.remove(nrf_dfu_package::TEMP_PATH);
  nrfPackageUploadFailed=true;nrfPackageUploadStatus=status;nrfPackageUploadActive=false;
  nrfPackageUploadLastAt=0;
  snprintf(nrfPackageUploadError,sizeof(nrfPackageUploadError),"%s",reason);
  logEvent("nRF DFU package upload rejected HTTP %d: %s",status,reason);
}

void handleNrfPackagePage() {
  if(!otaServer.authenticate("admin",otaPassword))return otaServer.requestAuthentication();
  NrfLegacyDfuRelay::PackageInfo info;char error[128]={};
  const bool present=SPIFFS.exists(NRF_DFU_PACKAGE_PATH);
  const bool valid=present&&validateNrfPackageAt(NRF_DFU_PACKAGE_PATH,info,error,sizeof(error));
  const bool ready=nrfPackageUploadSafe();
  const String page=String("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'><title>nRF DFU</title>")+
    "<h1>nRF52840 firmware update</h1><p>Gateway "+String(deviceId)+"</p><p><a href='/'>Open ESP32-S3 gateway update</a></p><div id=s>Loading status…</div><p>Current package: "+
    (valid?String("valid, application ")+String(info.binarySize)+" bytes":present?String("invalid: ")+error:"none")+
    "</p><p>"+(ready?"Ready. Upload replaces only /nrf-dfu.zip after full validation.":
      "Blocked: recording must be STOPPED and RAM queue, spool, HTTP work, and other updates must be idle.")+
    "</p><form id=f><input id=z type=file accept='.zip,application/zip' required><button "+(ready?"":"disabled")+
    ">Validate and store</button></form><p><button id=p>Pair connected sensor</button> <button id=b>Start update</button> <button id=c>Cancel</button></p><pre id=o></pre>"
    "<script>const f=document.getElementById('f'),z=document.getElementById('z'),o=document.getElementById('o'),s=document.getElementById('s');"
    "async function status(){try{let r=await fetch('/nrf-dfu/status',{cache:'no-store'}),v=await r.json();s.textContent=`State: ${v.state} | connected: ${v.connected_sensor||'none'} (${v.connected_address||'address unknown'}) | paired: ${v.paired_sensor||'ANY'} | firmware: ${v.sensor_firmware||'unknown'} | progress: ${v.bytes_sent}/${v.bytes_total} (${v.percent}%) | ${v.message}`;document.getElementById('p').disabled=!v.can_pair;document.getElementById('b').disabled=!v.can_start;}catch(x){s.textContent=String(x)}}"
    "f.onsubmit=async e=>{e.preventDefault();let d=new FormData();d.append('package',z.files[0]);o.textContent='Uploading and validating…';try{let r=await fetch('/nrf-dfu/upload',{method:'POST',body:d});o.textContent=await r.text();await status()}catch(x){o.textContent=String(x)}};"
    "document.getElementById('p').onclick=async()=>{let v=await (await fetch('/nrf-dfu/status')).json();if(!v.connected_sensor||!confirm('Pair sensor '+v.connected_sensor+' at '+(v.connected_address||'unknown address')+'?'))return;let d=new URLSearchParams({sensor_id:v.connected_sensor});let r=await fetch('/nrf-dfu/pair',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:d});o.textContent=await r.text();await status()};"
    "document.getElementById('b').onclick=async()=>{if(!confirm('Start DFU for the explicitly paired sensor? Measurement pauses until reconnect.'))return;let r=await fetch('/nrf-dfu/start',{method:'POST'});o.textContent=await r.text();await status()};"
    "document.getElementById('c').onclick=async()=>{let r=await fetch('/nrf-dfu/cancel',{method:'POST'});o.textContent=await r.text();await status()};status();setInterval(status,1000)</script>";
  otaServer.sendHeader("Cache-Control","no-store");otaServer.send(200,"text/html; charset=utf-8",page);
}

void handleNrfPackageUploadComplete() {
  if(!otaServer.authenticate("admin",otaPassword))return otaServer.requestAuthentication();
  if(nrfPackageUploadFailed) {
    const int status=nrfPackageUploadStatus;const String message=nrfPackageUploadError;
    nrfPackageUploadFailed=false;nrfPackageUploadError[0]=0;
    return otaServer.send(status,"text/plain",message+"; existing package and samples were preserved.");
  }
  if(nrfPackageUploadActive)failNrfPackageUpload("upload did not complete",400);
  if(nrfPackageUploadFailed) {
    nrfPackageUploadFailed=false;
    return otaServer.send(400,"text/plain","Upload did not complete; existing package and samples were preserved.");
  }
  otaServer.send(201,"text/plain","Validated nRF DFU package stored. DFU was not started; use the web Start update button or NRF DFU START explicitly.");
}

void handleNrfDfuStatus() {
  if(!otaServer.authenticate("admin",otaPassword))return otaServer.requestAuthentication();
  char connected[17],paired[17],address[18],firmware[32],message[128];
  portENTER_CRITICAL(&stateMux);
  memcpy(connected,connectedSensorId,sizeof(connected));memcpy(paired,targetSensorId,sizeof(paired));
  memcpy(address,connectedSensorAddress,sizeof(address));memcpy(firmware,connectedSensorFirmware,sizeof(firmware));
  memcpy(message,nrfDfuLastResult,sizeof(message));
  portEXIT_CRITICAL(&stateMux);
  const NrfDfuMode mode=nrfDfuMode;
  const char* state=mode!=NRF_DFU_NORMAL?nrfDfuModeName(mode):nrfDfuHasResult?(nrfDfuLastSuccess?"SUCCESS":"FAILED"):"IDLE";
  NrfLegacyDfuRelay::PackageInfo package;char packageError[128]={};
  const bool packageValid=SPIFFS.exists(NRF_DFU_PACKAGE_PATH)&&validateNrfPackageAt(NRF_DFU_PACKAGE_PATH,package,packageError,sizeof(packageError));
  const bool pairedConnected=paired[0]&&connected[0]&&!strcmp(paired,connected)&&bleConnected;
  const bool canPair=mode==NRF_DFU_NORMAL&&otaMode==OTA_NORMAL&&bleConnected&&connected[0];
  const bool canStart=mode==NRF_DFU_NORMAL&&otaMode==OTA_NORMAL&&!nrfPackageUploadActive&&pairedConnected&&
    cloudRecordingKnown&&!cloudRecording&&spool.mounted&&!spool.fault&&packageValid;
  const uint32_t sent=nrfDfuBytesSent,total=nrfDfuBytesTotal;
  cJSON* root=cJSON_CreateObject();
  cJSON_AddStringToObject(root,"state",state);cJSON_AddStringToObject(root,"connected_sensor",connected);
  cJSON_AddStringToObject(root,"connected_address",address);cJSON_AddStringToObject(root,"paired_sensor",paired);
  cJSON_AddStringToObject(root,"sensor_firmware",firmware);cJSON_AddStringToObject(root,"message",message);
  cJSON_AddBoolToObject(root,"package_valid",packageValid);cJSON_AddBoolToObject(root,"can_pair",canPair);
  cJSON_AddBoolToObject(root,"can_start",canStart);addNumber(root,"bytes_sent",sent);addNumber(root,"bytes_total",total);
  addNumber(root,"percent",total?(100.0*sent/total):0);addNumber(root,"queue",uxQueueMessagesWaiting(samplesQueue));
  addNumber(root,"spool",spool.count);cJSON_AddBoolToObject(root,"http_busy",otaHttpBusy);
  cJSON_AddStringToObject(root,"recording",!cloudRecordingKnown?"UNKNOWN":cloudRecording?"RECORDING":"STOPPED");
  char* body=cJSON_PrintUnformatted(root);cJSON_Delete(root);
  otaServer.sendHeader("Cache-Control","no-store");otaServer.send(200,"application/json",body?body:"{}");if(body)cJSON_free(body);
}

void handleNrfDfuPair() {
  if(!otaServer.authenticate("admin",otaPassword))return otaServer.requestAuthentication();
  if(!otaSameOrigin())return otaServer.send(400,"text/plain","Origin mismatch");
  const String requested=otaServer.arg("sensor_id");char error[128]={};
  if(!setPairedSensor(requested.c_str(),error,sizeof(error)))return otaServer.send(409,"text/plain",error);
  otaServer.send(200,"text/plain","Connected sensor paired and retained in NVS");
}

void handleNrfDfuStart() {
  if(!otaServer.authenticate("admin",otaPassword))return otaServer.requestAuthentication();
  if(!otaSameOrigin())return otaServer.send(400,"text/plain","Origin mismatch");
  char error[128]={};if(!requestNrfDfuStart(error,sizeof(error)))return otaServer.send(409,"text/plain",error);
  otaServer.send(202,"text/plain","DFU preparation started; this page may be refreshed without stopping it");
}

void handleNrfDfuCancel() {
  if(!otaServer.authenticate("admin",otaPassword))return otaServer.requestAuthentication();
  if(!otaSameOrigin())return otaServer.send(400,"text/plain","Origin mismatch");
  char message[64];const bool accepted=requestNrfDfuCancel(message,sizeof(message));otaServer.send(accepted?202:409,"text/plain",message);
}

void handleNrfPackageUploadProgress() {
  HTTPUpload& upload=otaServer.upload();
  if(upload.status==UPLOAD_FILE_START) {
    nrfPackageUploadFailed=false;nrfPackageUploadError[0]=0;nrfPackageUploadBytes=0;nrfPackageUploadLimit=0;
    nrfPackageUploadLastAt=uptimeMs();
    if(!otaServer.authenticate("admin",otaPassword)){failNrfPackageUpload("authentication required",401);return;}
    if(!otaSameOrigin()){failNrfPackageUpload("origin mismatch",400);return;}
    if(!upload.filename.endsWith(".zip")){failNrfPackageUpload("a ZIP file is required",400);return;}
    recoverNrfPackageFiles();
    if(!nrfPackageUploadSafe()){failNrfPackageUpload("recording, pending data, HTTP, SPIFFS, or update state is not safe",409);return;}
    if(SPIFFS.exists(nrf_dfu_package::TEMP_PATH)&&!SPIFFS.remove(nrf_dfu_package::TEMP_PATH)) {
      failNrfPackageUpload("stale temporary file could not be removed",500);return;
    }
    nrfPackageUploadLimit=nrf_dfu_package::temporaryCapacity(SPIFFS.totalBytes(),SPIFFS.usedBytes(),spool.budget);
    if(!nrfPackageUploadLimit){failNrfPackageUpload("insufficient SPIFFS space while preserving spool reserve",507);return;}
    nrfPackageUploadFile=SPIFFS.open(nrf_dfu_package::TEMP_PATH,FILE_WRITE);
    if(!nrfPackageUploadFile){failNrfPackageUpload("temporary file open failed",500);return;}
    nrfPackageUploadActive=true;
    logEvent("nRF DFU package upload started limit=%lu spool_reserve=%lu",(unsigned long)nrfPackageUploadLimit,(unsigned long)spool.budget);
    return;
  }
  if(upload.status==UPLOAD_FILE_WRITE) {
    if(!nrfPackageUploadActive||nrfPackageUploadFailed)return;
    if(upload.currentSize>nrfPackageUploadLimit-nrfPackageUploadBytes) {
      failNrfPackageUpload("package exceeds safe SPIFFS capacity",413);return;
    }
    const size_t written=nrfPackageUploadFile.write(upload.buf,upload.currentSize);
    if(written!=upload.currentSize){failNrfPackageUpload("temporary file write failed",507);return;}
    nrfPackageUploadBytes+=written;nrfPackageUploadLastAt=uptimeMs();return;
  }
  if(upload.status==UPLOAD_FILE_ABORTED) {failNrfPackageUpload("upload was interrupted",400);return;}
  if(upload.status!=UPLOAD_FILE_END||!nrfPackageUploadActive||nrfPackageUploadFailed)return;
  nrfPackageUploadFile.flush();nrfPackageUploadFile.close();nrfPackageUploadActive=false;nrfPackageUploadLastAt=0;
  if(!nrfPackageUploadBytes||upload.totalSize!=nrfPackageUploadBytes) {
    failNrfPackageUpload("uploaded byte count mismatch",400);return;
  }
  if(!nrfPackageUploadSafe()) {
    failNrfPackageUpload("device state changed during upload; retry after pending data drains",409);return;
  }
  NrfLegacyDfuRelay::PackageInfo info;char error[128]={};
  if(!validateNrfPackageAt(nrf_dfu_package::TEMP_PATH,info,error,sizeof(error))) {
    char message[128];snprintf(message,sizeof(message),"DFU ZIP validation failed: %.92s",error);
    failNrfPackageUpload(message,400);return;
  }
  if(!promoteNrfPackage(error,sizeof(error))) {failNrfPackageUpload(error,500);return;}
  nrfDfuPackage=info;
  logEvent("nRF DFU package stored bytes=%lu app_bytes=%lu; DFU not started",
    (unsigned long)nrfPackageUploadBytes,(unsigned long)info.binarySize);
}

bool otaVersionValid(const char* version) {
  unsigned int major=0,minor=0,patch=0;char tail=0;
  return version&&sscanf(version,"%u.%u.%u%c",&major,&minor,&patch,&tail)==3;
}

bool verifyOtaManifest(char* text) {
  char format[16]={},target[16]={},version[24]={},digest[65]={},signatureText[128]={};
  unsigned long long build=0;
  const int fields=sscanf(text,"%15[^\n]\ntarget=%15[^\n]\nversion=%23[0-9.]\nbuild=%llu\nsha256=%64[0-9a-f]\nsignature=%127[^\n]\n",
      format,target,version,&build,digest,signatureText);
  if(fields!=6){logEvent("OTA manifest verify failed stage=FORMAT fields=%d bytes=%lu",fields,(unsigned long)otaManifestLength);return false;}
  if(strcmp(format,"FSGWOTA1")){logEvent("OTA manifest verify failed stage=FORMAT header");return false;}
  if(strcmp(target,"esp32s3")){logEvent("OTA manifest verify failed stage=TARGET");return false;}
  if(!otaVersionValid(version)){logEvent("OTA manifest verify failed stage=VERSION syntax");return false;}
  if(build<=OTA_BUILD_NUMBER){
    logEvent("OTA manifest verify failed stage=BUILD incoming=%llu current=%llu",build,(unsigned long long)OTA_BUILD_NUMBER);return false;
  }
  if(!strcmp(version,FIRMWARE_VERSION)){logEvent("OTA manifest verify failed stage=VERSION unchanged");return false;}
  size_t decoded=0;
  const int base64Result=mbedtls_base64_decode(otaSignature,sizeof(otaSignature),&decoded,
      (const unsigned char*)signatureText,strlen(signatureText));
  if(base64Result!=0||decoded==0){
    logEvent("OTA manifest verify failed stage=BASE64 code=%d bytes=%lu",base64Result,(unsigned long)decoded);return false;
  }
  char canonical[192];
  const int n=snprintf(canonical,sizeof(canonical),"FSGWOTA1\nesp32s3\n%s\n%llu\n%s\n",version,build,digest);
  if(n<=0||n>=(int)sizeof(canonical)){logEvent("OTA manifest verify failed stage=FORMAT canonical");return false;}
  uint8_t hash[32];
  const int hashResult=mbedtls_sha256((const uint8_t*)canonical,n,hash,0);
  if(hashResult!=0){logEvent("OTA manifest verify failed stage=HASH code=%d",hashResult);return false;}
  mbedtls_pk_context key;mbedtls_pk_init(&key);
  const int parsed=mbedtls_pk_parse_public_key(&key,(const uint8_t*)OTA_SIGNING_PUBLIC_KEY_PEM,strlen(OTA_SIGNING_PUBLIC_KEY_PEM)+1);
  if(parsed!=0){mbedtls_pk_free(&key);logEvent("OTA manifest verify failed stage=KEY code=%d",parsed);return false;}
  const int verifyResult=mbedtls_pk_verify(&key,MBEDTLS_MD_SHA256,hash,sizeof(hash),otaSignature,decoded);
  mbedtls_pk_free(&key);
  if(verifyResult!=0){logEvent("OTA manifest verify failed stage=SIGNATURE code=%d",verifyResult);return false;}
  otaIncomingBuild=build;snprintf(otaIncomingVersion,sizeof(otaIncomingVersion),"%s",version);
  snprintf(otaIncomingDigest,sizeof(otaIncomingDigest),"%s",digest);
  logEvent("OTA manifest verified version=%s build=%llu bytes=%lu signature_bytes=%lu",version,build,
    (unsigned long)otaManifestLength,(unsigned long)decoded);
  return true;
}

void resetOtaUpload() {
  if(otaUpdateStarted)Update.abort();
  otaUpdateStarted=false;otaManifestReady=false;otaBinaryReady=false;otaUploadActive=false;
  otaManifestLength=0;otaManifestBuffer[0]=0;otaIncomingVersion[0]=0;otaIncomingDigest[0]=0;
  otaIncomingBuild=0;otaReceivedBytes=0;
  if(otaShaStarted){mbedtls_sha256_free(&otaShaContext);otaShaStarted=false;}
}

void abortOtaSession() {
  resetOtaUpload();
  xSemaphoreTake(otaIngressMutex,portMAX_DELAY);
  const uint32_t missed=otaGapSamples;
  otaMode=OTA_NORMAL;otaDeadlineAt=0;otaGapSamples=0;
  xSemaphoreGive(otaIngressMutex);
  if(missed)logEvent("OTA measurement gap ended; %lu incoming samples not queued",(unsigned long)missed);
  if(otaLeaseHeld){
    otaLeaseHeld=false;otaLeaseReleasePending=true;otaLeaseReleaseDeferredOnce=true;
    logEvent("OTA lease release deferred until HTTP request cleanup");
  }
}

void rejectOtaUpload(const char* reason,int code) {
  otaUploadFailure=reason;otaUploadFailureCode=code;
  logEvent("OTA reject HTTP %d: %s",code,reason);
  abortOtaSession();
}

void handleOtaPage() {
  if(!otaServer.authenticate("admin",otaPassword))return otaServer.requestAuthentication();
  const bool safe=otaSafeToStart();
  const String page=String("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'><title>Gateway OTA</title>")+
    "<h1>ESP32-S3 firmware update</h1><p>Device " + String(deviceId) + " · " + FIRMWARE_VERSION + "</p><p><a href='/nrf-dfu'>Open nRF52840 sensor update</a></p><p>"+
    (safe?"Ready to reserve a STOPPED recording state and drain stored samples before upload.":"Blocked: check SPIFFS health, Wi-Fi, pending boot verification, and the USB-issued OTA token.")+
    "</p><p>Choose the signed .manifest and matching .bin from the same release.</p><form id=f><label>Manifest <input id=m type=file accept='.manifest' required></label><br><label>Firmware <input id=b type=file accept='.bin' required></label><br><label><input id=c type=checkbox required> I approve flashing this signed firmware; the gateway will restart.</label><br><button "+
    (safe?"":"disabled")+">Verify and update</button></form><pre id=o></pre><script>const f=document.getElementById('f'),m=document.getElementById('m'),b=document.getElementById('b'),c=document.getElementById('c'),o=document.getElementById('o');f.onsubmit=async e=>{e.preventDefault();if(!c.checked)return;try{let r=await fetch('/prepare',{method:'POST'});if(!r.ok){o.textContent=await r.text();return;}let ready=false;for(let i=0;i<180;i++){let s=await fetch('/status');if(!s.ok)throw Error(await s.text());let v=await s.json();if(v.mode!='PREPARE')throw Error('OTA preparation stopped');if(v.ready){ready=true;break;}o.textContent='Draining queue '+v.queue+' and spool '+v.spool+'; waiting for HTTP '+v.http;await new Promise(resolve=>setTimeout(resolve,1000));}if(!ready)throw Error('OTA preparation timed out');let d=new FormData();d.append('manifest',m.files[0]);d.append('firmware',b.files[0]);o.textContent='Uploading and verifying…';r=await fetch('/update',{method:'POST',body:d});o.textContent=await r.text();}catch(err){o.textContent=String(err);await fetch('/cancel',{method:'POST'}).catch(()=>{});}}</script>";
  otaServer.sendHeader("Cache-Control","no-store");otaServer.send(200,"text/html; charset=utf-8",page);
}

void handleOtaPrepare() {
  if(!otaServer.authenticate("admin",otaPassword))return otaServer.requestAuthentication();
  if(!otaSameOrigin()){logEvent("OTA reject HTTP 400: origin mismatch at prepare");return otaServer.send(400,"text/plain","OTA origin mismatch");}
  if(!otaSafeToStart()) {
    logEvent("OTA reject HTTP 409: prepare mode=%u wifi=%u spoolMounted=%u spoolFault=%u token=%u pendingVerify=%u",
      (unsigned)otaMode,(unsigned)(WiFi.status()==WL_CONNECTED),(unsigned)spool.mounted,(unsigned)spool.fault,
      (unsigned)(otaPassword[0]!=0),(unsigned)otaPendingVerify);
    return otaServer.send(409,"text/plain","OTA preparation blocked by Wi-Fi, SPIFFS, token, or boot verification");
  }
  otaUploadFailure=nullptr;
  xSemaphoreTake(otaIngressMutex,portMAX_DELAY);
  otaMode=OTA_PREPARE;otaDeadlineAt=uptimeMs()+OTA_PREPARE_TIMEOUT_MS;otaGapSamples=0;
  xSemaphoreGive(otaIngressMutex);
  logEvent("OTA PREPARE; BLE ingress paused, draining RAM, spool, and HTTP work");
  otaServer.send(200,"text/plain","OTA preparation started");
}

void handleOtaStatus() {
  if(!otaServer.authenticate("admin",otaPassword))return otaServer.requestAuthentication();
  const uint32_t queued=uxQueueMessagesWaiting(samplesQueue);
  const String body=String("{\"mode\":\"")+(otaMode==OTA_PREPARE?"PREPARE":otaMode==OTA_ACTIVE?"ACTIVE":"NORMAL")+
    "\",\"ready\":"+(otaMode==OTA_PREPARE&&otaLeaseHeld&&otaSafeState()?"true":"false")+
    ",\"queue\":"+String(queued)+",\"spool\":"+String(spool.count)+
    ",\"http\":"+(otaHttpBusy?"true":"false")+"}";
  otaServer.sendHeader("Cache-Control","no-store");otaServer.send(200,"application/json",body);
}

void handleOtaCancel() {
  if(!otaServer.authenticate("admin",otaPassword))return otaServer.requestAuthentication();
  if(!otaSameOrigin())return otaServer.send(400,"text/plain","OTA origin mismatch");
  if(otaMode!=OTA_NORMAL){logEvent("OTA cancelled by user");abortOtaSession();}
  otaServer.send(200,"text/plain","OTA cancelled; normal collection resumed");
}

void handleOtaUpload() {
  if(!otaServer.authenticate("admin",otaPassword))return otaServer.requestAuthentication();
  if(otaUploadFailure) return otaServer.send(otaUploadFailureCode,"text/plain",String(otaUploadFailure)+"; running firmware remains selected.");
  if(otaMode!=OTA_ACTIVE||!otaUpdateStarted||!otaManifestReady||!otaBinaryReady||
     !otaSameOrigin()||!otaSafeState()) {
    rejectOtaUpload("OTA state, origin, or drain safety check failed",409);
    return otaServer.send(409,"text/plain","OTA blocked; running firmware remains selected.");
  }
  if(!otaReceivedBytes||Update.hasError()) {
    logEvent("OTA image write error code=%u bytes=%lu",(unsigned)Update.getError(),(unsigned long)otaReceivedBytes);
    rejectOtaUpload("image write incomplete",400);
    return otaServer.send(400,"text/plain","OTA image write incomplete; running firmware remains selected.");
  }
  uint8_t digest[32];char actualHex[65];
  memcpy(digest,otaSignature,sizeof(digest));
  for(uint8_t i=0;i<32;++i)snprintf(actualHex+i*2,3,"%02x",digest[i]);actualHex[64]=0;
  if(strcmp(actualHex,otaIncomingDigest)) {
    rejectOtaUpload("signed image SHA-256 mismatch",400);
    return otaServer.send(400,"text/plain","Image SHA-256 mismatch; running firmware remains selected.");
  }
  if(!Update.end(true)||Update.hasError()) {
    logEvent("OTA ESP image validation failed code=%u bytes=%lu",(unsigned)Update.getError(),(unsigned long)otaReceivedBytes);
    rejectOtaUpload("ESP image validation failed",400);
    return otaServer.send(400,"text/plain","ESP image validation failed; running firmware remains selected.");
  }
  logEvent("OTA verified signed image bytes=%lu",(unsigned long)otaReceivedBytes);
  otaUpdateStarted=false;otaUploadActive=false;otaServer.send(200,"text/plain","Signed image verified. Gateway restarting into OTA slot.");
  logEvent("OTA accepted version=%s build=%llu; restarting",otaIncomingVersion,(unsigned long long)otaIncomingBuild);
  delay(500);ESP.restart();
}

bool otaLeaseResponse(const String& body) {
  cJSON* root=cJSON_Parse(body.c_str());if(!root)return false;
  const bool success=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"success"));
  cJSON* recording=cJSON_GetObjectItemCaseSensitive(root,"recording");
  cJSON* state=cJSON_GetObjectItemCaseSensitive(recording,"state");
  const bool hasState=cJSON_IsString(state);
  const bool stopped=hasState&&!strcmp(state->valuestring,"STOPPED");
  cJSON_Delete(root);
  otaLastCloudAckAt=success?uptimeMs():0;
  return success&&stopped;
}

bool syncOtaLease(const char* state) {
  cJSON* root=cJSON_CreateObject();addNumber(root,"version",3);
  cJSON_AddStringToObject(root,"device_id",deviceId);char sensorId[17];
  portENTER_CRITICAL(&stateMux);memcpy(sensorId,connectedSensorId,sizeof(sensorId));portEXIT_CRITICAL(&stateMux);
  cJSON_AddStringToObject(root,"sensor_id",sensorId);cJSON_AddArrayToObject(root,"samples");
  cJSON* health=healthJson(0,0);cJSON_AddStringToObject(health,"ota_state",state);cJSON_AddItemToObject(root,"health",health);
  char* payload=cJSON_PrintUnformatted(root);cJSON_Delete(root);if(!payload)return false;
  String redirect,response;int postCode=0;
  {
    GatewaySecureClient client;client.setInsecure();HTTPClient post;
    post.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);post.setConnectTimeout(5000);post.setTimeout(15000);post.setReuse(false);
    if(post.begin(client,GOOGLE_SCRIPT_URL)) {
      post.addHeader("Content-Type","application/json");postCode=post.POST((uint8_t*)payload,strlen(payload));
      if(postCode==HTTP_CODE_FOUND||postCode==HTTP_CODE_SEE_OTHER)redirect=post.getLocation();
      else if(postCode>=200&&postCode<300&&post.getSize()<=16000)response=post.getString();
      post.end();
    }
    client.stop();
  }
  cJSON_free(payload);
  if(redirect.length()) {
    delay(100);GatewaySecureClient client;client.setInsecure();HTTPClient get;
    get.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);get.setConnectTimeout(10000);get.setTimeout(15000);get.setReuse(false);
    if(get.begin(client,redirect)) {const int code=get.GET();if(code>=200&&code<300&&get.getSize()<=16000)response=get.getString();get.end();}
    client.stop();
  }
  return response.length()&&otaLeaseResponse(response);
}

bool serviceDeferredOtaLeaseRelease() {
  if(!otaLeaseReleasePending)return false;
  // Never start another HTTPS client in the WebServer upload/cancel callback.
  // Waiting one complete loop also lets handleClient() release request buffers.
  if(otaLeaseReleaseDeferredOnce){otaLeaseReleaseDeferredOnce=false;return true;}
  otaHttpBusy=true;
  const bool released=syncOtaLease("IDLE");
  otaHttpBusy=false;otaLeaseReleasePending=false;
  logEvent(released?"OTA lease release completed after HTTP cleanup":
    "OTA lease release failed after HTTP cleanup; retry normal cloud health");
  return true;
}

void handleOtaUploadProgress() {
  HTTPUpload& upload=otaServer.upload();
  if(upload.status==UPLOAD_FILE_ABORTED) {
    if(otaMode!=OTA_NORMAL)rejectOtaUpload("client upload aborted",400);
    return;
  }
  if(otaUploadFailure)return;
  if(upload.status==UPLOAD_FILE_START) {
    if(!otaServer.authenticate("admin",otaPassword)||!otaSameOrigin()) {
      rejectOtaUpload("authentication or origin rejected",400);return;
    }
    if(upload.filename.endsWith(".manifest")) {
      if(!otaLeaseHeld||!otaSafeState()) {rejectOtaUpload("PREPARE drain incomplete or lease unavailable",409);return;}
      if(!syncOtaLease("UPDATING")) {rejectOtaUpload("recording no longer STOPPED",409);return;}
      if(!otaActivateIfDrained()) {rejectOtaUpload("new BLE data arrived before ACTIVE",409);return;}
      resetOtaUpload();otaUploadActive=true;
      return;
    }
    if(upload.filename.endsWith(".bin")&&otaMode==OTA_ACTIVE&&otaManifestReady&&otaUploadActive&&otaSafeState()) {
      if(!Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH)) {
        logEvent("OTA Update.begin failed code=%u",(unsigned)Update.getError());
        rejectOtaUpload("Update.begin failed",400);return;
      }
      otaUpdateStarted=true;mbedtls_sha256_init(&otaShaContext);
      otaShaStarted=mbedtls_sha256_starts(&otaShaContext,0)==0;
      if(!otaShaStarted){mbedtls_sha256_free(&otaShaContext);rejectOtaUpload("SHA-256 initialization failed",400);return;}
      otaReceivedBytes=0;return;
    }
    rejectOtaUpload("unexpected file or unsafe BIN start",400);return;
  }
  if(upload.status==UPLOAD_FILE_WRITE) {
    if(upload.filename.endsWith(".manifest")) {
      if(otaMode!=OTA_ACTIVE||otaManifestLength+upload.currentSize>=sizeof(otaManifestBuffer)) {
        rejectOtaUpload("manifest too large or OTA state changed",400);return;
      }
      memcpy(otaManifestBuffer+otaManifestLength,upload.buf,upload.currentSize);otaManifestLength+=upload.currentSize;
      otaDeadlineAt=uptimeMs()+OTA_ACTIVE_TIMEOUT_MS;
      return;
    }
    if(upload.filename.endsWith(".bin")&&otaMode==OTA_ACTIVE&&otaUpdateStarted&&otaShaStarted) {
      if(mbedtls_sha256_update(&otaShaContext,upload.buf,upload.currentSize)!=0) {
        rejectOtaUpload("SHA-256 update failed",400);return;
      }
      const size_t written=Update.write(upload.buf,upload.currentSize);
      if(written!=upload.currentSize) {
        logEvent("OTA Update.write failed code=%u written=%lu expected=%lu",(unsigned)Update.getError(),
          (unsigned long)written,(unsigned long)upload.currentSize);
        rejectOtaUpload("Update.write incomplete",400);return;
      }
      otaReceivedBytes+=written;otaDeadlineAt=uptimeMs()+OTA_ACTIVE_TIMEOUT_MS;
      return;
    }
    rejectOtaUpload("unexpected upload chunk",400);return;
  }
  if(upload.status==UPLOAD_FILE_END&&upload.filename.endsWith(".manifest")) {
    logEvent("OTA manifest received bytes=%lu expected=%lu capacity=%lu",
      (unsigned long)otaManifestLength,(unsigned long)upload.totalSize,(unsigned long)(sizeof(otaManifestBuffer)-1));
    if(!otaManifestLength||upload.totalSize!=otaManifestLength) {
      rejectOtaUpload("manifest byte count mismatch",400);return;
    }
    otaManifestBuffer[otaManifestLength]=0;otaManifestReady=verifyOtaManifest(otaManifestBuffer);
    if(!otaManifestReady)rejectOtaUpload("manifest signature, version, or format invalid",400);
    return;
  }
  if(upload.status==UPLOAD_FILE_END&&upload.filename.endsWith(".bin")&&otaShaStarted&&otaUpdateStarted) {
    if(!otaReceivedBytes||upload.totalSize!=otaReceivedBytes) {
      logEvent("OTA BIN size mismatch received=%lu reported=%lu",(unsigned long)otaReceivedBytes,(unsigned long)upload.totalSize);
      rejectOtaUpload("BIN byte count mismatch",400);return;
    }
    uint8_t digest[32];
    if(mbedtls_sha256_finish(&otaShaContext,digest)!=0){rejectOtaUpload("SHA-256 finish failed",400);return;}
    memcpy(otaSignature,digest,sizeof(digest));mbedtls_sha256_free(&otaShaContext);otaShaStarted=false;otaBinaryReady=true;
    return;
  }
  rejectOtaUpload("unexpected upload completion",400);
}

void startOtaWebServer() {
  if(otaServerReady)return;
  const char* headers[]={"Host","Origin"};otaServer.collectHeaders(headers,2);
  otaServer.on("/",HTTP_GET,handleOtaPage);
  otaServer.on("/prepare",HTTP_POST,handleOtaPrepare);
  otaServer.on("/status",HTTP_GET,handleOtaStatus);
  otaServer.on("/cancel",HTTP_POST,handleOtaCancel);
  otaServer.on("/update",HTTP_POST,handleOtaUpload,handleOtaUploadProgress);
  otaServer.on("/nrf-dfu",HTTP_GET,handleNrfPackagePage);
  otaServer.on("/nrf-dfu/upload",HTTP_POST,handleNrfPackageUploadComplete,handleNrfPackageUploadProgress);
  otaServer.on("/nrf-dfu/status",HTTP_GET,handleNrfDfuStatus);
  otaServer.on("/nrf-dfu/pair",HTTP_POST,handleNrfDfuPair);
  otaServer.on("/nrf-dfu/start",HTTP_POST,handleNrfDfuStart);
  otaServer.on("/nrf-dfu/cancel",HTTP_POST,handleNrfDfuCancel);
  otaServer.onNotFound([](){otaServer.send(404,"text/plain","Not found");});
  otaServer.begin();otaServerReady=true;
  logEvent("OTA local updater listening at http://%s/",WiFi.localIP().toString().c_str());
}

void checkOtaBootValidation(uint64_t now) {
#ifdef CONFIG_APP_ROLLBACK_ENABLE
  if(!otaPendingVerify||now-otaBootAt<30000ULL)return;
  if(!spool.mounted||!bleConnected||!firstFrameAt||firstFrameAt<otaBootAt||
     WiFi.status()!=WL_CONNECTED||!otaLastCloudAckAt||otaLastCloudAckAt<otaBootAt||
     now-otaLastCloudAckAt>60000ULL)return;
  if(esp_ota_mark_app_valid_cancel_rollback()==ESP_OK) {
    otaPendingVerify=false;logEvent("OTA boot self-check passed; rollback cancelled");
    otaRainbowStartedAt=now;otaRainbowLastFrameAt=0;otaRainbowActive=true;
  }
#endif
}

#ifdef CONFIG_APP_ROLLBACK_ENABLE
extern "C" bool verifyRollbackLater() { return true; }
#endif

void startWifiPortal() {
  if(wifiManager.getConfigPortalActive())return;
  // In nonblocking mode startConfigPortal() returns false even when active.
  wifiManager.startConfigPortal(AP_NAME,AP_PASSWORD);
  if(!wifiManager.getConfigPortalActive())logEvent("ERROR Wi-Fi configuration portal inactive after start");
  else if(!(WiFi.getMode()&WIFI_AP))logEvent("ERROR Wi-Fi portal active without AP mode");
  else logEvent("Wi-Fi configuration portal active (AP mode)");
}

void serviceWifi(uint64_t now) {
  bool disconnectEvent=false;
  uint8_t disconnectReason=0;
  portENTER_CRITICAL(&stateMux);
  disconnectEvent=wifiDisconnectEventPending;
  disconnectReason=wifiLastDisconnectReason;
  wifiDisconnectEventPending=false;
  portEXIT_CRITICAL(&stateMux);

  if(WiFi.status()==WL_CONNECTED){
    const bool newlyConnected=!wifiWasConnected;
    wifiWasConnected=true;wifiTrying=false;wifiDisconnectPending=false;wifiTried=0;
    wifiActiveSlot=-1;
    for(uint8_t i=0;i<wifiNetworkCount;++i)if(WiFi.SSID()==wifiNetworks[i].ssid)wifiActiveSlot=i;
    if(newlyConnected) {
      if(wifiActiveSlot>=0)rememberSuccessfulWifiSlot((uint8_t)wifiActiveSlot);
      logEvent("Wi-Fi connected slot=%d ssid=%s ip=%s",(int)wifiActiveSlot,WiFi.SSID().c_str(),WiFi.localIP().toString().c_str());
      // mDNS follows ESP-NETIF link events internally. Keep one responder for
      // the process lifetime instead of recreating its UDP PCB during Wi-Fi
      // slot failover while HTTPS work can be active on another task.
      if(!mdnsReady) {
        mdnsReady=MDNS.begin("foodmonitor");
        if(mdnsReady)MDNS.addService("http","tcp",80);
        else logEvent("mDNS unavailable; use IP address http://%s/",WiFi.localIP().toString().c_str());
      }
      if(mdnsReady)logEvent("mDNS updater address=http://foodmonitor.local/");
    }
    return; // Never scan or switch a healthy connection.
  }
  if(wifiWasConnected) {
    wifiWasConnected=false;wifiTrying=false;wifiDisconnectPending=false;wifiTried=0;
    wifiNextSlot=wifi_recovery::preferredSlot(wifiPreferredSlot,wifiNetworkCount);
    wifiRetryAt=now+wifi_recovery::SLOT_SETTLE_MS;
    logEvent("Wi-Fi link lost reason=%u; automatic recovery starts at preferred slot=%u",
      (unsigned)disconnectReason,(unsigned)wifiNextSlot);
  }
  if(disconnectEvent) {
    logEvent("Wi-Fi disconnect event reason=%u status=%d",(unsigned)disconnectReason,(int)WiFi.status());
  }
  if(wifiManager.getConfigPortalActive())return; // Portal owns STA while configuring.
  if(wifiDisconnectPending) {
    if(!wifi_recovery::disconnectComplete(disconnectEvent,(uint32_t)(now-wifiDisconnectAt)))return;
    if(!disconnectEvent)logEvent("Wi-Fi disconnect event timeout; continuing failover after bounded fallback");
    wifiDisconnectPending=false;
  }
  if(disconnectEvent&&wifiTrying&&wifi_recovery::staleLocalLeave(wifiDisconnectPending,disconnectReason)) {
    logEvent("Wi-Fi stale local-leave event ignored during slot=%u attempt",(unsigned)wifiNextSlot);
  } else if(disconnectEvent&&wifiTrying) {
    const uint8_t failedSlot=wifiNextSlot;
    wifiTrying=false;++wifiTried;
    wifiNextSlot=wifi_recovery::nextSlot(wifiNextSlot,wifiNetworkCount);
    logEvent("Wi-Fi connect failed slot=%u reason=%u; next slot=%u",
      (unsigned)failedSlot,(unsigned)disconnectReason,(unsigned)wifiNextSlot);
    if(wifi_recovery::cycleComplete(wifiTried,wifiNetworkCount)){
      wifiRetryAt=now+WIFI_RETRY_MS;wifiTried=0;
      wifiNextSlot=wifi_recovery::preferredSlot(wifiPreferredSlot,wifiNetworkCount);
      logEvent("Wi-Fi networks unavailable; retrying preferred slot=%u in %lu ms",
        (unsigned)wifiNextSlot,(unsigned long)WIFI_RETRY_MS);
    } else wifiRetryAt=now+wifi_recovery::SLOT_SETTLE_MS;
  }
  if(wifiTrying){
    if(now-wifiAttemptAt<WIFI_CONNECT_MS)return;
    const uint8_t timedOutSlot=wifiNextSlot;
    logEvent("Wi-Fi connect timeout slot=%u ssid=%s; cancelling STA attempt before failover",
      (unsigned)timedOutSlot,wifiNetworks[timedOutSlot].ssid);
    WiFi.disconnect(false,false);wifiDisconnectPending=true;wifiDisconnectAt=now;
    wifiTrying=false;++wifiTried;
    wifiNextSlot=wifi_recovery::nextSlot(wifiNextSlot,wifiNetworkCount);
    if(wifi_recovery::cycleComplete(wifiTried,wifiNetworkCount)){
      wifiRetryAt=now+WIFI_RETRY_MS;wifiTried=0;
      wifiNextSlot=wifi_recovery::preferredSlot(wifiPreferredSlot,wifiNetworkCount);
      logEvent("Wi-Fi networks unavailable; retrying preferred slot=%u in %lu ms",
        (unsigned)wifiNextSlot,(unsigned long)WIFI_RETRY_MS);
      return;
    }
    wifiRetryAt=now+wifi_recovery::SLOT_SETTLE_MS;
    return;
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
  logEvent("Wi-Fi connect attempt slot=%u ssid=%s",(unsigned)wifiNextSlot,wifiNetworks[wifiNextSlot].ssid);
}

void initGatewayPixel() {

  gatewayPixel.begin();gatewayPixel.setBrightness(NEOPIXEL_BRIGHTNESS);

  gatewayPixel.setPixelColor(0,gatewayPixel.Color(0,0,255));gatewayPixel.show();

  shownPixel=PIXEL_BOOT;logEvent("NeoPixel init GPIO=%u brightness=%u state=BOOT/BLUE",NEOPIXEL_PIN,NEOPIXEL_BRIGHTNESS);

}

void setGatewayPixel(PixelState state) {

  if(state==shownPixel)return;

  uint32_t color=0;

  if(state==PIXEL_BOOT||state==PIXEL_UPDATE)color=gatewayPixel.Color(0,0,255);

  else if(state==PIXEL_BLE_OFF)color=gatewayPixel.Color(255,0,0);

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

                     uint32_t spoolDepth,bool recordingKnown,bool recording,bool backlog,
                     bool updateActive) {

  bool ble;Sample sample;bool sampleKnown;

  portENTER_CRITICAL(&stateMux);ble=bleConnected;sample=latest;sampleKnown=haveLatest;portEXIT_CRITICAL(&stateMux);

  if(otaRainbowActive) {
    if(otaRainbowVisible(otaRainbowStartedAt,now)) {
      if(!otaRainbowLastFrameAt||now-otaRainbowLastFrameAt>=OTA_RAINBOW_FRAME_MS) {
        const uint8_t phase=(uint8_t)(((now-otaRainbowStartedAt)/OTA_RAINBOW_FRAME_MS)*256/(OTA_RAINBOW_MS/OTA_RAINBOW_FRAME_MS));
        gatewayPixel.setPixelColor(0,otaRainbowColor(phase));gatewayPixel.show();
        otaRainbowLastFrameAt=now;
      }
    } else {otaRainbowActive=false;shownPixel=PIXEL_BOOT;}
  }
  if(!otaRainbowActive)setGatewayPixel(pixelState(ble,wifi,cloud,backlog,updateActive));

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

  xSemaphoreTake(otaIngressMutex,portMAX_DELAY);
  const bool espOtaPaused=otaMode!=OTA_NORMAL;
  const bool nrfPaused=nrfDfuMode!=NRF_DFU_NORMAL;
  const bool otaPaused=espOtaPaused||nrfPaused;
  if(espOtaPaused)++otaGapSamples;
  if(nrfPaused)++nrfDfuGapSamples;
  xSemaphoreGive(otaIngressMutex);
  if(otaPaused) {
    logEvent(espOtaPaused?"OTA measurement gap: incoming BLE sample not accepted":"nRF DFU measurement gap: incoming BLE sample not accepted");
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
    xSemaphoreTake(otaIngressMutex,portMAX_DELAY);
    const bool espOtaPausedAtQueue=otaMode!=OTA_NORMAL;
    const bool nrfPausedAtQueue=nrfDfuMode!=NRF_DFU_NORMAL;
    const bool pausedAtQueue=espOtaPausedAtQueue||nrfPausedAtQueue;
    if(espOtaPausedAtQueue)++otaGapSamples;
    if(nrfPausedAtQueue)++nrfDfuGapSamples;
    const BaseType_t queued=pausedAtQueue?pdFALSE:xQueueSend(samplesQueue,&env,0);
    xSemaphoreGive(otaIngressMutex);
    if(pausedAtQueue) {
      portENTER_CRITICAL(&stateMux);environmentState.historyRejected();portEXIT_CRITICAL(&stateMux);
      logEvent(espOtaPausedAtQueue?"OTA measurement gap: environment history not queued":"nRF DFU measurement gap: environment history not queued");return;
    }
    if(queued!=pdTRUE) {
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

  xSemaphoreTake(otaIngressMutex,portMAX_DELAY);
  const bool espOtaPausedAtQueue=otaMode!=OTA_NORMAL;
  const bool nrfPausedAtQueue=nrfDfuMode!=NRF_DFU_NORMAL;
  const bool pausedAtQueue=espOtaPausedAtQueue||nrfPausedAtQueue;
  if(espOtaPausedAtQueue)++otaGapSamples;
  if(nrfPausedAtQueue)++nrfDfuGapSamples;
  const BaseType_t queued=pausedAtQueue?pdFALSE:xQueueSend(samplesQueue,&s,0);
  xSemaphoreGive(otaIngressMutex);
  if(pausedAtQueue){logEvent(espOtaPausedAtQueue?"OTA measurement gap: gas seq=%lu not queued":"nRF DFU measurement gap: gas seq=%lu not queued",(unsigned long)s.seq);return;}
  if(queued!=pdTRUE) {

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

    portENTER_CRITICAL(&stateMux);bleConnected=false;connectedSensorId[0]=0;connectedSensorAddress[0]=0;
    connectedSensorFirmware[0]=0;framer.reset();portEXIT_CRITICAL(&stateMux);

    logEvent("BLE DISCONNECTED; partial frame cleared");

  }

};

ClientCallbacks clientCallbacks;

class ScanCallbacks:public BLEAdvertisedDeviceCallbacks {

  void onResult(BLEAdvertisedDevice dev)override {

    if(targetDevice)return;

    if(dev.haveServiceUUID()&&dev.isAdvertisingService(NUS_SERVICE_UUID)) {

      char wanted[17];portENTER_CRITICAL(&stateMux);memcpy(wanted,targetSensorId,sizeof(wanted));portEXIT_CRITICAL(&stateMux);
      if(wanted[0]) {
        const String expected=String("BME688-")+wanted;
        if(!dev.haveName()||String(dev.getName().c_str())!=expected)return;
      }

      targetDevice=new BLEAdvertisedDevice(dev);BLEDevice::getScan()->stop();

    }

  }

};

ScanCallbacks scanCallbacks;

void updateNrfDfuProgress(uint32_t sent,uint32_t total,void*) {
  portENTER_CRITICAL(&stateMux);nrfDfuBytesSent=sent;nrfDfuBytesTotal=total;
  snprintf(nrfDfuLastResult,sizeof(nrfDfuLastResult),"Transferring firmware %lu/%lu bytes",
    (unsigned long)sent,(unsigned long)total);portEXIT_CRITICAL(&stateMux);
}

void bleTask(void*) {

  BLEDevice::init("FoodMonitor-ESP32");

  bleClient=BLEDevice::createClient();bleClient->setClientCallbacks(&clientCallbacks);

  while(true) {

    xSemaphoreTake(otaIngressMutex,portMAX_DELAY);
    const bool otaPaused=otaMode!=OTA_NORMAL;
    const NrfDfuMode dfuMode=nrfDfuMode;
    xSemaphoreGive(otaIngressMutex);
    if(otaPaused) {
      if(bleClient&&bleClient->isConnected())bleClient->disconnect();
      vTaskDelay(pdMS_TO_TICKS(1000));continue;
    }

    if(dfuMode==NRF_DFU_PREPARE) {
      // Keep the current, explicitly paired application connection available
      // for buttonless DFU entry while the main loop drains pre-existing data.
      vTaskDelay(pdMS_TO_TICKS(100));continue;
    }

    if(dfuMode==NRF_DFU_ACTIVE) {
      char error[128]={};
      const BLEAddress applicationAddress=bleClient->getPeerAddress();
      portENTER_CRITICAL(&stateMux);nrfDfuTransferStartedAt=uptimeMs();
      snprintf(nrfDfuLastResult,sizeof(nrfDfuLastResult),"Entering the paired sensor bootloader");portEXIT_CRITICAL(&stateMux);
      logEvent("nRF DFU ACTIVE; MTU=23 PRN=8 measurement gap continues");
      const bool ok=nrfDfuRelay.run(SPIFFS,NRF_DFU_PACKAGE_PATH,nrfDfuPackage,bleClient,
        applicationAddress,&nrfDfuCancelRequested,error,sizeof(error),updateNrfDfuProgress,nullptr);
      xSemaphoreTake(otaIngressMutex,portMAX_DELAY);
      nrfDfuMode=ok?NRF_DFU_RECONNECT:NRF_DFU_NORMAL;
      nrfDfuDeadlineAt=ok?uptimeMs()+NRF_DFU_RECONNECT_TIMEOUT_MS:0;
      nrfDfuCancelRequested=false;
      if(ok)snprintf(nrfDfuLastResult,sizeof(nrfDfuLastResult),"Transfer validated; waiting for sensor reconnect");
      else {nrfDfuHasResult=true;nrfDfuLastSuccess=false;snprintf(nrfDfuLastResult,sizeof(nrfDfuLastResult),"%.120s",error);}
      xSemaphoreGive(otaIngressMutex);
      if(ok)logEvent("nRF DFU transfer validated and activated; waiting for NUS reconnect");
      else logEvent("nRF DFU FAILED: %s; normal collection resumed",error);
      vTaskDelay(pdMS_TO_TICKS(1000));continue;
    }

    bool disconnectForRetarget=false;portENTER_CRITICAL(&stateMux);disconnectForRetarget=pairingDisconnectRequested;pairingDisconnectRequested=false;portEXIT_CRITICAL(&stateMux);
    if(disconnectForRetarget&&bleClient&&bleClient->isConnected()) {logEvent("BLE disconnect for pairing target change");bleClient->disconnect();}

    if(!bleConnected) {

      delete targetDevice;targetDevice=nullptr;

      logEvent("BLE scan");

      BLEScan* scan=BLEDevice::getScan();scan->setAdvertisedDeviceCallbacks(&scanCallbacks);scan->setActiveScan(true);

      scan->start(3,false);scan->clearResults();

      xSemaphoreTake(otaIngressMutex,portMAX_DELAY);
      const bool mayConnect=otaMode!=OTA_ACTIVE;
      xSemaphoreGive(otaIngressMutex);
      if(mayConnect&&targetDevice && bleClient->connect(targetDevice)) {

        const String peerAddress=bleClient->getPeerAddress().toString();
        portENTER_CRITICAL(&stateMux);connectedSensorId[0]=0;connectedSensorFirmware[0]=0;
        peerAddress.toCharArray(connectedSensorAddress,sizeof(connectedSensorAddress));portEXIT_CRITICAL(&stateMux);
        if(targetDevice->haveName()) {
          String name(targetDevice->getName().c_str());
          if(name.startsWith("BME688-")&&name.length()==23) {
            char sensor[17];name.substring(7).toCharArray(sensor,sizeof(sensor));
            portENTER_CRITICAL(&stateMux);memcpy(connectedSensorId,sensor,sizeof(sensor));portEXIT_CRITICAL(&stateMux);
          }
        }

        BLERemoteService* service=bleClient->getService(NUS_SERVICE_UUID);

        BLERemoteCharacteristic* tx=service?service->getCharacteristic(NUS_TX_UUID):nullptr;

        if(tx&&tx->canNotify()) {

          portENTER_CRITICAL(&stateMux);framer.reset();portEXIT_CRITICAL(&stateMux);

          tx->registerForNotify(notifyCallback);bleConnected=bleClient->isConnected();

          if(!bleConnectedAt)bleConnectedAt=uptimeMs();
          char sensorIdSnapshot[17];portENTER_CRITICAL(&stateMux);memcpy(sensorIdSnapshot,connectedSensorId,sizeof(sensorIdSnapshot));portEXIT_CRITICAL(&stateMux);
          BLEUUID disUuid((uint16_t)0x180A),firmwareUuid((uint16_t)0x2A26);
          BLERemoteService* dis=bleClient->getService(disUuid);
          BLERemoteCharacteristic* revision=dis?dis->getCharacteristic(firmwareUuid):nullptr;
          const String firmware=revision&&revision->canRead()?revision->readValue():String();
          portENTER_CRITICAL(&stateMux);firmware.toCharArray(connectedSensorFirmware,sizeof(connectedSensorFirmware));portEXIT_CRITICAL(&stateMux);
          logEvent("BLE connected and subscribed sensor_id=%s address=%s firmware=%s uptime=%llu ms",
            sensorIdSnapshot[0]?sensorIdSnapshot:"unknown",peerAddress.c_str(),firmware.length()?firmware.c_str():"unknown",(unsigned long long)uptimeMs());

          if(dfuMode==NRF_DFU_RECONNECT) {
            if(firmware.length()) {
              logEvent("nRF DFU SUCCESS; reconnected firmware=%s",firmware.c_str());
              xSemaphoreTake(otaIngressMutex,portMAX_DELAY);nrfDfuMode=NRF_DFU_NORMAL;nrfDfuDeadlineAt=0;
              nrfDfuHasResult=true;nrfDfuLastSuccess=true;
              snprintf(nrfDfuLastResult,sizeof(nrfDfuLastResult),"Reconnected with firmware %.31s",firmware.c_str());xSemaphoreGive(otaIngressMutex);
            } else {
              logEvent("nRF DFU reconnect missing firmware revision; update remains unconfirmed");
              bleClient->disconnect();
            }
          }

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

  cJSON_AddStringToObject(o,"device_id",deviceId);
  char sensorIdSnapshot[17];portENTER_CRITICAL(&stateMux);memcpy(sensorIdSnapshot,connectedSensorId,sizeof(sensorIdSnapshot));portEXIT_CRITICAL(&stateMux);
  cJSON_AddStringToObject(o,"sensor_id",sensorIdSnapshot);
  cJSON_AddStringToObject(o,"firmware_version",FIRMWARE_VERSION);
  cJSON_AddStringToObject(o,"ota_state",otaPendingVerify?"VERIFYING":otaMode!=OTA_NORMAL?"UPDATING":"IDLE");
  if(WiFi.status()==WL_CONNECTED)cJSON_AddStringToObject(o,"local_ip",WiFi.localIP().toString().c_str());

  cJSON_AddBoolToObject(o,"ble_connected",bleConnected);

  if(known||has)cJSON_AddBoolToObject(o,"sensor_ok",ok);

  if(has){addNumber(o,"last_valid_age_ms",uptimeMs()-s.receivedUptime);cJSON* v=cJSON_CreateObject();

    addNumber(v,"t",s.t);addNumber(v,"h",s.h);addNumber(v,"p",s.p);addNumber(v,"g",s.g);cJSON_AddItemToObject(o,"latest",v);}
  const char* mode=h.mode==1?"ENV":h.mode==2?"GAS":h.mode==3?"COOL":h.mode==4?"REC":"LEGACY";
  cJSON_AddStringToObject(o,"sensor_mode",mode);
  if(known&&h.mode){cJSON_AddBoolToObject(o,"environment_ok",h.envOK!=0);
    cJSON_AddBoolToObject(o,"gas_ok",h.gasOK!=0);}
  if(known){
    cJSON_AddBoolToObject(o,"usb_power",h.usbPower!=0);
    cJSON_AddBoolToObject(o,"battery_measurement_valid",h.batteryKnown!=0);
    if(h.batteryKnown)addNumber(o,"battery_mv",h.batteryMillivolts);
    if(h.batteryExtended){
      addNumber(o,"battery_percent",h.batteryPercent);
      static const char* states[]={"DEFAULT","COLLECTING","CALIBRATED","INSUFFICIENT_DATA","INVALID"};
      cJSON_AddStringToObject(o,"battery_calibration_state",states[h.batteryState]);
      if(h.batteryMinMv)addNumber(o,"battery_min_mv",h.batteryMinMv);
      if(h.batteryMaxMv)addNumber(o,"battery_max_mv",h.batteryMaxMv);
      cJSON_AddBoolToObject(o,"battery_calibrated",h.batteryState==2);
    }
  }
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

bool ackUidMatches(const AckIdentity& expected,const char* actual) {

  if(!actual)return false;

  char boot[33],uidText[48];memcpy(boot,expected.boot,32);boot[32]=0;

  const int n=snprintf(uidText,sizeof(uidText),"%s-%lu",boot,(unsigned long)expected.rx);

  return n>0&&n<(int)sizeof(uidText)&&!strcmp(uidText,actual);

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

    ok=cJSON_IsString(u)&&ackUidMatches(job.acks[i],u->valuestring)&&cJSON_IsString(st);

    if(ok){const char* status=st->valuestring;ok=!strcmp(status,"stored")||!strcmp(status,"duplicate")||!strcmp(status,"not_recording")||!strcmp(status,"unassigned_time");

      if(!strcmp(status,"unassigned_time"))++result.unassigned;}

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

    result.heartbeat = job->heartbeat;

    result.http = -1;



    if (WiFi.status() == WL_CONNECTED) {



      String redirectUrl;

      int postCode = -1;

      logHttpsHeapDiagnostic("POST-pre");



      // ========================================================

      // 1단계: Apps Script에 POST

      //

      // 반드시 별도 scope에서 실행한다.

      // 이 scope가 끝나면 POST용 HTTP/TLS 객체가 완전히 파괴된다.

      // ========================================================

      {

        GatewaySecureClient postClient;

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
          logEvent("HTTP POST begin kind=%s uptime=%llu ms bytes=%u",job->heartbeat?"health":"data",(unsigned long long)lastPostAt,(unsigned)strlen(job->json));
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

            if(postCode==HTTPC_ERROR_CONNECTION_REFUSED)logHttpsConnectDiagnostic("POST",postClient);

          }



          post.end();

        } else {

          logEvent("HTTP POST begin failed");

        }



        // 명시적으로 TLS socket 종료

        postClient.stop();

      }

      logHttpsHeapDiagnostic("POST-clean");

      // POST has consumed the request body. Do not keep this comparatively
      // large allocation alive while the redirect GET creates a second TLS
      // context. A retry rebuilds the same JSON from the preserved batch.
      if(job->json){cJSON_free(job->json);job->json=nullptr;}
      logHttpsHeapDiagnostic("POST-body-free");



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

        logHttpsHeapDiagnostic("GET-pre");



        {

          GatewaySecureClient getClient;

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

              if(result.http==HTTPC_ERROR_CONNECTION_REFUSED)logHttpsConnectDiagnostic("redirect",getClient);



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

        logHttpsHeapDiagnostic("GET-clean");

      }

    }



    logEvent(

      "HTTP %d application ACK=%u samples=%lu",

      result.http,

      result.ok,

      (unsigned long)job->count

    );



    if(job->json)cJSON_free(job->json);

    delete job;

    logHttpsHeapDiagnostic("JOB-free");



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
  nrfDfuRelay.setLogger(logEvent);

  logMutex=xSemaphoreCreateMutex();otaIngressMutex=xSemaphoreCreateMutex();samplesQueue=xQueueCreate(RAM_SAMPLES,sizeof(Sample));

  jobsQueue=xQueueCreate(1,sizeof(UploadJob*));resultsQueue=xQueueCreate(1,sizeof(UploadResult));

  if(!logMutex||!samplesQueue||!jobsQueue||!resultsQueue){Serial0.println("FATAL allocation failed");while(true)delay(1000);}

  pinMode(WIFI_RESET_PIN,INPUT_PULLUP);WiFi.mode(WIFI_STA);
  WiFi.onEvent([](WiFiEvent_t,WiFiEventInfo_t info){
    portENTER_CRITICAL(&stateMux);
    wifiLastDisconnectReason=info.wifi_sta_disconnected.reason;wifiDisconnectEventPending=true;
    portEXIT_CRITICAL(&stateMux);
  },ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  initGatewayPixel();initGatewayLcd();

  uint32_t random[4];esp_fill_random(random,sizeof(random));

  snprintf(gatewayBoot,sizeof(gatewayBoot),"%08lx%08lx%08lx%08lx",(unsigned long)random[0],(unsigned long)random[1],(unsigned long)random[2],(unsigned long)random[3]);
  snprintf(deviceId,sizeof(deviceId),"%012llx",(unsigned long long)(ESP.getEfuseMac()&0xffffffffffffULL));
  loadSensorPairing();

  logEvent("BOOT gas-scan-v3 gateway=%s device=%s uptime=%llu ms",gatewayBoot,deviceId,(unsigned long long)uptimeMs());
  logEvent("FIRMWARE build=%s compiled=%s %s flash_chip=%lu psram=%lu",FIRMWARE_VERSION,
    __DATE__,__TIME__,(unsigned long)ESP.getFlashChipSize(),(unsigned long)ESP.getPsramSize());
  loadOtaSettings();
  const esp_partition_t* running=esp_ota_get_running_partition();
  esp_ota_img_states_t otaState=ESP_OTA_IMG_UNDEFINED;
  if(running&&esp_ota_get_state_partition(running,&otaState)==ESP_OK&&otaState==ESP_OTA_IMG_PENDING_VERIFY) {
    otaPendingVerify=true;otaBootAt=uptimeMs();logEvent("OTA boot pending self-check; rollback remains enabled");
  }
  const esp_partition_t* spiffsPartition=esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,nullptr);
  if(spiffsPartition)logEvent("SPIFFS partition offset=0x%lx size=0x%lx",
    (unsigned long)spiffsPartition->address,(unsigned long)spiffsPartition->size);
  else logEvent("ERROR SPIFFS partition missing");

  spool.begin();
  logEvent("SPIFFS mount %s",spool.mounted?"success":"failed");
  recoverNrfPackageFiles();

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

  static bool busy=false, activeJobHeartbeat=false, failed=false, healthHeartbeatFailed=false, haveCloudAck=false;

  static uint64_t due=0, healthHeartbeatDue=0, lastHealthHeartbeatStart=0,
                  healthHeartbeatStartedAt=0, lastHealthHeartbeatAck=0,
                  activeSince=0, lastSpill=0;

  static uint32_t retryDelay=UPLOAD_INTERVAL_MS;

  static int wifiPrevious=-1;

  uint64_t now=uptimeMs();wifiManager.process();handlePairingCommand();

  int wifiState=WiFi.status();

  if(wifiState!=wifiPrevious){
    logEvent("Wi-Fi state=%d uptime=%llu ms",wifiState,(unsigned long long)now);
    if(wifiState==WL_CONNECTED){wifiConnectedAt=now;due=now;healthHeartbeatDue=now;retryDelay=UPLOAD_INTERVAL_MS;}
    wifiPrevious=wifiState;
  }

  serviceWifi(now);
  if(WiFi.status()==WL_CONNECTED)startOtaWebServer();

  ButtonEvents button=bootButton.update(digitalRead(WIFI_RESET_PIN)==HIGH,now);

  if(button.shortPress){lcdMode=lcdMode==LCD_DEBUG?LCD_SENSOR:LCD_DEBUG;

    logEvent(lcdMode==LCD_SENSOR?"LCD MODE SENSOR":"LCD MODE DEBUG");lastLcdRefresh=0;}

  if(button.longPress){logEvent("Wi-Fi registration portal; saved networks and spool preserved");
    wifiTrying=false;

    startWifiPortal();}

  UploadResult result;

  if(xQueueReceive(resultsQueue,&result,0)==pdTRUE) {

    busy=false;activeJobHeartbeat=false;

    if(result.heartbeat) {
      if(result.ok) {
        const uint64_t interval=lastHealthHeartbeatAck?now-lastHealthHeartbeatAck:0;
        const uint64_t duration=healthHeartbeatStartedAt?now-healthHeartbeatStartedAt:0;
        lastHealthHeartbeatAck=now;healthHeartbeatFailed=false;haveCloudAck=true;
        lastAckAt=now;otaLastCloudAckAt=now;lastCloudUpload=clockValid()?utcMs():0;
        if(!strcmp(result.recording,"RECORDING")){cloudRecordingKnown=true;cloudRecording=true;}
        else if(!strcmp(result.recording,"STOPPED")){cloudRecordingKnown=true;cloudRecording=false;}
        logEvent("Health heartbeat ACK interval=%llu ms duration=%llu ms",
          (unsigned long long)interval,(unsigned long long)duration);
      } else {
        ++uploadFailures;healthHeartbeatFailed=true;
        // A transport failure gets one full quiet interval after completion.
        // Successful heartbeats remain start-to-start and never overlap because busy stays set.
        healthHeartbeatDue=now+HEALTH_HEARTBEAT_INTERVAL_MS;
        logEvent("Health heartbeat failed http=%d; retry in %lu ms",
          result.http,(unsigned long)HEALTH_HEARTBEAT_INTERVAL_MS);
      }
    } else if(result.ok) {

      // DATA INTEGRITY: only exact application ACK can release a batch. A

      // retry keeps the original UIDs; a reboot may safely replay a disk chunk.

      if(!activePath.isEmpty()&&!spool.acknowledge(activePath,activeCount))logEvent("ERROR spool ACK cleanup; replay/repair required");

      pending.release(result.ok);activePath="";failed=false;retryDelay=UPLOAD_INTERVAL_MS;

      haveCloudAck=true;
      lastAckAt=now;
      otaLastCloudAckAt=now;
      logEvent("MILESTONE application ACK uptime=%llu ms",(unsigned long long)now);

      if(!strcmp(result.recording,"RECORDING")){cloudRecordingKnown=true;cloudRecording=true;}

      else if(!strcmp(result.recording,"STOPPED")){cloudRecordingKnown=true;cloudRecording=false;}

      lastCloudUpload=clockValid()?utcMs():0;unassignedCount+=result.unassigned;
      healthHeartbeatDue=now+HEALTH_HEARTBEAT_INTERVAL_MS;
      healthHeartbeatFailed=false;

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

  otaPendingSamples=uxQueueMessagesWaiting(samplesQueue)+activeCount+stagedCount+spool.count;
  otaActiveSamples=activeCount;otaStagedSamples=stagedCount;
  otaHttpBusy=busy;otaHttpFailed=failed;
  if(otaServerReady)otaServer.handleClient(); // OTA request wins before another data job starts.
  if(nrfPackageUploadActive&&uptimeMs()-nrfPackageUploadLastAt>60000ULL)
    failNrfPackageUpload("upload timed out",408);
  const bool otaMaintenanceNetworkBusy=serviceDeferredOtaLeaseRelease();

  if(nrfDfuMode==NRF_DFU_PREPARE) {
    if(now>nrfDfuDeadlineAt||!cloudRecordingKnown||cloudRecording||spool.fault) {
      logEvent("nRF DFU PREPARE failed: %s; normal collection resumed",
        now>nrfDfuDeadlineAt?"timeout":!cloudRecordingKnown?"recording unknown":cloudRecording?"recording active":"SPIFFS fault");
      xSemaphoreTake(otaIngressMutex,portMAX_DELAY);nrfDfuMode=NRF_DFU_NORMAL;nrfDfuDeadlineAt=0;
      nrfDfuHasResult=true;nrfDfuLastSuccess=false;snprintf(nrfDfuLastResult,sizeof(nrfDfuLastResult),"Preparation failed or timed out");xSemaphoreGive(otaIngressMutex);
    } else if(!activeCount&&!stagedCount&&!spool.count&&!busy&&!failed&&
              !uxQueueMessagesWaiting(samplesQueue)&&!uxQueueMessagesWaiting(resultsQueue)&&bleConnected) {
      xSemaphoreTake(otaIngressMutex,portMAX_DELAY);
      if(nrfDfuMode==NRF_DFU_PREPARE)nrfDfuMode=NRF_DFU_ACTIVE;
      snprintf(nrfDfuLastResult,sizeof(nrfDfuLastResult),"Starting transfer to paired sensor");
      xSemaphoreGive(otaIngressMutex);
      logEvent("nRF DFU PREPARE complete; existing data preserved and drained");
    }
  } else if(nrfDfuMode==NRF_DFU_RECONNECT&&now>nrfDfuDeadlineAt) {
    logEvent("nRF DFU reconnect timeout; update result unconfirmed, normal collection resumed");
    xSemaphoreTake(otaIngressMutex,portMAX_DELAY);nrfDfuMode=NRF_DFU_NORMAL;nrfDfuDeadlineAt=0;
    nrfDfuHasResult=true;nrfDfuLastSuccess=false;snprintf(nrfDfuLastResult,sizeof(nrfDfuLastResult),"Reconnect timed out; update result is unconfirmed");xSemaphoreGive(otaIngressMutex);
  }

  if(otaMode==OTA_PREPARE&&!otaLeaseHeld&&otaSafeState()) {
    otaHttpBusy=true;
    const bool leaseReady=syncOtaLease("UPDATING");
    otaHttpBusy=false;
    if(leaseReady) {
      otaLeaseHeld=true;
      logEvent("OTA PREPARE cloud lease confirmed; recording STOPPED");
    } else {
      logEvent("OTA reject HTTP 409: cloud lease or recording not STOPPED");
      abortOtaSession();
    }
  }

  const uint32_t queuedForUpload=uxQueueMessagesWaiting(samplesQueue);
  if(shouldStartDataUpload(busy,nrfPackageUploadActive||otaMode==OTA_ACTIVE||nrfDfuMode==NRF_DFU_ACTIVE||nrfDfuMode==NRF_DFU_RECONNECT,otaMaintenanceNetworkBusy||otaLeaseReleasePending,now,due,
       activeCount,stagedCount,spool.count,queuedForUpload)) {

    // Preserve unreadable disk contents; current-boot RAM samples may still upload with stable UIDs.
    const bool ramOnlyOnline=spool.mountFailedAtBoot()&&wifiState==WL_CONNECTED;
    if(!activeCount&&(!spool.fault||ramOnlyOnline)) {

      if(spool.count)spool.oldest(active,activeCount,activePath);

      else if(stagedCount&&ramOnlyOnline) {
        memcpy(active,staging,stagedCount*sizeof(Sample));activeCount=stagedCount;
        stagedCount=0;activeSince=now;
      } else if(!stagedCount){activeCount=drain(active);activeSince=now;}

    }

    if(!activeCount) {
      due=now+UPLOAD_INTERVAL_MS;
      logEvent("ERROR pending upload data could not form a non-empty batch");
    } else {
      UploadJob* job=new UploadJob();job->count=activeCount;job->heartbeat=false;
      for(uint32_t i=0;i<activeCount;++i){memcpy(job->acks[i].boot,active[i].boot,32);job->acks[i].rx=active[i].rx;}

      cJSON* root=cJSON_CreateObject();addNumber(root,"version",3);
      char sensorIdSnapshot[17];portENTER_CRITICAL(&stateMux);memcpy(sensorIdSnapshot,connectedSensorId,sizeof(sensorIdSnapshot));portEXIT_CRITICAL(&stateMux);
      cJSON_AddStringToObject(root,"device_id",deviceId);cJSON_AddStringToObject(root,"sensor_id",sensorIdSnapshot);
      cJSON* arr=cJSON_AddArrayToObject(root,"samples");

      for(uint32_t i=0;i<activeCount;++i)cJSON_AddItemToArray(arr,sampleJson(active[i]));

      cJSON_AddItemToObject(root,"health",healthJson(activeCount,stagedCount));job->json=cJSON_PrintUnformatted(root);cJSON_Delete(root);

      if(job->json&&xQueueSend(jobsQueue,&job,0)==pdTRUE){busy=true;activeJobHeartbeat=false;logEvent("Batch upload start n=%lu spool=%lu uptime=%llu ms",(unsigned long)activeCount,(unsigned long)spool.count,(unsigned long long)now);}

      else {if(job->json)cJSON_free(job->json);delete job;due=now+UPLOAD_INTERVAL_MS;logEvent("ERROR upload allocation/queue failed");}
    }

  }

  const bool updateActive=nrfPackageUploadActive||otaMode!=OTA_NORMAL||nrfDfuMode!=NRF_DFU_NORMAL;
  const uint32_t heartbeatQueued=uxQueueMessagesWaiting(samplesQueue);
  if(shouldStartHealthHeartbeat(busy||failed,updateActive,
       otaMaintenanceNetworkBusy||otaLeaseReleasePending,wifiState==WL_CONNECTED,
       now,healthHeartbeatDue,activeCount,stagedCount,spool.count,heartbeatQueued)) {
    UploadJob* job=new UploadJob();job->count=0;job->heartbeat=true;
    cJSON* root=cJSON_CreateObject();addNumber(root,"version",3);
    char sensorIdSnapshot[17];portENTER_CRITICAL(&stateMux);memcpy(sensorIdSnapshot,connectedSensorId,sizeof(sensorIdSnapshot));portEXIT_CRITICAL(&stateMux);
    cJSON_AddStringToObject(root,"device_id",deviceId);cJSON_AddStringToObject(root,"sensor_id",sensorIdSnapshot);
    cJSON_AddArrayToObject(root,"samples");
    cJSON_AddItemToObject(root,"health",healthJson(activeCount,stagedCount));job->json=cJSON_PrintUnformatted(root);cJSON_Delete(root);
    if(job->json&&xQueueSend(jobsQueue,&job,0)==pdTRUE) {
      busy=true;activeJobHeartbeat=true;
      const uint64_t interval=lastHealthHeartbeatStart?now-lastHealthHeartbeatStart:0;
      lastHealthHeartbeatStart=now;healthHeartbeatStartedAt=now;
      healthHeartbeatDue=now+HEALTH_HEARTBEAT_INTERVAL_MS;
      logEvent("Health heartbeat start interval=%llu ms target=%lu ms uptime=%llu ms",
        (unsigned long long)interval,(unsigned long)HEALTH_HEARTBEAT_INTERVAL_MS,
        (unsigned long long)now);
    } else {
      if(job->json)cJSON_free(job->json);delete job;
      healthHeartbeatDue=now+HEALTH_HEARTBEAT_INTERVAL_MS;
      logEvent("ERROR health heartbeat allocation/queue failed; retry in %lu ms",
        (unsigned long)HEALTH_HEARTBEAT_INTERVAL_MS);
    }
  }

  uint32_t queueDepth=uxQueueMessagesWaiting(samplesQueue);

  bool wifiOnline=wifiState==WL_CONNECTED;

  // Health-only work still blocks concurrent update I/O, but a normal heartbeat
  // is not a warning condition for the status LED.
  CloudState cloud=cloudState(wifiOnline,busy&&!activeJobHeartbeat,failed,
                              healthHeartbeatFailed,haveCloudAck);

  bool backlog=queueDepth||spool.count||activeCount||stagedCount||failed;

  if(haveCloudAck&&!backlog&&!backlogDrainedAt){backlogDrainedAt=now;logEvent("MILESTONE backlog drained uptime=%llu ms",(unsigned long long)now);}
  if(backlog)backlogDrainedAt=0;

  otaPendingSamples=queueDepth+activeCount+stagedCount+spool.count;
  otaActiveSamples=activeCount;otaStagedSamples=stagedCount;
  otaHttpBusy=busy;otaHttpFailed=failed;
  checkOtaBootValidation(now);
  if(otaMode!=OTA_NORMAL&&
     (uptimeMs()>otaDeadlineAt||WiFi.status()!=WL_CONNECTED||spool.fault)) {
    logEvent("OTA %s; normal collection resuming",
      uptimeMs()>otaDeadlineAt?"timed out":WiFi.status()!=WL_CONNECTED?"Wi-Fi disconnected":"SPIFFS fault");
    abortOtaSession();
  }

  updateGatewayUi(now,wifiOnline,cloud,queueDepth,spool.count,cloudRecordingKnown,cloudRecording,backlog,updateActive);

  delay(10);

}
