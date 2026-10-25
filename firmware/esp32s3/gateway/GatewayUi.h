#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace gateway_ui {

constexpr size_t LCD_COLUMNS = 16;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t BUTTON_LONG_PRESS_MS = 3000;
constexpr uint32_t BUTTON_RESET_PRESS_MS = 10000;

inline int wifiRegistrationSlot(int existingSlot, uint8_t count, uint8_t capacity) {
  return existingSlot >= 0 ? existingSlot : count < capacity ? count : -1;
}

enum LcdMode : uint8_t { LCD_SENSOR, LCD_DEBUG };
enum CloudState : uint8_t { CLOUD_OFF, CLOUD_WAIT, CLOUD_OK };
enum PixelState : uint8_t { PIXEL_BOOT, PIXEL_UPDATE, PIXEL_BLE_OFF, PIXEL_WIFI_OFF, PIXEL_WAIT, PIXEL_OK };

constexpr uint32_t OTA_RAINBOW_MS = 3000;
constexpr uint32_t OTA_RAINBOW_FRAME_MS = 30;

inline bool otaRainbowVisible(uint64_t startedAt, uint64_t now) {
  return now >= startedAt && now - startedAt < OTA_RAINBOW_MS;
}

inline uint32_t otaRainbowColor(uint8_t phase) {
  if (phase < 85) return ((uint32_t)(255 - phase * 3) << 16) | ((uint32_t)(phase * 3) << 8);
  if (phase < 170) { phase -= 85; return ((uint32_t)(255 - phase * 3) << 8) | phase * 3; }
  phase -= 170;
  return ((uint32_t)(phase * 3) << 16) | (255 - phase * 3);
}

inline void fixedLine(char out[LCD_COLUMNS + 1], const char* text) {
  size_t n = strlen(text);
  if (n > LCD_COLUMNS) n = LCD_COLUMNS;
  memcpy(out, text, n);
  memset(out + n, ' ', LCD_COLUMNS - n);
  out[LCD_COLUMNS] = 0;
}

inline void compactCount(uint32_t value, char out[5]) {
  if (value < 1000) snprintf(out, 5, "%lu", (unsigned long)value);
  else if (value < 1000000) snprintf(out, 5, "%luk", (unsigned long)(value / 1000));
  else if (value < 1000000000) snprintf(out, 5, "%luM", (unsigned long)(value / 1000000));
  else snprintf(out, 5, "%luG", (unsigned long)(value / 1000000000));
}

inline void compactGas(float gasKohm, char out[6]) {
  if (!isfinite(gasKohm) || gasKohm < 0) snprintf(out, 6, "--");
  else if (gasKohm < 1000) snprintf(out, 6, "%.0fk", gasKohm);
  else if (gasKohm < 1000000) snprintf(out, 6, "%.0fM", gasKohm / 1000.0f);
  else snprintf(out, 6, "999M");
}

inline void formatSensor(bool haveSample, float t, float h, float p, float g,
                         char line1[LCD_COLUMNS + 1], char line2[LCD_COLUMNS + 1]) {
  char temp[40];
  if (!haveSample || !isfinite(t) || !isfinite(h) || !isfinite(p) || !isfinite(g)) {
    fixedLine(line1, "T:-- H:--");
    fixedLine(line2, "P:---- G:--");
    return;
  }
  snprintf(temp, sizeof(temp), "T:%.1f H:%.1f", t, h);
  fixedLine(line1, temp);
  char gas[6]; compactGas(g, gas);
  snprintf(temp, sizeof(temp), "P:%.0f G:%s", p, gas);
  fixedLine(line2, temp);
}

inline const char* cloudText(CloudState state) {
  return state == CLOUD_OK ? "OK" : state == CLOUD_WAIT ? "WAIT" : "--";
}

inline CloudState cloudState(bool wifiOnline, bool dataHttpBusy,
                             bool dataUploadFailed, bool heartbeatFailed,
                             bool haveCloudAck) {
  if (!wifiOnline) return CLOUD_OFF;
  return dataHttpBusy || dataUploadFailed || heartbeatFailed || !haveCloudAck
           ? CLOUD_WAIT : CLOUD_OK;
}

inline void formatDebug(bool ble, bool wifi, CloudState cloud, uint32_t queue,
                        uint32_t spool, bool recordingKnown, bool recording,
                        char line1[LCD_COLUMNS + 1], char line2[LCD_COLUMNS + 1]) {
  char temp[40], q[5], sp[5];
  snprintf(temp, sizeof(temp), "B:%s W:%s C:%s", ble ? "OK" : "--",
           wifi ? "OK" : "--", cloudText(cloud));
  fixedLine(line1, temp);
  compactCount(queue, q); compactCount(spool, sp);
  snprintf(temp, sizeof(temp), "Q%s SP%s R%c", q, sp,
           recordingKnown ? (recording ? '1' : '0') : '-');
  fixedLine(line2, temp);
}

inline PixelState pixelState(bool ble, bool wifi, CloudState cloud, bool backlog,
                             bool updateActive=false) {
  if (updateActive) return PIXEL_UPDATE;
  if (!ble) return PIXEL_BLE_OFF;
  if (!wifi) return PIXEL_WIFI_OFF;
  if (cloud != CLOUD_OK || backlog) return PIXEL_WAIT;
  return PIXEL_OK;
}

inline bool uploadDataPending(uint32_t active, uint32_t staged,
                              uint32_t spool, uint32_t queued) {
  return active || staged || spool || queued;
}

inline bool shouldStartDataUpload(bool busy, bool otaActive,
                                  bool otaMaintenanceNetworkBusy, uint64_t now,
                                  uint64_t due, uint32_t active,
                                  uint32_t staged, uint32_t spool,
                                  uint32_t queued) {
  return !busy && !otaActive && !otaMaintenanceNetworkBusy && now >= due &&
         uploadDataPending(active, staged, spool, queued);
}

inline bool shouldStartHealthHeartbeat(bool busy, bool updateActive,
                                       bool otaMaintenanceNetworkBusy,
                                       bool wifiOnline, uint64_t now,
                                       uint64_t due, uint32_t active,
                                       uint32_t staged, uint32_t spool,
                                       uint32_t queued) {
  return wifiOnline && !busy && !updateActive &&
         !otaMaintenanceNetworkBusy && now >= due &&
         !uploadDataPending(active, staged, spool, queued);
}

struct ButtonEvents { bool shortPress=false, longPress=false, resetPress=false; };

class ActiveLowButton {
 public:
  ButtonEvents update(bool pinHigh, uint64_t now) {
    ButtonEvents event;
    if (pinHigh != rawHigh_) { rawHigh_ = pinHigh; changedAt_ = now; }
    if (rawHigh_ != stableHigh_ && now - changedAt_ >= BUTTON_DEBOUNCE_MS) {
      stableHigh_ = rawHigh_;
      if (!stableHigh_) pressedAt_ = now;
      else {
        const uint64_t heldMs = now - pressedAt_;
        if (heldMs >= BUTTON_RESET_PRESS_MS) event.resetPress = true;
        else if (heldMs >= BUTTON_LONG_PRESS_MS) event.longPress = true;
        else event.shortPress = true;
      }
    }
    return event;
  }

 private:
  bool rawHigh_=true, stableHigh_=true;
  uint64_t changedAt_=0, pressedAt_=0;
};

}  // namespace gateway_ui
