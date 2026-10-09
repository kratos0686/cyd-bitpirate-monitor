#pragma once
#include <stddef.h>
#include <stdint.h>

enum class LinkState : uint8_t { Searching, Connecting, Connected, Lost };

class Link {
public:
  virtual ~Link() {}
  virtual void begin() = 0;
  virtual void loop() = 0;                                  // called repeatedly from the link task
  virtual LinkState state() const = 0;
  virtual bool send(const uint8_t* d, size_t n) = 0;        // queues bytes; false (and drops them) if not connected
};
