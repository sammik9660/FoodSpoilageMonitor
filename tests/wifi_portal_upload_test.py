"""Check the cross-task pause boundaries in the real gateway sketch.

The existing scheduler/spool tests cover their individual policies; these
assertions protect the portal-to-scheduler connections that can be lost when
the portal and uploader are edited separately. No hardware is simulated.
"""
from pathlib import Path

source = (Path(__file__).parents[1] / "firmware/esp32s3/gateway/gateway.ino").read_text(encoding="utf-8")
loop = source[source.index("void loop() {") :]
wifi = source[source.index("void serviceWifi(uint64_t now)") : source.index("void initGatewayPixel()")]
portal = source[source.index("void requestWifiPortal()") : source.index("void serviceWifi(uint64_t now)")]

assert "if(wifiPortalPending||wifiManager.getConfigPortalActive())return" in wifi
assert "wifiPortalPending=true" in portal
assert "if(wifiPortalPending&&!busy&&!uxQueueMessagesWaiting(jobsQueue)&&!uxQueueMessagesWaiting(resultsQueue))" in loop
assert loop.index("xQueueReceive(resultsQueue,&result,0)") < loop.index("startWifiPortal();") < loop.index("bool outage=")
assert "const bool portalUploadPaused=wifiPortalPending||wifiManager.getConfigPortalActive()" in loop
assert "bool outage=portalUploadPaused||failed||spool.count||WiFi.status()!=WL_CONNECTED" in loop
assert "if(!portalUploadPaused&&shouldStartDataUpload(" in loop
assert "if(!portalUploadPaused&&shouldStartHealthHeartbeat(" in loop
assert "portalUploadPaused?false:serviceDeferredOtaLeaseRelease()" in loop
assert "if(!portalUploadPaused)serviceRemoteUpdate()" in loop
assert "if(!wifiPortalPending&&!wifiManager.getConfigPortalActive()&&!remoteOwnsUpdate" in source or (
    "return !wifiPortalPending&&!wifiManager.getConfigPortalActive()&&!remoteOwnsUpdate" in source
)
assert "!remoteOwnsUpdate||!strcmp(remoteClient.record.state,\"VERIFYING\")" in source
assert "spool.save(staging,stagedCount,path)" in loop
assert "spool.acknowledge(activePath,activeCount)" in loop
setup = source[source.index("wifiManager.setConfigPortalBlocking(false)") : source.index("loadWifiNetworks();WiFi.persistent(false)")]
assert 'const char* portalMenu[]={"wifinoscan","info","exit"}' in setup
routes = source[source.index("void handleWifiPortalSave()") : source.index("bool wifiPortalAllowed()")]
assert 'wifiManager.server->arg("s")' in routes and 'wifiManager.server->arg("p")' in routes
assert 'saveWifiNetwork(ssid,password)' in routes
assert 'if(ssid==wifiNetworks[i].ssid)valid=false' in routes
assert 'WIFI_PORTAL credentials stored in NVS slot=' in routes
assert 'wifiManager.server->on("/wifisave",HTTP_POST,handleWifiPortalSave)' in routes
assert 'wifiManager.server->on("/wifi",HTTP_POST,handleWifiPortalScan)' in routes
assert 'WiFi.scanNetworks(true)' in routes and 'if(!(WiFi.getMode()&WIFI_STA))' in routes
assert 'wifiPortalExitAt=uptimeMs()+3000' in routes
assert 'wifiPortalCandidateSlot>=0&&WiFi.status()!=WL_CONNECTED' in loop
portal_close = loop[loop.index('if(wifiPortalStarted&&!wifiManager.getConfigPortalActive())') : loop.index('int wifiState=WiFi.status()')]
assert 'STA_CANCEL_PORTAL_EXIT' in portal_close
assert 'WiFi.disconnect(false,false);wifiDisconnectPending=true;wifiDisconnectAt=now' in portal_close
assert 'wifiPortalScanActive&&WiFi.scanComplete()!=WIFI_SCAN_RUNNING' in loop
failed_attempt = wifi[wifi.index('} else if(disconnectEvent&&wifiTrying)') : wifi.index('if(wifiTrying){')]
assert failed_attempt.index('WiFi.disconnect(false,false)') < failed_attempt.index('return;')
assert 'wifiDisconnectPending=true;wifiDisconnectAt=now' in failed_attempt
assert wifi.index('if(wifiDisconnectPending)') < wifi.index('WiFi.begin(wifiNetworks[wifiNextSlot]')
print("PASS portal waits for HTTP completion, pauses all new HTTPS paths, preserves outage spool and resumes existing ACK flow")
print("PASS scan-free Configure, async optional scan, explicit NVS save, existing slot protection, and STA cancel before failover")
