#include <cassert>
#include <algorithm>
#include <map>
#include <iostream>
#include "remote_stubs/Arduino.h"
#include "remote_stubs/cJSON.h"
#include "remote_stubs/mbedtls/sha256.h"
#include "../firmware/esp32s3/gateway/RemoteUpdateProtocol.h"
#include "../firmware/esp32s3/gateway/NrfDfuPackagePolicy.h"
using std::min;
uint64_t ticks=100,otaBootAt=0,firstFrameAt=0,otaLastCloudAckAt=0;
uint64_t uptimeMs(){return ticks;}uint64_t utcMs(){return 1800000000000ULL+ticks;}bool clockValid(){return true;}void delay(unsigned n){ticks+=n;}
#define portMAX_DELAY 0
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
int otaIngressMutex=0,stateMux=0;void xSemaphoreTake(int,int){}void xSemaphoreGive(int){}
struct {int status(){return 3;}} WiFi;
constexpr int WL_CONNECTED=3,HTTPC_DISABLE_FOLLOW_REDIRECTS=0,FILE_WRITE=1;
bool strictTls=false,cloudOK=true,validSignature=true,validZip=true,wrongRange=false,cutDownload=false,writeFail=false;
unsigned commits=0,reboots=0,dfuStarts=0;std::string failState;
std::map<std::string,std::vector<uint8_t>> disk,web;
struct File{std::vector<uint8_t>* value=nullptr;operator bool()const{return value;}
 size_t write(const uint8_t* p,size_t n){if(!value||writeFail)return 0;value->insert(value->end(),p,p+n);return n;}void flush(){}void close(){value=nullptr;}};
struct {bool exists(const char* p){return disk.count(p);}bool remove(const char* p){return disk.erase(p)>0;}
 File open(const char* p,int){disk[p].clear();return {&disk[p]};}size_t totalBytes(){return 900000;}size_t usedBytes(){size_t n=0;for(auto& i:disk)n+=i.second.size();return n;}} SPIFFS;
struct GatewaySecureClient{void useBuiltinCACertBundle(){strictTls=true;}void setInsecure(){strictTls=false;}void setHandshakeTimeout(int){}void stop(){}};
struct Stream{std::vector<uint8_t> data;size_t at=0;int available(){return cutDownload&&at>=data.size()/2?0:(int)(data.size()-at);}
 int readBytes(uint8_t* p,size_t n){n=min(n,(size_t)17);memcpy(p,data.data()+at,n);at+=n;ticks+=10;return (int)n;}};
