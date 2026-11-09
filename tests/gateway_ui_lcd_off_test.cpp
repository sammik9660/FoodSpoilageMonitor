#include <cassert>
#include "../firmware/esp32s3/gateway/GatewayUi.h"
#if ENABLE_I2C_LCD
#error LCD must be disabled by default
#endif

using namespace gateway_ui;
int main() {
  assert(pixelState(true,true,CLOUD_OK,false)==PIXEL_OK);
  assert(wifiFeedbackColor(WIFI_FEEDBACK_PORTAL,0)==0xa000ff);
  ActiveLowButton boot;
  boot.update(false,0);boot.update(false,BUTTON_DEBOUNCE_MS);
  boot.update(true,BUTTON_LONG_PRESS_MS);
  assert(boot.update(true,BUTTON_LONG_PRESS_MS+BUTTON_DEBOUNCE_MS).longPress);
}
