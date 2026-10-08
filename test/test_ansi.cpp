#include "check.h"
#include "ansi.h"
#include <string>

namespace {
struct RecSink : TermSink {
  std::string log;
  void putChar(char c) override { log += c; }
  void carriageReturn() override { log += "<CR>"; }
  void lineFeed() override { log += "<LF>"; }
  void cursorLeft(int n) override { log += "<L" + std::to_string(n) + ">"; }
  void cursorRight(int n) override { log += "<R" + std::to_string(n) + ">"; }
  void eraseInLine(int m) override { log += "<E" + std::to_string(m) + ">"; }
  void clearScreen() override { log += "<CLS>"; }
  void setColor(uint8_t i) override { log += "<C" + std::to_string(i) + ">"; }
};
struct NullSink : TermSink {
  void putChar(char) override {} void carriageReturn() override {} void lineFeed() override {}
  void cursorLeft(int) override {} void cursorRight(int) override {} void eraseInLine(int) override {}
  void clearScreen() override {} void setColor(uint8_t) override {}
};
std::string run(const std::string& in) {
  RecSink s; AnsiParser p(s);
  p.feed((const uint8_t*)in.data(), in.size());
  return s.log;
}
}  // namespace

SUITE(ansi_tests) {
  CHECK_STR(run("ab\r\n"), "ab<CR><LF>");
  CHECK_STR(run("\b"), "<L1>");
  CHECK_STR(run("\t"), "    ");
  CHECK_STR(run("\x7f"), "");
  CHECK_STR(run("\x07"), "");

  CHECK_STR(run("\x1b[K"), "<E0>");
  CHECK_STR(run("\x1b[0K"), "<E0>");
  CHECK_STR(run("\x1b[1K"), "<E1>");
  CHECK_STR(run("\x1b[2K"), "<E2>");
  CHECK_STR(run("\x1b[3K"), "");

  CHECK_STR(run("\x1b[D"), "<L1>");
  CHECK_STR(run("\x1b[3D"), "<L3>");
  CHECK_STR(run("\x1b[2C"), "<R2>");
  CHECK_STR(run("\x1b[5G"), "<CR><R4>");
  CHECK_STR(run("\x1b[1G"), "<CR>");

  CHECK_STR(run("\x1b[31mred\x1b[0m"), "<C1>red<C7>");
  CHECK_STR(run("\x1b[1;32m"), "<C10>");
  CHECK_STR(run("\x1b[m"), "<C7>");
  CHECK_STR(run("\x1b[91m"), "<C9>");
  CHECK_STR(run("\x1b[1m"), "<C15>");
  CHECK_STR(run("\x1b[1;31m\x1b[22m"), "<C9><C1>");

  CHECK_STR(run("\x1b[2J"), "<CLS>");
  CHECK_STR(run("\x1b[0J"), "");
  CHECK_STR(run("\x1b[?25lX"), "X");           // private sequences ignored

  {  // sequence split across feed() calls
    RecSink s; AnsiParser p(s);
    p.feed(0x1b); p.feed('['); p.feed('3'); p.feed('1'); p.feed('m');
    CHECK_STR(s.log, "<C1>");
  }

  // Review Focus 3: non-ASCII, garbage, unterminated sequences
  CHECK_STR(run("a\xF0\x9F\x93\x8A b"), "a? b");        // 4-byte UTF-8 emoji -> one '?'
  CHECK_STR(run("\xE2\x98\xA0"), "?");                  // 3-byte UTF-8
  CHECK_STR(run("\x1b(Bx"), "x");                       // charset designator swallowed
  CHECK_STR(run("\x1b[3\x1b[K"), "<E0>");               // ESC aborts an unfinished CSI
  CHECK_STR(run("\x1b[99999999999K"), "");              // huge parameter is clamped, not an erase mode
  CHECK_STR(run("\x1b[1;2;3;4;5;6;7;8m"), "<C15>");     // too many parameters tolerated

  {  // fuzz: random bytes, then escape-heavy bytes, must not crash
    NullSink s; AnsiParser p(s);
    unsigned x = 12345;
    for (int i = 0; i < 200000; i++) { x = x * 1664525u + 1013904223u; p.feed((uint8_t)(x >> 24)); }
    const char alphabet[] = "\x1b[;?0123456789mKDCGJH(B";
    for (int i = 0; i < 200000; i++) { x = x * 1664525u + 1013904223u; p.feed((uint8_t)alphabet[(x >> 24) % (sizeof alphabet - 1)]); }
    CHECK(true);
  }
}
