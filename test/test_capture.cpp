#include "check.h"
#include "termbuf.h"
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

SUITE(capture_tests) {
  std::ifstream f("test/captures/s3_session.bin", std::ios::binary);
  CHECK(f.good());
  std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  CHECK(data.size() > 1000);

  auto t = std::make_unique<TermBuf>();
  AnsiParser p(*t);
  p.feed((const uint8_t*)data.data(), data.size());

  bool sawHelp = false, sawPrompt = false, allPrintable = true;
  t->scrollBy(1000000);
  for (;;) {
    for (int r = 0; r < ROWS; r++) {
      std::string s;
      for (int c = 0; c < COLS; c++) {
        char ch = t->cellAt(r, c).ch;
        if (ch < 0x20 || ch > 0x7e) allPrintable = false;
        s += ch;
      }
      if (s.find("ESP32-Bus-Expander") != std::string::npos) sawHelp = true;
      if (s.find("HIZ>") != std::string::npos) sawPrompt = true;
    }
    if (t->viewOffset() == 0) break;
    t->scrollBy(-ROWS);
  }
  CHECK(sawHelp);                       // the help header (line ~0 of 413) has scrolled out of the 400-line ring; check the tail
  CHECK(t->totalLines() > SCROLLBACK_LINES);   // proves the ring wrapped on real data
  CHECK(sawPrompt);
  CHECK(allPrintable);
}
