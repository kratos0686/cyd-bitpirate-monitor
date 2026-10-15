#pragma once
#include <Arduino.h>
#include <WebSocketsClient.h>
#include <atomic>
#include "bytering.h"
#include "config.h"
#include "link.h"

class WsLink : public Link {
public:
  explicit WsLink(ByteRing& rx) : rx_(rx), tx_(TX_RING_BYTES) {}
  void begin() override;
  void loop() override;
  LinkState state() const override { return (LinkState)state_.load(); }
  bool send(const uint8_t* d, size_t n) override;
  void scanWifiNetworks() override;
  WifiScanState wifiScanState() const override { return (WifiScanState)scanState_.load(); }
  int wifiNetworkCount() const override;
  bool wifiNetwork(int index, WifiNetwork& network) const override;
  bool setWifiCredentials(const char* ssid, const char* password) override;
private:
  ByteRing& rx_;
  ByteRing tx_;
  WebSocketsClient ws_;
  std::atomic<uint8_t> state_{(uint8_t)LinkState::Searching};
  std::atomic<uint8_t> scanState_{(uint8_t)WifiScanState::Idle};
  std::atomic<bool> scanRequested_{false};
  std::atomic<bool> wantPrompt_{false};     // set on connect: the S3 prints nothing until it gets input
  bool wsStarted_ = false, wasConnected_ = false;
  bool scanInProgress_ = false, wifiChanging_ = false;
  uint32_t lastWifiTry_ = 0;
  char ssid_[33] = {0}, password_[64] = {0};
  char pendingSsid_[33] = {0}, pendingPassword_[64] = {0};
  bool credentialsPending_ = false;
  WifiNetwork networks_[8] = {};
  int networkCount_ = 0;
  mutable portMUX_TYPE networkMux_ = portMUX_INITIALIZER_UNLOCKED;
  portMUX_TYPE commandMux_ = portMUX_INITIALIZER_UNLOCKED;
};
