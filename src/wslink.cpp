#ifndef REPLAY_MODE
#include "wslink.h"
#include <Arduino.h>
#include <WiFi.h>
#include "secrets.h"

void WsLink::begin() {
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

void WsLink::loop() {
  if (WiFi.status() != WL_CONNECTED) {
    state_ = (uint8_t)(wasConnected_ ? LinkState::Lost : LinkState::Searching);
    if (wsStarted_) { ws_.disconnect(); wsStarted_ = false; }
    uint8_t junk[32]; while (tx_.pop(junk, sizeof junk) > 0) {}
    if (millis() - lastWifiTry_ >= 5000) {
      WiFi.disconnect();
      WiFi.begin(WS_SSID, WS_PASS);
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
