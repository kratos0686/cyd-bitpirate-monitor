#include "bytering.h"

size_t ByteRing::push(const uint8_t* d, size_t n) {
  size_t head = head_.load(std::memory_order_relaxed);
  size_t tail = tail_.load(std::memory_order_acquire);
  size_t freeB = cap_ - (head - tail);
  size_t acc = n < freeB ? n : freeB;
  for (size_t i = 0; i < acc; i++) buf_[(head + i) % cap_] = d[i];
  head_.store(head + acc, std::memory_order_release);
  if (acc < n) dropped_.fetch_add(n - acc);
  return acc;
}

size_t ByteRing::pop(uint8_t* out, size_t max) {
  size_t tail = tail_.load(std::memory_order_relaxed);
  size_t head = head_.load(std::memory_order_acquire);
  size_t avail = head - tail;
  size_t n = avail < max ? avail : max;
  for (size_t i = 0; i < n; i++) out[i] = buf_[(tail + i) % cap_];
  tail_.store(tail + n, std::memory_order_release);
  return n;
}
