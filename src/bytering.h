#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>

// Single-producer / single-consumer byte queue. One task calls push(), another calls pop().
// Bytes that do not fit are dropped and counted.
class ByteRing {
public:
  explicit ByteRing(size_t cap) : cap_(cap), buf_(new uint8_t[cap]) {}
  ~ByteRing() { delete[] buf_; }
  ByteRing(const ByteRing&) = delete;
  ByteRing& operator=(const ByteRing&) = delete;

  size_t push(const uint8_t* d, size_t n);      // returns bytes accepted
  size_t pop(uint8_t* out, size_t max);         // returns bytes read
  size_t size() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }
  size_t takeDropped() { return dropped_.exchange(0); }

private:
  const size_t cap_;
  uint8_t* const buf_;
  std::atomic<size_t> head_{0}, tail_{0}, dropped_{0};   // head/tail are monotonic counters
};
