#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <cJSON.h>
#include <mbedtls/md.h>
#include "RemoteUpdateProtocol.h"

// Main loop owns NVS/state; network worker can only enqueue an authenticated command.
class RemoteUpdateClient {
 public:
  struct Record {
    uint32_t magic=0x52553131,seq=0;
    char id[33]={},kind[6]={},version[24]={},build[21]={},state[16]="IDLE",error[49]={};
    uint32_t expires=0;uint8_t percent=0;
    uint32_t checksum=0;
  } record;
  bool active=false,storageReady=false;
  uint64_t nextPoll=0;
  void begin(const char* device) {
    snprintf(device_,sizeof(device_),"%s",device);
    inbox_=xQueueCreate(1,sizeof(remote_update::Command));storageReady=prefs_.begin("fsm-remote",false);
    if(!storageReady||!inbox_)return;
    String key=prefs_.getString("key",""),hosts=prefs_.getString("hosts","");
    key.toCharArray(key_,sizeof(key_));hosts.toCharArray(hosts_,sizeof(hosts_));
    prefs_.getString("download","").toCharArray(downloadToken_,sizeof(downloadToken_));
    if(prefs_.isKey("job")) {
      if(prefs_.getBytesLength("job")!=sizeof(record)||prefs_.getBytes("job",&record,sizeof(record))!=sizeof(record)||record.magic!=0x52553131||record.checksum!=remote_update::crc(&record,offsetof(Record,checksum))){storageReady=false;return;}
      record.state[15]=0;record.error[48]=0;record.id[32]=0;record.kind[5]=0;record.version[23]=0;record.build[20]=0;
    }
  }
  bool enabled() const{return storageReady&&remote_update::hex(key_,64)&&hosts_[0];}
  bool requiresCloudTls() const{return key_[0]!=0;}
  const char* hosts() const{return hosts_;}
  const char* downloadToken() const{return downloadToken_;}
  bool configure(const String& key,const String& hosts,const String& downloadToken="") {
    if(!storageReady||active||!remote_update::hex(key.c_str(),64)||hosts.isEmpty()||hosts.length()>=sizeof(hosts_))return false;
    for(size_t i=0;i<hosts.length();++i){const char c=hosts[i];if(!((c>='a'&&c<='z')||isdigit((unsigned char)c)||c=='.'||c=='-'||c==','))return false;}
    if(downloadToken.length()>=sizeof(downloadToken_))return false;
    for(size_t i=0;i<downloadToken.length();++i)if((unsigned char)downloadToken[i]<33||(unsigned char)downloadToken[i]>126)return false;
    if((downloadToken.length()||downloadToken_[0])&&strchr(hosts.c_str(),','))return false; // Never share a bearer credential across hosts.
    if(prefs_.isKey("key")&&prefs_.getString("key","")!=key)return false; // Rotation needs an explicit coordinated procedure.
    if(prefs_.putString("key",key)!=64||prefs_.putString("hosts",hosts)!=hosts.length())return false;
    if(downloadToken.length()&&prefs_.putString("download",downloadToken)!=downloadToken.length())return false;
    key.toCharArray(key_,sizeof(key_));hosts.toCharArray(hosts_,sizeof(hosts_));prefs_.getString("download","").toCharArray(downloadToken_,sizeof(downloadToken_));return true;
  }
  bool persist(){if(!storageReady)return false;record.checksum=remote_update::crc(&record,offsetof(Record,checksum));return prefs_.putBytes("job",&record,sizeof(record))==sizeof(record);}
  bool state(const char* name,const char* error="",uint8_t percent=0) {
    const bool changed=strcmp(record.state,name)||strcmp(record.error,error);
    snprintf(record.state,sizeof(record.state),"%s",name);snprintf(record.error,sizeof(record.error),"%s",error);record.percent=percent;
    if(changed&&!persist()){snprintf(record.state,sizeof(record.state),"FAILED");snprintf(record.error,sizeof(record.error),"NVS_WRITE");storageReady=false;return false;}
    return true;
  }
  bool take(remote_update::Command& c,uint32_t now) {
    if(!enabled()||active||xQueueReceive(inbox_,&c,0)!=pdTRUE)return false;
    if(!remote_update::validCommand(c,device_,now,record.seq,hosts_))return false;
    // Consume the sequence before any download/flash/BLE side effect. Retained over reboot.
    record.seq=c.seq;record.expires=c.expires;snprintf(record.id,sizeof(record.id),"%s",c.id);
    snprintf(record.kind,sizeof(record.kind),"%s",c.kind);snprintf(record.version,sizeof(record.version),"%s",c.version);snprintf(record.build,sizeof(record.build),"%s",c.build);
    // Always commit the new identity, even if a recovered record had the same state text.
    snprintf(record.state,sizeof(record.state),"RECEIVED");record.error[0]=0;record.percent=0;
    if(!persist()){storageReady=false;return false;}active=true;return true;
  }
  void attach(cJSON* root,char nonce[33],uint64_t uptime,uint32_t now,const char* firmware,uint64_t build,const char* sensorVersion) {
    nonce[0]=0;if(!enabled()||now<1700000000||uptime<nextPoll)return;
    nextPoll=uptime+(active?remote_update::REPORT_MS:remote_update::POLL_MS);
    uint8_t bytes[16];esp_fill_random(bytes,sizeof(bytes));for(int i=0;i<16;++i)snprintf(nonce+i*2,3,"%02x",bytes[i]);
    char buildText[21];snprintf(buildText,sizeof(buildText),"%llu",(unsigned long long)build);
    const char* sensor=remote_update::version(sensorVersion)?sensorVersion:"";
    char canonical[512];const int n=snprintf(canonical,sizeof(canonical),"FSGWREQUEST1\n%s\n%s\n%lu\n%s\n%lu\n%s\n%u\n%s\n%s\n%s\n%s\n",
      device_,nonce,(unsigned long)now,record.id,(unsigned long)record.seq,record.state,record.percent,record.error,firmware,buildText,sensor);
    char mac[65];if(n<=0||n>=(int)sizeof(canonical)||!hmac(canonical,mac)){nonce[0]=0;return;}
    cJSON* r=cJSON_AddObjectToObject(root,"remote");cJSON_AddStringToObject(r,"nonce",nonce);cJSON_AddNumberToObject(r,"at",now);
    cJSON_AddStringToObject(r,"id",record.id);cJSON_AddNumberToObject(r,"seq",record.seq);cJSON_AddStringToObject(r,"state",record.state);
    cJSON_AddNumberToObject(r,"percent",record.percent);cJSON_AddStringToObject(r,"error",record.error);cJSON_AddStringToObject(r,"firmware",firmware);
    cJSON_AddStringToObject(r,"build",buildText);cJSON_AddStringToObject(r,"sensor_version",sensor);cJSON_AddStringToObject(r,"mac",mac);
  }
  void receive(const cJSON* root,const char* nonce) {
    if(!enabled()||!nonce||!nonce[0])return;
    const cJSON* r=cJSON_GetObjectItemCaseSensitive(root,"remote");if(!cJSON_IsObject(r))return;
    remote_update::Command c;
    if(!str(r,"device",c.device)||!str(r,"id",c.id)||!str(r,"nonce",c.nonce)||strcmp(c.nonce,nonce)||
       !str(r,"kind",c.kind)||!str(r,"sensor",c.sensor)||!str(r,"version",c.version)||!str(r,"build",c.build)||
       !str(r,"url",c.url)||!str(r,"sha256",c.sha)||!str(r,"manifest_url",c.manifestUrl)||!str(r,"manifest_sha256",c.manifestSha)||!str(r,"mac",c.mac)||
       !number(r,"seq",c.seq)||!number(r,"issued",c.issued)||!number(r,"expires",c.expires)||!number(r,"bytes",c.bytes)||!number(r,"manifest_bytes",c.manifestBytes))return;
    const cJSON* allow=cJSON_GetObjectItemCaseSensitive(r,"allow_downgrade");if(!cJSON_IsBool(allow))return;c.allowDowngrade=cJSON_IsTrue(allow);
    char canonical[1536],mac[65];const int n=remote_update::canonical(c,canonical,sizeof(canonical));
    if(n<=0||n>=(int)sizeof(canonical)||!hmac(canonical,mac)||!equal(mac,c.mac))return;
    xQueueSend(inbox_,&c,0);
  }
 private:
  Preferences prefs_;QueueHandle_t inbox_=nullptr;char device_[13]={},key_[65]={},hosts_[256]={},downloadToken_[257]={};
  static bool equal(const char* a,const char* b){if(strlen(a)!=strlen(b))return false;uint8_t d=0;for(size_t i=0;a[i];++i)d|=a[i]^b[i];return !d;}
  bool hmac(const char* text,char out[65]) const {
    uint8_t hash[32];if(mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),(const uint8_t*)key_,strlen(key_),(const uint8_t*)text,strlen(text),hash))return false;
    for(int i=0;i<32;++i)snprintf(out+i*2,3,"%02x",hash[i]);return true;
  }
  template<size_t N>static bool str(const cJSON* root,const char* name,char (&out)[N]) {
    const cJSON* v=cJSON_GetObjectItemCaseSensitive(root,name);if(!cJSON_IsString(v)||strlen(v->valuestring)>=N)return false;
    for(const char* p=v->valuestring;*p;++p)if((unsigned char)*p<32||(unsigned char)*p>126)return false;
    snprintf(out,N,"%s",v->valuestring);return true;
  }
  static bool number(const cJSON* root,const char* name,uint32_t& out){const cJSON* v=cJSON_GetObjectItemCaseSensitive(root,name);if(!cJSON_IsNumber(v)||v->valuedouble<0||v->valuedouble>4294967295.0||v->valuedouble!=(uint32_t)v->valuedouble)return false;out=(uint32_t)v->valuedouble;return true;}
};
