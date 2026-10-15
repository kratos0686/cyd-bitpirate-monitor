#ifndef REPLAY_MODE
#include "wslink.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <string.h>
#include "secrets.h"

void WsLink::begin() {
  Preferences prefs;
  strncpy(ssid_, WS_SSID, sizeof ssid_ - 1);
  strncpy(password_, WS_PASS, sizeof password_ - 1);
  if (prefs.begin("cyd-wifi", false)) {
    prefs.getString("ssid", ssid_, sizeof ssid_);
    prefs.getString("password", password_, sizeof password_);
    prefs.end();
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);             // loop() owns reconnects so the reported state stays accurate
  ws_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
      case WStype_CONNECTED:
        wasConnected_ = true;
        wantPrompt_ = true;
        state_ = (uint8_t)LinkState::Connected;
        break;
      case WStype_DISCONNECTED:
        if (state_.load() == (uint8_t)LinkState::Connected) state_ = (uint8_t)LinkState::Lost;
        break;
      case WStype_TEXT:
      case WStype_BIN:
        rx_.push(payload, length);
        break;
      default: break;
    }
  });
  ws_.setReconnectInterval(2000);
  ws_.enableHeartbeat(15000, 3000, 2);      // ping every 15 s; 2 missed pongs = dead link
  lastWifiTry_ = millis() - 5000;           // try immediately
}

void WsLink::scanWifiNetworks() {
  if (scanState_.load() == (uint8_t)WifiScanState::Scanning) return;
  portENTER_CRITICAL(&networkMux_);
  networkCount_ = 0;
  portEXIT_CRITICAL(&networkMux_);
  scanState_ = (uint8_t)WifiScanState::Scanning;
  scanRequested_ = true;
}

int WsLink::wifiNetworkCount() const {
  portENTER_CRITICAL(&networkMux_);
  int count = networkCount_;
  portEXIT_CRITICAL(&networkMux_);
  return count;
}

bool WsLink::wifiNetwork(int index, WifiNetwork& network) const {
  portENTER_CRITICAL(&networkMux_);
  bool valid = index >= 0 && index < networkCount_;
  if (valid) network = networks_[index];
  portEXIT_CRITICAL(&networkMux_);
  return valid;
}

bool WsLink::setWifiCredentials(const char* ssid, const char* password) {
  if (!ssid || !password) return false;
  size_t ssidLength = strnlen(ssid, sizeof pendingSsid_);
  size_t passwordLength = strnlen(password, sizeof pendingPassword_);
  if (ssidLength == 0 || ssidLength >= sizeof pendingSsid_ || passwordLength >= sizeof pendingPassword_) return false;
  portENTER_CRITICAL(&commandMux_);
  memcpy(pendingSsid_, ssid, ssidLength + 1);
  memcpy(pendingPassword_, password, passwordLength + 1);
  credentialsPending_ = true;
  portEXIT_CRITICAL(&commandMux_);
  return true;
}

static void addNetwork(WifiNetwork* networks, int& count, const char* ssid, int rssi, bool secure) {
  if (!ssid[0]) return;
  for (int i = 0; i < count; ++i) {
    if (strcmp(networks[i].ssid, ssid) == 0) {
      if (rssi > networks[i].rssi) networks[i].rssi = rssi;
      return;
    }
  }
  int pos = 0;
  while (pos < count && networks[pos].rssi >= rssi) ++pos;
  if (pos >= 8) return;
  if (count < 8) ++count;
  for (int i = count - 1; i > pos; --i) networks[i] = networks[i - 1];
  strncpy(networks[pos].ssid, ssid, sizeof networks[pos].ssid - 1);
  networks[pos].ssid[sizeof networks[pos].ssid - 1] = 0;
  networks[pos].rssi = rssi;
  networks[pos].secure = secure;
}

void WsLink::loop() {
  char requestedSsid[sizeof pendingSsid_], requestedPassword[sizeof pendingPassword_];
  bool credentialsChanged = false;
  portENTER_CRITICAL(&commandMux_);
  if (credentialsPending_) {
    memcpy(requestedSsid, pendingSsid_, sizeof requestedSsid);
    memcpy(requestedPassword, pendingPassword_, sizeof requestedPassword);
    credentialsPending_ = false;
    credentialsChanged = true;
  }
  portEXIT_CRITICAL(&commandMux_);
  if (credentialsChanged) {
    memcpy(ssid_, requestedSsid, sizeof ssid_);
    memcpy(password_, requestedPassword, sizeof password_);
    Preferences prefs;
    if (prefs.begin("cyd-wifi", false)) {
      prefs.putString("ssid", ssid_);
      prefs.putString("password", password_);
      prefs.end();
    }
    ws_.disconnect();
    wsStarted_ = false;
    WiFi.disconnect();
    wifiChanging_ = true;
    lastWifiTry_ = millis() - 5000;
    state_ = (uint8_t)LinkState::Searching;
    return;
  }

  if (scanRequested_.exchange(false)) {
    scanInProgress_ = WiFi.scanNetworks(true, true) == WIFI_SCAN_RUNNING;
    if (!scanInProgress_) scanState_ = (uint8_t)WifiScanState::Failed;
  }
  if (scanInProgress_) {
    int result = WiFi.scanComplete();
    if (result >= 0) {
      WifiNetwork found[8] = {};
      int count = 0;
      for (int i = 0; i < result; ++i) {
        String name = WiFi.SSID(i);
        addNetwork(found, count, name.c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      }
      portENTER_CRITICAL(&networkMux_);
      memcpy(networks_, found, sizeof networks_);
      networkCount_ = count;
      portEXIT_CRITICAL(&networkMux_);
      WiFi.scanDelete();
      scanInProgress_ = false;
      scanState_ = (uint8_t)WifiScanState::Complete;
    } else if (result != WIFI_SCAN_RUNNING) {
      scanInProgress_ = false;
      scanState_ = (uint8_t)WifiScanState::Failed;
    }
  }

  if (wifiChanging_) {
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect();
    else wifiChanging_ = false;
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    state_ = (uint8_t)(wasConnected_ ? LinkState::Lost : LinkState::Searching);
    if (wsStarted_) { ws_.disconnect(); wsStarted_ = false; }
    uint8_t junk[32]; while (tx_.pop(junk, sizeof junk) > 0) {}
    if (millis() - lastWifiTry_ >= 5000) {
      WiFi.disconnect();
      WiFi.begin(ssid_, password_);
      lastWifiTry_ = millis();
    }
    return;
  }
  if (!wsStarted_) {
    ws_.begin(WS_HOST, WS_PORT, WS_PATH);
    wsStarted_ = true;
    state_ = (uint8_t)LinkState::Connecting;
  }
  ws_.loop();

  uint8_t b[64]; size_t n;
  if (state_.load() == (uint8_t)LinkState::Connected) {
    if (wantPrompt_.exchange(false)) ws_.sendTXT((const uint8_t*)"\r", 1);   // an empty line makes the S3 show its prompt
    while ((n = tx_.pop(b, sizeof b)) > 0) {
      if (WS_USE_TEXT) ws_.sendTXT(b, n); else ws_.sendBIN(b, n);
    }
  } else {
    while (tx_.pop(b, sizeof b) > 0) {}     // never deliver stale keystrokes after a reconnect
  }
}

bool WsLink::send(const uint8_t* d, size_t n) {
  if (state() != LinkState::Connected) return false;
  return tx_.push(d, n) == n;
}

#endif  // REPLAY_MODE
