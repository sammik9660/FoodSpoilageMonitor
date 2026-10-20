#include <cassert>
#include <cstring>
#include "../firmware/esp32s3/gateway/GatewayUi.h"

using namespace gateway_ui;

static void linesAreFixed(const char* a,const char* b) {
  assert(strlen(a)==LCD_COLUMNS);assert(strlen(b)==LCD_COLUMNS);
}

int main() {
  char a[LCD_COLUMNS+1],b[LCD_COLUMNS+1];

  formatSensor(false,0,0,0,0,a,b);linesAreFixed(a,b);
  assert(strstr(a,"T:--")&&strstr(b,"G:--"));
  formatSensor(true,31.0f,42.2f,1005.6f,123.4f,a,b);linesAreFixed(a,b);
  assert(strstr(a,"T:31.0 H:42.2")==a);assert(strstr(b,"P:1006 G:123k")==b);
  formatSensor(true,-40.0f,100.0f,1100.0f,9999999.0f,a,b);linesAreFixed(a,b);

  formatDebug(true,true,CLOUD_OK,0,0,true,false,a,b);linesAreFixed(a,b);
  assert(strstr(a,"B:OK W:OK C:OK")==a);assert(strstr(b,"Q0 SP0 R0"));
  formatDebug(true,true,CLOUD_WAIT,999999,999999,true,true,a,b);linesAreFixed(a,b);
  assert(strstr(a,"C:WAIT"));assert(strstr(b,"Q999k SP999k R1"));
  formatDebug(false,false,CLOUD_OFF,0xffffffffu,0xffffffffu,false,false,a,b);linesAreFixed(a,b);

  assert(cloudState(false,false,false,false,false)==CLOUD_OFF);
  assert(cloudState(true,false,false,false,true)==CLOUD_OK);
  assert(cloudState(true,true,false,false,true)==CLOUD_WAIT);
  assert(cloudState(true,false,true,false,true)==CLOUD_WAIT);
  assert(cloudState(true,false,false,true,true)==CLOUD_WAIT);
  assert(cloudState(true,false,false,false,false)==CLOUD_WAIT);

  assert(pixelState(false,false,CLOUD_WAIT,true)==PIXEL_BLE_OFF);
  assert(pixelState(false,false,CLOUD_WAIT,true,true)==PIXEL_UPDATE);
  assert(pixelState(true,false,CLOUD_WAIT,true)==PIXEL_WIFI_OFF);
  assert(pixelState(true,true,CLOUD_WAIT,false)==PIXEL_WAIT);
  assert(pixelState(true,true,CLOUD_OK,true)==PIXEL_WAIT);
  assert(pixelState(true,true,CLOUD_OK,false)==PIXEL_OK);
  assert(!uploadDataPending(0,0,0,0));
  assert(uploadDataPending(1,0,0,0));
  assert(uploadDataPending(0,1,0,0));
  assert(uploadDataPending(0,0,1,0));
  assert(uploadDataPending(0,0,0,1));
  assert(!shouldStartDataUpload(false,false,false,5000,5000,0,0,0,0));
  assert(shouldStartDataUpload(false,false,false,5000,5000,0,0,0,1));
  assert(!shouldStartDataUpload(true,false,false,5000,5000,0,0,0,1));
  assert(!shouldStartDataUpload(false,true,false,5000,5000,0,0,0,1));
  assert(!shouldStartDataUpload(false,false,true,5000,5000,0,0,0,1));
  assert(!shouldStartDataUpload(false,false,false,4999,5000,0,0,0,1));
  assert(shouldStartHealthHeartbeat(false,false,false,true,5000,5000,0,0,0,0));
  assert(!shouldStartHealthHeartbeat(false,false,false,true,4999,5000,0,0,0,0));
  assert(!shouldStartHealthHeartbeat(true,false,false,true,5000,5000,0,0,0,0));
  assert(!shouldStartHealthHeartbeat(false,true,false,true,5000,5000,0,0,0,0));
  assert(!shouldStartHealthHeartbeat(false,false,true,true,5000,5000,0,0,0,0));
  assert(!shouldStartHealthHeartbeat(false,false,false,false,5000,5000,0,0,0,0));
  assert(!shouldStartHealthHeartbeat(false,false,false,true,5000,5000,0,0,0,1));
  assert(otaRainbowVisible(1000,1000));
  assert(otaRainbowVisible(1000,3999));
  assert(!otaRainbowVisible(1000,4000));
  assert(!otaRainbowVisible(1000,999));
  assert(otaRainbowColor(0)==0xff0000);
  assert(otaRainbowColor(85)==0x00ff00);
  assert(otaRainbowColor(170)==0x0000ff);

  ActiveLowButton shortButton;
  assert(!shortButton.update(true,0).shortPress);
  shortButton.update(false,10);assert(!shortButton.update(false,39).shortPress);
  assert(!shortButton.update(false,40).shortPress);
  shortButton.update(true,100);ButtonEvents shortRelease=shortButton.update(true,130);
  assert(shortRelease.shortPress&&!shortRelease.longPress);
  assert(!shortButton.update(true,200).shortPress);

  ActiveLowButton longButton;
  longButton.update(false,0);longButton.update(false,BUTTON_DEBOUNCE_MS);
  ButtonEvents held=longButton.update(false,BUTTON_DEBOUNCE_MS+BUTTON_LONG_PRESS_MS);
  assert(held.longPress&&!held.shortPress);assert(!longButton.update(false,4000).longPress);
  longButton.update(true,4100);ButtonEvents longRelease=longButton.update(true,4130);
  assert(!longRelease.shortPress&&!longRelease.longPress);

  puts("PASS gateway UI: formatting, pixel priority, data/health scheduling, update blocking, rainbow");
}