struct HTTPClient{std::string url,range;Stream stream;unsigned total=0,start=0,last=0;
 void setReuse(bool){}void setFollowRedirects(int){}void setConnectTimeout(int){}void setTimeout(int){}void collectHeaders(const char**,int){}
 bool begin(GatewaySecureClient&,const char* p){url=p;return true;}void addHeader(const char* k,const String& v){if(std::string(k)=="Range")range=v.c_str();}
 int GET(){assert(strictTls);if(!web.count(url))return 404;auto& bytes=web[url];total=(unsigned)bytes.size();sscanf(range.c_str(),"bytes=%u-%u",&start,&last);if(last>=total)return 416;stream.data.assign(bytes.begin()+start,bytes.begin()+last+1);return 206;}
 int getSize(){return (int)stream.data.size();}String header(const char* key){if(std::string(key)!="Content-Range")return "";char v[80];snprintf(v,sizeof(v),"bytes %u-%u/%u",wrongRange?start+1:start,last,total);return v;}
 Stream* getStreamPtr(){return &stream;}bool connected(){return stream.available()>0;}void end(){}
};
struct {bool authenticate(const char*,const char*){return true;}void requestAuthentication(){}void sendHeader(const char*,const char*){}void send(int,const char*,const String&){}String arg(const char*){return "";}} otaServer;
bool otaSameOrigin(){return true;}char otaPassword[33]="test",deviceId[13]="111111111111",targetSensorId[17]="0123456789abcdef",connectedSensorId[17]="0123456789abcdef",connectedSensorFirmware[32]="2.0.12";
char FIRMWARE_VERSION[24]="2.0.21";uint64_t OTA_BUILD_NUMBER=2026102706;
enum OtaMode{OTA_NORMAL,OTA_PREPARE,OTA_ACTIVE};OtaMode otaMode=OTA_NORMAL;
enum NrfDfuMode{NRF_DFU_NORMAL,NRF_DFU_PREPARE,NRF_DFU_ACTIVE,NRF_DFU_RECONNECT};NrfDfuMode nrfDfuMode=NRF_DFU_NORMAL;
bool remoteOwnsUpdate=false,otaPendingVerify=false,bleConnected=false,cloudRecordingKnown=true,cloudRecording=false,otaHttpBusy=false,otaHttpFailed=false,otaLeaseHeld=false,otaLeaseReleasePending=false,otaLeaseReleaseDeferredOnce=false,otaRollbackApproved=false,nrfDfuCancelRequested=false,nrfDfuHasResult=false,nrfDfuLastSuccess=false;
const char* otaUploadFailure=nullptr;uint32_t otaPendingSamples=0,otaGapSamples=0,nrfDfuBytesSent=0,nrfDfuBytesTotal=0;uint64_t otaDeadlineAt=0,nrfDfuTransferStartedAt=0;
constexpr uint32_t OTA_ACTIVE_TIMEOUT_MS=120000,OTA_PREPARE_TIMEOUT_MS=180000;
struct{bool mounted=true,fault=false;size_t budget=500000;}spool;
QueueHandle_t resultsQueue=xQueueCreate(1,1);
struct{uint64_t receivedUptime=0;}latest;bool haveLatest=false;
struct{bool known=false;uint64_t receivedAt=0;}environmentState;
struct NrfLegacyDfuRelay{struct PackageInfo{};};
bool validateNrfPackageAt(const char*,NrfLegacyDfuRelay::PackageInfo&,char*,size_t){return validZip;}
bool promoteNrfPackage(char*,size_t){disk[nrf_dfu_package::FINAL_PATH]=disk[nrf_dfu_package::TEMP_PATH];disk.erase(nrf_dfu_package::TEMP_PATH);return true;}
struct Client {
 struct Record{uint32_t seq=0;char id[33]={},kind[6]={},version[24]={},build[21]={},state[16]="IDLE",error[49]={};uint8_t percent=0;}record;
 remote_update::Command pending;bool queued=false,active=false,ready=true;uint64_t nextPoll=0;
 bool enabled(){return ready;}bool requiresCloudTls(){return true;}void begin(const char*){}
 const char* hosts(){return "updates.example.invalid";}const char* downloadToken(){return "";}
 bool configure(const String&,const String&,const String&){return true;}
 bool state(const char* state,const char* error="",uint8_t pct=0){if(failState==state){failState.clear();ready=false;return false;}strcpy(record.state,state);strcpy(record.error,error);record.percent=pct;return true;}
 bool take(remote_update::Command& c,uint32_t){if(!queued||active)return false;c=pending;queued=false;active=true;strcpy(record.kind,c.kind);strcpy(record.version,c.version);strcpy(record.build,c.build);record.seq=c.seq;return true;}
 void attach(cJSON*,char* nonce,uint64_t,uint32_t,const char*,uint64_t,const char*){nonce[0]=0;}
}remoteClient;
void logEvent(const char*,...){}
void abortOtaSession(){otaMode=OTA_NORMAL;otaLeaseHeld=false;otaLeaseReleasePending=true;}
bool otaSafeToStart(){return !remoteOwnsUpdate&&otaMode==OTA_NORMAL&&nrfDfuMode==NRF_DFU_NORMAL;}
bool otaSafeState(){return spool.mounted&&!spool.fault&&!otaPendingSamples&&!otaHttpBusy&&!bleConnected;}
bool otaActivateIfDrained(){if(otaMode!=OTA_PREPARE||!otaSafeState())return false;otaMode=OTA_ACTIVE;return true;}
bool syncOtaLease(const char*){remoteClient.nextPoll=ticks+15000;otaLastCloudAckAt=ticks;return cloudOK;}
bool requestNrfDfuCancel(char*,size_t){nrfDfuMode=NRF_DFU_NORMAL;return true;}
bool requestNrfDfuStart(char*,size_t,bool){dfuStarts++;nrfDfuMode=NRF_DFU_ACTIVE;nrfDfuTransferStartedAt=ticks;return true;}
size_t otaManifestLength=0,otaReceivedBytes=0;uint64_t otaIncomingBuild=0;char otaIncomingVersion[24]={},otaIncomingDigest[65]={};bool otaManifestReady=false,otaUploadActive=false;
std::vector<uint8_t> written;
bool verifyOtaManifest(char*){if(!validSignature)return false;strcpy(otaIncomingVersion,remoteClient.pending.version);otaIncomingBuild=strtoull(remoteClient.pending.build,nullptr,10);strcpy(otaIncomingDigest,remoteClient.pending.sha);return true;}
bool beginOtaImage(){written.clear();otaReceivedBytes=0;return true;}
bool writeOtaImage(const uint8_t* bytes,size_t size){if(writeFail)return false;written.insert(written.end(),bytes,bytes+size);otaReceivedBytes+=size;return true;}
bool sealOtaImage(){return !written.empty();}bool commitOtaImage(){assert(!strcmp(remoteClient.record.state,"VERIFYING"));commits++;return true;}
struct {void restart(){reboots++;}}ESP;
#include "../firmware/esp32s3/gateway/RemoteUpdateRuntime.h"
static void hash(const std::vector<uint8_t>& bytes,char out[65]){uint8_t d[32];assert(!mbedtls_sha256(bytes.data(),bytes.size(),d,0));for(int i=0;i<32;++i)snprintf(out+i*2,3,"%02x",d[i]);}
static void reset(const char* kind){
 remote_runtime::stage=remote_runtime::NONE;remote_runtime::ownsTemp=false;remote_runtime::hashStarted=false;remote_runtime::zip.close();
 remoteClient=Client{};failState.clear();otaMode=OTA_NORMAL;nrfDfuMode=NRF_DFU_NORMAL;remoteOwnsUpdate=false;otaPendingSamples=0;otaHttpBusy=false;bleConnected=false;otaLeaseHeld=false;otaLeaseReleasePending=false;otaPendingVerify=false;nrfDfuHasResult=false;nrfDfuLastSuccess=false;haveLatest=false;environmentState.known=false;
 cloudOK=validSignature=validZip=true;wrongRange=cutDownload=writeFail=false;commits=reboots=dfuStarts=0;written.clear();disk.clear();web.clear();
 disk["/spool-preserved"]={1,2,3};disk[nrf_dfu_package::FINAL_PATH]={4,5,6};ticks=100;strcpy(FIRMWARE_VERSION,"2.0.21");OTA_BUILD_NUMBER=2026102706;
 auto& c=remoteClient.pending;strcpy(c.kind,kind);strcpy(c.version,!strcmp(kind,"esp32")?"2.0.22":"2.0.12");strcpy(c.build,"2026102801");strcpy(c.sensor,targetSensorId);c.expires=(uint32_t)(utcMs()/1000)+600;c.seq=1;c.bytes=300;
 strcpy(c.url,"https://updates.example.invalid/image");web[c.url]=std::vector<uint8_t>(c.bytes,0x5a);hash(web[c.url],c.sha);
 strcpy(c.manifestUrl,"https://updates.example.invalid/manifest");web[c.manifestUrl]={'s','i','g','n','e','d'};c.manifestBytes=6;hash(web[c.manifestUrl],c.manifestSha);
 remoteClient.queued=true;
}
static void prepare(){serviceRemoteUpdate();assert(otaMode==OTA_PREPARE);otaLeaseHeld=true;serviceRemoteUpdate();assert(otaMode==OTA_ACTIVE);}
int main(){
 reset("esp32");serviceRemoteUpdate();otaPendingSamples=1;serviceRemoteUpdate();assert(commits==0&&remote_runtime::stage==remote_runtime::PREPARE);otaPendingSamples=0;otaLeaseHeld=true;serviceRemoteUpdate();serviceRemoteUpdate();serviceRemoteUpdate();assert(commits==1&&reboots==1);assert(disk["/spool-preserved"]==std::vector<uint8_t>({1,2,3}));
 strcpy(FIRMWARE_VERSION,"2.0.22");OTA_BUILD_NUMBER=2026102801;otaMode=OTA_NORMAL;otaPendingVerify=true;otaBootAt=ticks;beginRemoteUpdate();serviceRemoteUpdate();assert(strcmp(remoteClient.record.state,"SUCCESS"));otaPendingVerify=false;bleConnected=true;firstFrameAt=ticks+1;otaLastCloudAckAt=ticks+2;serviceRemoteUpdate();assert(!strcmp(remoteClient.record.state,"SUCCESS"));
 for(int failure=0;failure<5;++failure){reset("esp32");prepare();if(failure==0)validSignature=false;if(failure==1)wrongRange=true;if(failure==2)web[remoteClient.pending.url][30]^=1;if(failure==3)cutDownload=true;if(failure==4)writeFail=true;serviceRemoteUpdate();if(remote_runtime::stage==remote_runtime::DOWNLOAD)serviceRemoteUpdate();assert(!strcmp(remoteClient.record.state,"FAILED"));assert(commits==0&&reboots==0);assert(disk["/spool-preserved"].size()==3);}
 reset("nrf");prepare();cutDownload=true;serviceRemoteUpdate();assert(!strcmp(remoteClient.record.state,"FAILED"));assert(disk[nrf_dfu_package::FINAL_PATH]==std::vector<uint8_t>({4,5,6}));assert(!disk.count(nrf_dfu_package::TEMP_PATH));
 reset("nrf");prepare();serviceRemoteUpdate();assert(remote_runtime::stage==remote_runtime::WAIT_SENSOR&&dfuStarts==0);bleConnected=true;otaLeaseReleasePending=false;serviceRemoteUpdate();assert(dfuStarts==1);nrfDfuMode=NRF_DFU_NORMAL;nrfDfuHasResult=nrfDfuLastSuccess=true;serviceRemoteUpdate();assert(strcmp(remoteClient.record.state,"SUCCESS"));haveLatest=true;latest.receivedUptime=nrfDfuTransferStartedAt+1;serviceRemoteUpdate();assert(!strcmp(remoteClient.record.state,"SUCCESS"));
 reset("esp32");serviceRemoteUpdate();ticks+=601000;otaLeaseHeld=true;serviceRemoteUpdate();assert(!strcmp(remoteClient.record.error,"COMMAND_EXPIRED")&&commits==0);
 reset("nrf");spool.budget=900000;serviceRemoteUpdate();otaLeaseHeld=true;serviceRemoteUpdate();assert(!strcmp(remoteClient.record.error,"SPOOL_RESERVE_SPACE")&&disk[nrf_dfu_package::FINAL_PATH].size()==3);spool.budget=500000;
 reset("esp32");serviceRemoteUpdate();otaLeaseHeld=true;failState="DOWNLOADING";serviceRemoteUpdate();assert(commits==0&&otaMode==OTA_NORMAL);serviceRemoteUpdate();assert(!remoteOwnsUpdate);
 reset("nrf");prepare();serviceRemoteUpdate();bleConnected=true;otaLeaseReleasePending=false;failState="UPDATING";serviceRemoteUpdate();assert(dfuStarts==0);serviceRemoteUpdate();assert(!remoteOwnsUpdate);
 reset("esp32");otaMode=OTA_PREPARE;serviceRemoteUpdate();assert(otaMode==OTA_PREPARE&&commits==0); // a remote command cannot cancel a local session
 std::cout<<"PASS real remote runtime: safety drain, HTTPS/header/hash/write failures, preserved spool/ZIP, boot validation and nRF version+new-data completion\n";
}
