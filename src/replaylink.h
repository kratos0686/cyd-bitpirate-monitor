#pragma once
#include "bytering.h"
#include "config.h"
#include "link.h"

// Plays a captured byte stream into the rx queue (32 bytes / 20 ms) and echoes whatever the UI "sends"
// back into it, so the whole display path can be exercised without Wi-Fi or the S3.
class ReplayLink : public Link {
public:
  ReplayLink(ByteRing& rx, const uint8_t* data, size_t len, bool loop)
    : rx_(rx), tx_(TX_RING_BYTES), data_(data), len_(len), loop_(loop) {}
  void begin() override { state_ = LinkState::Connected; }
  void loop() override;
  LinkState state() const override { return state_; }
  bool send(const uint8_t* d, size_t n) override { return tx_.push(d, n) == n; }
private:
  ByteRing& rx_;
  ByteRing tx_;
  const uint8_t* data_; size_t len_; bool loop_;
  size_t pos_ = 0; uint32_t last_ = 0;
  LinkState state_ = LinkState::Searching;
};
