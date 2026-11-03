#include <WiFi.h>
#include <esp_arduino_version.h>
#include <esp_system.h>

// Independent STA diagnostic. Credentials live only in RAM; no AP, cloud,
// BLE, SPIFFS, NVS writes, or firmware update is performed by this sketch.
constexpr uint32_t CONNECT_OBSERVE_MS = 20000;
constexpr uint8_t EVENT_CAPACITY = 16;

struct WifiEvent {
  WiFiEvent_t type;
  uint32_t atMs;
  uint16_t reason;
};
WifiEvent events[EVENT_CAPACITY];
portMUX_TYPE eventMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint8_t eventHead = 0, eventTail = 0;
volatile uint32_t eventDrops = 0;
volatile int lastDisconnectReason = -1;

struct Candidate {
  bool found = false;
  int32_t rssi = -128;
  int32_t channel = 0;
  uint8_t bssid[6] = {};
};

const char* authName(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "OTHER";
  }
}

void onWifiEvent(WiFiEvent_t type, WiFiEventInfo_t info) {
  if (type != ARDUINO_EVENT_WIFI_STA_CONNECTED &&
      type != ARDUINO_EVENT_WIFI_STA_GOT_IP &&
      type != ARDUINO_EVENT_WIFI_STA_DISCONNECTED &&
      type != ARDUINO_EVENT_WIFI_STA_LOST_IP) return;
  WifiEvent item = {type, millis(), 0};
  if (type == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
    item.reason = info.wifi_sta_disconnected.reason;
  portENTER_CRITICAL(&eventMux);
  const uint8_t next = (eventHead + 1) % EVENT_CAPACITY;
  if (next == eventTail) ++eventDrops;
  else { events[eventHead] = item; eventHead = next; }
  if (type == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
    lastDisconnectReason = item.reason;
  portEXIT_CRITICAL(&eventMux);
}

void drainEvents() {
  while (true) {
    WifiEvent item;
    portENTER_CRITICAL(&eventMux);
    const bool available = eventTail != eventHead;
    if (available) { item = events[eventTail]; eventTail = (eventTail + 1) % EVENT_CAPACITY; }
    portEXIT_CRITICAL(&eventMux);
    if (!available) break;
    if (item.type == ARDUINO_EVENT_WIFI_STA_CONNECTED)
      Serial.printf("EVENT t=%lu STA_CONNECTED\n", (unsigned long)item.atMs);
    else if (item.type == ARDUINO_EVENT_WIFI_STA_GOT_IP)
      Serial.printf("EVENT t=%lu STA_GOT_IP\n", (unsigned long)item.atMs);
    else if (item.type == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
      Serial.printf("EVENT t=%lu STA_DISCONNECTED reason=%u\n", (unsigned long)item.atMs, item.reason);
    else
      Serial.printf("EVENT t=%lu STA_LOST_IP\n", (unsigned long)item.atMs);
  }
}

void clearAttemptEvents() {
  drainEvents();
  portENTER_CRITICAL(&eventMux);
  lastDisconnectReason = -1;
  portEXIT_CRITICAL(&eventMux);
}

bool readLine(String& value, size_t maxBytes) {
  value = "";
  bool tooLong = false;
  while (true) {
    if (!Serial.available()) { delay(10); continue; }
    const char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') return !tooLong;
    if (value.length() < maxBytes) value += ch;
    else tooLong = true;
  }
}

Candidate scanTarget(const String& ssid, const char* phase) {
  Candidate best;
  Serial.printf("SCAN %s begin (target SSID is not printed)\n", phase);
  const int16_t count = WiFi.scanNetworks(false, true);
  if (count < 0) {
    Serial.printf("SCAN %s failed code=%d\n", phase, count);
    return best;
  }
  int matches = 0;
  for (int i = 0; i < count; ++i) {
    if (WiFi.SSID(i) != ssid) continue;
    ++matches;
    uint8_t bssid[6] = {};
    WiFi.BSSID(i, bssid);
    const int32_t rssi = WiFi.RSSI(i);
    const int32_t channel = WiFi.channel(i);
    const wifi_auth_mode_t auth = WiFi.encryptionType(i);
    Serial.printf("SCAN %s match=%d RSSI=%ld dBm channel=%ld auth=%s(%d) BSSID_suffix=%02X:%02X:%02X\n",
      phase, matches, (long)rssi, (long)channel, authName(auth), (int)auth,
      bssid[3], bssid[4], bssid[5]);
    if (!best.found || rssi > best.rssi) {
      best.found = true; best.rssi = rssi; best.channel = channel;
      memcpy(best.bssid, bssid, sizeof(best.bssid));
    }
  }
  Serial.printf("SCAN %s total=%d target_matches=%d\n", phase, count, matches);
  WiFi.scanDelete();
  return best;
}

bool observeConnection(const char* mode, wl_status_t beginResult) {
  const uint32_t start = millis();
  Serial.printf("CONNECT %s begin_result=%d observe_ms=%lu\n",
    mode, (int)beginResult, (unsigned long)CONNECT_OBSERVE_MS);
  while (millis() - start < CONNECT_OBSERVE_MS) {
    drainEvents();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("CONNECT %s success elapsed_ms=%lu RSSI=%ld dBm channel=%d IP=%s\n",
        mode, (unsigned long)(millis() - start), (long)WiFi.RSSI(),
        WiFi.channel(), WiFi.localIP().toString().c_str());
      return true;
    }
    delay(10);
  }
  drainEvents();
  int reason;
  portENTER_CRITICAL(&eventMux);
  reason = lastDisconnectReason;
  portEXIT_CRITICAL(&eventMux);
  Serial.printf("CONNECT %s failed status=%d last_reason=%d elapsed_ms=%lu\n",
    mode, (int)WiFi.status(), reason, (unsigned long)(millis() - start));
  return false;
}

void runDiagnostic(const String& ssid, const String& password) {
  WiFi.disconnect(false, false); // Disconnect RAM STA state; do not erase credentials.
  delay(300);
  clearAttemptEvents();
  // Try the gateway's ordinary two-argument WiFi.begin() before any scan.
  // Keep observing after a disconnect to test whether early failover matters.
  const bool autoOk = observeConnection("AUTO", WiFi.begin(ssid.c_str(), password.c_str()));
  if (!autoOk) {
    WiFi.disconnect(false, false);
    delay(300);
    clearAttemptEvents();
    Candidate best = scanTarget(ssid, "after-auto-1");
    delay(1000);
    Candidate repeat = scanTarget(ssid, "after-auto-2");
    if (repeat.found && (!best.found || repeat.rssi > best.rssi)) best = repeat;
    if (best.found) {
      WiFi.disconnect(false, false);
      delay(300);
      clearAttemptEvents();
      Serial.printf("PINNED target channel=%ld BSSID_suffix=%02X:%02X:%02X\n",
        (long)best.channel, best.bssid[3], best.bssid[4], best.bssid[5]);
      observeConnection("PINNED", WiFi.begin(ssid.c_str(), password.c_str(), best.channel, best.bssid));
    } else Serial.println("PINNED skipped: target was absent from both scans");
  }
  Serial.printf("DIAG event_drops=%lu (0 expected)\n", (unsigned long)eventDrops);
}

void setup() {
  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 10000) delay(10);
  Serial.printf("ESP32-S3 Wi-Fi diagnostic core=%s reset_reason=%d\n",
    ESP_ARDUINO_VERSION_STR, (int)esp_reset_reason());
  Serial.println("No passwords are printed or stored by this sketch. Do not share the Serial Monitor input field.");
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.onEvent(onWifiEvent);
}

void loop() {
  String ssid, password;
  Serial.println("Enter target SSID, then press Enter (Newline enabled):");
  if (!readLine(ssid, 32) || ssid.isEmpty()) {
    Serial.println("Invalid SSID: use 1 to 32 UTF-8 bytes."); return;
  }
  Serial.println("Enter password, then press Enter (not echoed by device; empty for open network):");
  if (!readLine(password, 64)) {
    Serial.println("Invalid password: maximum 64 bytes."); return;
  }
  runDiagnostic(ssid, password);
  // Forget the entered strings before the next run. The Arduino input field
  // itself may still show what was typed until the user clears it.
  ssid = ""; password = "";
  Serial.println("Test finished. Enter another SSID to repeat; no credentials were saved.");
}
