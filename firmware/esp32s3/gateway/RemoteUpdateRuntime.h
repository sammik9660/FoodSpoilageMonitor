#pragma once
// Included after the existing gateway engines; main-loop-only orchestration.
namespace remote_runtime {
enum Stage { NONE, PREPARE, MANIFEST, DOWNLOAD, WAIT_SENSOR, DFU, BOOT_VERIFY, FINISH };
Stage stage=NONE;
remote_update::Command command;
uint32_t offset=0;
uint64_t startedAt=0,sensorWaitAt=0;
File zip;
bool ownsTemp=false,hashStarted=false;
mbedtls_sha256_context hash;
uint8_t buffer[4096];
char manifest[512]={};
const char* downloadError="DOWNLOAD_FAILED";
}

void configureCloudTls(GatewaySecureClient& client) {
  if(remoteClient.requiresCloudTls())client.useBuiltinCACertBundle();
  else client.setInsecure(); // Existing local-only configuration; never used by remote downloads.
}
bool remoteGoogleRedirect(const String& url) {
  return url.startsWith("https://script.googleusercontent.com/")||url.startsWith("https://script.google.com/");
}
void remoteAttach(cJSON* root,char nonce[33]) {
  char version[32];portENTER_CRITICAL(&stateMux);memcpy(version,connectedSensorFirmware,sizeof(version));portEXIT_CRITICAL(&stateMux);
  remoteClient.attach(root,nonce,uptimeMs(),clockValid()?(uint32_t)(utcMs()/1000):0,FIRMWARE_VERSION,OTA_BUILD_NUMBER,version);
}
void handleRemoteConfig() {
  if(!otaServer.authenticate("admin",otaPassword))return otaServer.requestAuthentication();
  otaServer.sendHeader("Cache-Control","no-store");
  otaServer.send(200,"text/html; charset=utf-8",String("<!doctype html><meta charset=utf-8><h1>Remote update provisioning</h1><p>Device ")+deviceId+
    "</p><p>Remote control is "+(remoteClient.enabled()?"configured":"not configured")+
    ". Provision the same manually generated per-device key in Apps Script. No key is displayed or generated here.</p>"
    "<form method=post autocomplete=off><label>64-hex device key <input name=key type=password minlength=64 maxlength=64 required autocomplete=new-password></label><br>"
    "<label>Allowed HTTPS hostnames (comma-separated, no spaces) <input name=hosts maxlength=255 required></label><br>"
    "<label>Optional download bearer token (blank keeps current value) <input name=download type=password maxlength=256 autocomplete=new-password></label><br>"
    "<button>Save device configuration</button></form><p>Use a trusted LAN for this one-time local setup. Key replacement is intentionally rejected; coordinated rotation is required.</p>");
}
void handleRemoteConfigSave() {
  if(!otaServer.authenticate("admin",otaPassword))return otaServer.requestAuthentication();
  if(!otaSameOrigin())return otaServer.send(400,"text/plain","Origin rejected");
  if(remoteOwnsUpdate||otaMode!=OTA_NORMAL||nrfDfuMode!=NRF_DFU_NORMAL||otaHttpBusy||!cloudRecordingKnown||cloudRecording)
    return otaServer.send(409,"text/plain","Stop recording and finish updates before provisioning");
  const bool ok=remoteClient.configure(otaServer.arg("key"),otaServer.arg("hosts"),otaServer.arg("download"));
  otaServer.sendHeader("Cache-Control","no-store");
  otaServer.send(ok?200:400,"text/plain",ok?"Saved in NVS; configure the matching device key/catalog in Apps Script. No firmware update was started.":"Configuration rejected or NVS write failed; existing key is never silently replaced.");
}
void beginRemoteUpdate() {
  remoteClient.begin(deviceId);
  if(!remoteClient.enabled())return;
  if(!remote_update::terminal(remoteClient.record.state)) {
    if(!strcmp(remoteClient.record.kind,"esp32")&&!strcmp(remoteClient.record.state,"VERIFYING")&&
       !strcmp(remoteClient.record.version,FIRMWARE_VERSION)&&strtoull(remoteClient.record.build,nullptr,10)==OTA_BUILD_NUMBER) {
      remote_runtime::stage=remote_runtime::BOOT_VERIFY;remote_runtime::startedAt=uptimeMs();remoteOwnsUpdate=true;remoteClient.active=true;
    }else remoteClient.state("FAILED","BOOT_INTERRUPTED");
  }
}
void remoteFailure(const char* code) {
  using namespace remote_runtime;
  if(zip)zip.close();if(hashStarted){mbedtls_sha256_free(&hash);hashStarted=false;}
  if(ownsTemp){SPIFFS.remove(nrf_dfu_package::TEMP_PATH);ownsTemp=false;}
  if(remoteOwnsUpdate&&otaMode!=OTA_NORMAL)abortOtaSession();
  if(remoteOwnsUpdate&&nrfDfuMode==NRF_DFU_ACTIVE)nrfDfuCancelRequested=true;
  if(remoteOwnsUpdate&&nrfDfuMode==NRF_DFU_PREPARE){char error[96];requestNrfDfuCancel(error,sizeof(error));}
  remoteClient.state("FAILED",code,remoteClient.record.percent);stage=FINISH;
  logEvent("Remote update failed code=%s; command will not replay",code);
}
// A bounded range per invocation keeps HTTP clients sequential with cloud reports.
bool remoteReadRange(const char* url,uint32_t total,uint32_t start,uint32_t amount,bool manifestOnly) {
  using namespace remote_runtime;
  downloadError="URL_OR_CLOCK";
  if(!clockValid()||!remote_update::allowedUrl(url,remoteClient.hosts()))return false;
  if(remote_update::githubReleaseUrl(url)&&remoteClient.downloadToken()[0]){downloadError="DOWNLOAD_AUTH";return false;}
  String currentUrl(url);
  char range[64];snprintf(range,sizeof(range),"bytes=%lu-%lu",(unsigned long)start,(unsigned long)(start+amount-1));
  for(int hop=0;hop<2;++hop) {
    GatewaySecureClient client;client.useBuiltinCACertBundle();client.setHandshakeTimeout(15);
    HTTPClient http;http.setReuse(false);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);http.setConnectTimeout(10000);http.setTimeout(10000);
    const char* keys[]={"Content-Range","Content-Encoding"};http.collectHeaders(keys,2);
    downloadError="HTTPS_BEGIN";if(!http.begin(client,currentUrl.c_str()))return false;
    http.addHeader("Range",range);http.addHeader("Accept-Encoding","identity");
    if(remoteClient.downloadToken()[0])http.addHeader("Authorization",String("Bearer ")+remoteClient.downloadToken());
    const int code=http.GET();
    if(code==302) {
      const String location=http.getLocation();
      const bool allowed=hop==0&&remote_update::githubReleaseUrl(url)&&remote_update::githubAssetRedirect(location.c_str());
      http.end();client.stop();
      if(!allowed){downloadError="DOWNLOAD_REDIRECT_REJECTED";return false;}
      currentUrl=location;continue;
    }
  const bool headerOK=http.getSize()==(int)amount&&(!http.header("Content-Encoding").length()||http.header("Content-Encoding")=="identity")&&
    ((code==206&&remote_update::rangeMatches(http.header("Content-Range").c_str(),start,start+amount-1,total))||(code==200&&start==0&&amount==total));
  if(!headerOK){
    downloadError=code==401||code==403?"DOWNLOAD_AUTH":code==404?"DOWNLOAD_NOT_FOUND":code>=300&&code<400?"DOWNLOAD_REDIRECT_REJECTED":code<0?"DOWNLOAD_TLS_OR_CONNECT":"HTTP_RANGE_OR_SIZE";
    http.end();client.stop();return false;
  }
  auto* stream=http.getStreamPtr();uint32_t done=0;uint64_t last=uptimeMs(),begin=last;bool ok=true;
  while(done<amount) {
    const uint64_t now=uptimeMs();
    if(WiFi.status()!=WL_CONNECTED){downloadError="DOWNLOAD_OFFLINE";ok=false;break;}
    if(now-last>10000||now-begin>60000||now-startedAt>remote_update::RUN_SECONDS*1000ULL){downloadError="DOWNLOAD_TIMEOUT";ok=false;break;}
    const int available=stream->available();if(available<=0){if(!http.connected()){downloadError="DOWNLOAD_INTERRUPTED";ok=false;break;}delay(1);continue;}
    const size_t want=min((size_t)(amount-done),min(sizeof(buffer),(size_t)available));
    const int n=stream->readBytes(buffer,want);if(n<=0){downloadError="DOWNLOAD_INTERRUPTED";ok=false;break;}
    if(manifestOnly)memcpy(manifest+start+done,buffer,n);
    else if(!strcmp(command.kind,"esp32")){if(!writeOtaImage(buffer,n)){downloadError="OTA_WRITE";ok=false;break;}}
    else if(!zip||zip.write(buffer,n)!=(size_t)n){downloadError="PACKAGE_WRITE";ok=false;break;}
    if(!manifestOnly&&mbedtls_sha256_update(&hash,buffer,n)){downloadError="HASH_UPDATE";ok=false;break;}
    done+=n;last=uptimeMs();otaDeadlineAt=last+OTA_ACTIVE_TIMEOUT_MS;
    delay(1);
  }
  http.end();client.stop();return ok&&done==amount;
  }
  downloadError="DOWNLOAD_REDIRECT_REJECTED";return false;
}
bool remoteDigestMatches(const uint8_t* digest,const char* expected) {
  char text[65];for(int i=0;i<32;++i)snprintf(text+i*2,3,"%02x",digest[i]);return !strcmp(text,expected);
}
void serviceRemoteUpdate() {
  using namespace remote_runtime;
  if(!remoteClient.enabled()){
    if(stage!=NONE&&stage!=FINISH)remoteFailure("NVS_WRITE");
    if(stage!=FINISH)return;
  }
  const uint64_t now=uptimeMs();
  if(stage==NONE) {
    if(!clockValid()||!remoteClient.take(command,(uint32_t)(utcMs()/1000)))return;
    startedAt=now;offset=0;
    if(!otaSafeToStart()){remoteFailure("LOCAL_UPDATE_BUSY");return;}
    if(!strcmp(command.kind,"nrf")&&strcmp(targetSensorId,command.sensor)){remoteFailure("TARGET_NOT_PAIRED");return;}
    remoteOwnsUpdate=true;otaRollbackApproved=command.allowDowngrade;otaUploadFailure=nullptr;
    xSemaphoreTake(otaIngressMutex,portMAX_DELAY);otaMode=OTA_PREPARE;otaDeadlineAt=now+OTA_PREPARE_TIMEOUT_MS;otaGapSamples=0;xSemaphoreGive(otaIngressMutex);
    if(!remoteClient.state("PREPARING")){remoteFailure("NVS_WRITE");return;}stage=PREPARE;return;
  }
  if(stage==FINISH) {
    if(remoteOwnsUpdate&&nrfDfuMode!=NRF_DFU_NORMAL)return;
    if(remoteOwnsUpdate){otaLeaseReleasePending=true;otaLeaseReleaseDeferredOnce=true;}
    remoteClient.active=false;remoteOwnsUpdate=false;stage=NONE;return;
  }
  if(now-startedAt>remote_update::RUN_SECONDS*1000ULL){remoteFailure("UPDATE_TIMEOUT");return;}
  if(stage==BOOT_VERIFY) {
    if(!otaPendingVerify&&spool.mounted&&bleConnected&&firstFrameAt&&firstFrameAt>=otaBootAt&&WiFi.status()==WL_CONNECTED&&otaLastCloudAckAt>otaBootAt) {
      if(!remoteClient.state("SUCCESS","",100)){remoteFailure("NVS_WRITE");return;}stage=FINISH;
    }
    return; // Normal sampling/cloud ACKs perform the existing boot self-check.
  }
  if(stage==DFU) {
    if(nrfDfuMode==NRF_DFU_ACTIVE)remoteClient.state("UPDATING","",nrfDfuBytesTotal?(uint8_t)(nrfDfuBytesSent*100ULL/nrfDfuBytesTotal):0);
    else if(nrfDfuMode==NRF_DFU_RECONNECT)remoteClient.state("RECONNECTING");
    else if(nrfDfuMode==NRF_DFU_NORMAL&&nrfDfuHasResult) {
      if(!nrfDfuLastSuccess){remoteFailure("DFU_FAILED");return;}
      bool data=false;char version[32],sensor[17];portENTER_CRITICAL(&stateMux);
      data=(haveLatest&&latest.receivedUptime>nrfDfuTransferStartedAt)||(environmentState.known&&environmentState.receivedAt>nrfDfuTransferStartedAt);
      memcpy(version,connectedSensorFirmware,sizeof(version));memcpy(sensor,connectedSensorId,sizeof(sensor));portEXIT_CRITICAL(&stateMux);
      if(strcmp(version,command.version)||strcmp(sensor,command.sensor)){remoteFailure("DFU_VERSION_MISMATCH");return;}
      if(data){remoteClient.state("SUCCESS","",100);stage=FINISH;}else remoteClient.state("RECONNECTING");
    }
  }
  if(stage==PREPARE&&otaMode!=OTA_PREPARE){remoteFailure("PREPARATION_STOPPED");return;}
  if(otaHttpBusy||uxQueueMessagesWaiting(resultsQueue))return;
  if(now>=remoteClient.nextPoll&&!otaPendingSamples&&!otaHttpFailed) {
    // Uses the established POST -> redirect GET path. No TLS overlap with downloads.
    const bool ok=syncOtaLease("UPDATING");
    if(!ok&&(stage==MANIFEST||stage==DOWNLOAD||stage==WAIT_SENSOR)){remoteFailure("CLOUD_STOPPED_UNCONFIRMED");return;}
  }
  if(stage==PREPARE) {
    if(otaMode!=OTA_PREPARE){remoteFailure("PREPARATION_STOPPED");return;}
    if(!otaLeaseHeld||!otaSafeState())return;
    if(utcMs()/1000>command.expires){remoteFailure("COMMAND_EXPIRED");return;}
    if(!otaActivateIfDrained()){remoteFailure("PREPARATION_RACE");return;}
    cloudRecordingKnown=true;cloudRecording=false;
    if(!remoteClient.state("DOWNLOADING")){remoteFailure("NVS_WRITE");return;}
    if(!strcmp(command.kind,"esp32"))stage=MANIFEST;
    else {
      if(SPIFFS.exists(nrf_dfu_package::TEMP_PATH)){remoteFailure("TEMP_PACKAGE_EXISTS");return;}
      const size_t total=SPIFFS.totalBytes(),used=SPIFFS.usedBytes();
      size_t oldZipBytes=0;
      if(SPIFFS.exists(nrf_dfu_package::FINAL_PATH)){
        File oldZip=SPIFFS.open(nrf_dfu_package::FINAL_PATH,FILE_READ);
        if(!oldZip){remoteFailure("PACKAGE_OPEN");return;}
        oldZipBytes=oldZip.size();oldZip.close();
      }
      const size_t normal=nrf_dfu_package::temporaryCapacity(total,used,spool.budget);
      const size_t paused=nrf_dfu_package::pausedIngressCapacity(total,used,oldZipBytes,spool.budget);
      logEvent("nRF DFU space total=%u used=%u old_zip=%u spool_reserve=%u normal=%u paused=%u requested=%lu",
        (unsigned)total,(unsigned)used,(unsigned)oldZipBytes,(unsigned)spool.budget,(unsigned)normal,(unsigned)paused,(unsigned long)command.bytes);
      if(!otaSafeState()||command.bytes>paused){remoteFailure("SPOOL_RESERVE_SPACE");return;}
      zip=SPIFFS.open(nrf_dfu_package::TEMP_PATH,FILE_WRITE);if(!zip){remoteFailure("PACKAGE_OPEN");return;}ownsTemp=true;stage=DOWNLOAD;
      mbedtls_sha256_init(&hash);hashStarted=true;if(mbedtls_sha256_starts(&hash,0)){remoteFailure("HASH_INIT");return;}
    }
    return;
  }
  if(stage==MANIFEST) {
    if(otaMode!=OTA_ACTIVE||!otaSafeState()){remoteFailure("LOCAL_SAFETY_CHANGED");return;}
    if(!remoteReadRange(command.manifestUrl,command.manifestBytes,0,command.manifestBytes,true)){remoteFailure(downloadError);return;}
    uint8_t digest[32];
    if(mbedtls_sha256((const uint8_t*)manifest,command.manifestBytes,digest,0)||!remoteDigestMatches(digest,command.manifestSha)){remoteFailure("MANIFEST_HASH");return;}
    manifest[command.manifestBytes]=0;otaManifestLength=command.manifestBytes;
    if(!verifyOtaManifest(manifest)||strcmp(otaIncomingVersion,command.version)||otaIncomingBuild!=strtoull(command.build,nullptr,10)||strcmp(otaIncomingDigest,command.sha)){
      remoteFailure("SIGNED_MANIFEST_REJECTED");return;
    }
    otaManifestReady=true;otaUploadActive=true;
    if(utcMs()/1000>command.expires||!beginOtaImage()){remoteFailure("IMAGE_BEGIN_OR_EXPIRED");return;}
    mbedtls_sha256_init(&hash);hashStarted=true;if(mbedtls_sha256_starts(&hash,0)){remoteFailure("HASH_INIT");return;}
    stage=DOWNLOAD;return;
  }
  if(stage==DOWNLOAD) {
    if(otaMode!=OTA_ACTIVE||!otaSafeState()){remoteFailure("LOCAL_SAFETY_CHANGED");return;}
    const uint32_t amount=min(remote_update::CHUNK_BYTES,command.bytes-offset);
    if(!remoteReadRange(command.url,command.bytes,offset,amount,false)){remoteFailure(downloadError);return;}
    offset+=amount;remoteClient.state("DOWNLOADING","",(uint8_t)(offset*100ULL/command.bytes));
    if(offset<command.bytes)return;
    uint8_t digest[32];const int hashed=mbedtls_sha256_finish(&hash,digest);mbedtls_sha256_free(&hash);hashStarted=false;
    if(hashed||!remoteDigestMatches(digest,command.sha)){remoteFailure("IMAGE_HASH");return;}
    if(!strcmp(command.kind,"esp32")) {
      if(otaReceivedBytes!=command.bytes||!sealOtaImage()){remoteFailure("IMAGE_BYTE_COUNT");return;}
      if(!remoteClient.state("VERIFYING","",100)){remoteFailure("NVS_WRITE");return;}
      if(!syncOtaLease("UPDATING")||!otaSafeState()||!commitOtaImage()){remoteFailure("IMAGE_COMMIT");return;}
      delay(100);ESP.restart();return;
    }
    zip.flush();zip.close();NrfLegacyDfuRelay::PackageInfo info;char error[128];
    if(!validateNrfPackageAt(nrf_dfu_package::TEMP_PATH,info,error,sizeof(error))||!promoteNrfPackage(error,sizeof(error))){remoteFailure("DFU_PACKAGE_INVALID");return;}
    ownsTemp=false;abortOtaSession();stage=WAIT_SENSOR;sensorWaitAt=now;remoteClient.state("DOWNLOADING","WAIT_SENSOR",100);return;
  }
  if(stage==WAIT_SENSOR) {
    if(now-sensorWaitAt>180000){remoteFailure("PAIRED_SENSOR_UNAVAILABLE");return;}
    if(strcmp(targetSensorId,command.sensor)){remoteFailure("PAIRING_CHANGED");return;}
    if(!bleConnected||strcmp(connectedSensorId,command.sensor))return;
    if(otaLeaseReleasePending)return;
    if(!remoteClient.state("UPDATING")){remoteFailure("NVS_WRITE");return;}
    char error[128];if(!syncOtaLease("UPDATING")||!requestNrfDfuStart(error,sizeof(error),true)){remoteFailure("DFU_START_BLOCKED");return;}
    stage=DFU;
  }
}
