#pragma once
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
private:
  ByteRing& rx_;
  ByteRing tx_;
  WebSocketsClient ws_;
  std::atomic<uint8_t> state_{(uint8_t)LinkState::Searching};
  std::atomic<bool> wantPrompt_{false};     // set on connect: the S3 prints nothing until it gets input
  bool wsStarted_ = false, wasConnected_ = false;
  uint32_t lastWifiTry_ = 0;
};
