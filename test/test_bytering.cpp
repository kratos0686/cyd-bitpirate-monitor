#include "check.h"
#include "bytering.h"
#include <thread>
#include <vector>

SUITE(bytering_tests) {
  {  // basic push/pop
    ByteRing r(8);
    const uint8_t in[3] = {1, 2, 3};
    CHECK_EQ(r.push(in, 3), 3);
    CHECK_EQ(r.size(), 3);
    uint8_t out[8];
    CHECK_EQ(r.pop(out, 8), 3);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3);
    CHECK_EQ(r.pop(out, 8), 0);
  }
  {  // full: excess is dropped and counted (Review Focus 4)
    ByteRing r(8);
    uint8_t in[12];
    for (int i = 0; i < 12; i++) in[i] = (uint8_t)i;
    CHECK_EQ(r.push(in, 12), 8);
    CHECK_EQ(r.takeDropped(), 4);
    CHECK_EQ(r.takeDropped(), 0);
    uint8_t out[16];
    CHECK_EQ(r.pop(out, 16), 8);
    for (int i = 0; i < 8; i++) CHECK_EQ(out[i], i);   // the accepted bytes are the first 8, in order
  }
  {  // wrap-around
    ByteRing r(8);
    uint8_t in[6] = {1, 2, 3, 4, 5, 6}, out[8];
    r.push(in, 6); r.pop(out, 4);
    uint8_t in2[5] = {7, 8, 9, 10, 11};
    CHECK_EQ(r.push(in2, 5), 5);
    CHECK_EQ(r.pop(out, 8), 7);
    const uint8_t want[7] = {5, 6, 7, 8, 9, 10, 11};
    for (int i = 0; i < 7; i++) CHECK_EQ(out[i], want[i]);
  }
  {  // two threads, 2 MB through a 64-byte ring, order preserved
    ByteRing r(64);
    const size_t TOTAL = 2000000;
    std::thread prod([&] {
      size_t sent = 0; uint8_t buf[50];
      while (sent < TOTAL) {
        size_t n = 1 + (sent % 50);
        if (n > TOTAL - sent) n = TOTAL - sent;
        for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)((sent + i) & 0xFF);
        size_t off = 0;
        while (off < n) {            // retry the part that did not fit
          size_t acc = r.push(buf + off, n - off);
          off += acc;
          if (off < n) std::this_thread::yield();
        }
        sent += n;
      }
    });
    size_t got = 0; bool ok = true; uint8_t out[40];
    while (got < TOTAL) {
      size_t n = r.pop(out, sizeof out);
      for (size_t i = 0; i < n; i++) if (out[i] != (uint8_t)((got + i) & 0xFF)) ok = false;
      got += n;
      if (n == 0) std::this_thread::yield();
    }
    prod.join();
    CHECK(ok);
    CHECK_EQ(got, TOTAL);
  }
}
