#include "replaylink.h"
#include <Arduino.h>

void ReplayLink::loop() {
  uint32_t now = millis();
  if (now - last_ < 20) return;
  last_ = now;

  uint8_t b[32]; size_t n;
  while ((n = tx_.pop(b, sizeof b)) > 0) {
    for (size_t i = 0; i < n; i++) {
      if (b[i] == '\r') rx_.push((const uint8_t*)"\r\n", 2);
      else if (b[i] == BACKSPACE_BYTE) rx_.push((const uint8_t*)"\b \b", 3);
      else rx_.push(&b[i], 1);
    }
  }

  if (pos_ < len_) {
    size_t take = len_ - pos_ < 32 ? len_ - pos_ : 32;
    rx_.push(data_ + pos_, take);
    pos_ += take;
  } else if (loop_) {
    pos_ = 0;
  }
}
