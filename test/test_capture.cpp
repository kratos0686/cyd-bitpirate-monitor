#include "check.h"
#include <string.h>
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

// Frames captured from the S3 web CLI (WebSocket /ws, Task 13): the S3 redraws the whole line on every keystroke.
SUITE(web_cli_stream_tests) {
  auto t = std::make_unique<TermBuf>();
  AnsiParser p(*t);
  const char* frames[] = {"\n", "HIZ> ", "\rHIZ> a\x1b[K", "\rHIZ> ab\x1b[K", "\rHIZ> a \x1b[K", "\x1b[D"};
  for (const char* f : frames) p.feed((const uint8_t*)f, strlen(f));
  std::string row;
  for (int c = 0; c < 8; c++) row += t->cellAt(t->cursorRow(), c).ch;
  CHECK(row.substr(0, 6) == "HIZ> a");     // 'b' was erased by DEL
  CHECK_EQ(t->cellAt(t->cursorRow(), 6).ch, ' ');
  CHECK_EQ(t->cursorCol(), 6);                           // cursor sits just after 'a'
  const char* nl = "\n";
  p.feed((const uint8_t*)nl, 1);
  CHECK_EQ(t->cursorCol(), 0);                           // bare LF acts as CR+LF
}
