#include "check.h"
#include "termbuf.h"
#include <memory>
#include <string>

namespace {
struct Rig {
  TermBuf t; AnsiParser p{t};
  void feed(const std::string& s) { p.feed((const uint8_t*)s.data(), s.size()); }
};
std::string rowText(const TermBuf& t, int r) {
  std::string s;
  for (int c = 0; c < COLS; c++) s += t.cellAt(r, c).ch;
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
}
std::string num(const char* prefix, int i, int width) {
  char b[16]; snprintf(b, sizeof b, "%s%0*d", prefix, width, i); return b;
}
}  // namespace

SUITE(termbuf_tests) {
  {  // basics
    auto r = std::make_unique<Rig>();
    r->feed("abc");
    CHECK_STR(rowText(r->t, 0), "abc");
    CHECK_EQ(r->t.cursorCol(), 3);
    r->feed("\r\ncd");
    CHECK_STR(rowText(r->t, 1), "cd");
  }
  {  // Review Focus 1: bare LF returns to column 0
    auto r = std::make_unique<Rig>();
    r->feed("ab\ncd");
    CHECK_STR(rowText(r->t, 0), "ab");
    CHECK_STR(rowText(r->t, 1), "cd");
  }
  {  // wrap
    auto r = std::make_unique<Rig>();
    r->feed(std::string(60, 'x'));
    CHECK_EQ((int)rowText(r->t, 0).size(), COLS);
    CHECK_STR(rowText(r->t, 1), std::string(60 - COLS, 'x'));
    CHECK_EQ(r->t.cursorCol(), 60 - COLS);
  }
  {  // erase modes and line redraw as the S3 does it
    auto r = std::make_unique<Rig>(); r->feed("hello\x1b[3D\x1b[K");
    CHECK_STR(rowText(r->t, 0), "he");
    r = std::make_unique<Rig>(); r->feed("hello\x1b[2D\x1b[1K");
    CHECK_STR(rowText(r->t, 0), "    o");
    r = std::make_unique<Rig>(); r->feed("hello\x1b[2K");
    CHECK_STR(rowText(r->t, 0), "");
    r = std::make_unique<Rig>(); r->feed("\rHIZ> h\x1b[K\rHIZ> he\x1b[K");
    CHECK_STR(rowText(r->t, 0), "HIZ> he");
    r = std::make_unique<Rig>(); r->feed("\x1b[9Dz");
    CHECK_STR(rowText(r->t, 0), "z");
  }
  {  // color and current line text
    auto r = std::make_unique<Rig>(); r->feed("\x1b[31mr");
    CHECK_EQ(r->t.cellAt(0, 0).color, 1);
    r = std::make_unique<Rig>(); r->feed("HIZ> ");
    char buf[64]; r->t.currentLineText(buf, sizeof buf);
    CHECK_STR(buf, "HIZ>");
  }
  {  // scrolling window: 30 lines, cursor on blank line 30
    auto r = std::make_unique<Rig>();
    for (int i = 0; i < 30; i++) r->feed(num("L", i, 2) + "\r\n");
    CHECK_STR(rowText(r->t, 0), "L09");
    CHECK_STR(rowText(r->t, ROWS - 1), "");
    CHECK_EQ(r->t.cursorRow(), ROWS - 1);
    CHECK(r->t.cursorVisible());
    r->t.scrollBy(5);
    CHECK_STR(rowText(r->t, 0), "L04");
    CHECK(!r->t.cursorVisible());
    r->t.scrollBy(1000);
    CHECK_EQ(r->t.viewOffset(), 9);
    CHECK_STR(rowText(r->t, 0), "L00");
    r->t.scrollToLive();
    CHECK_EQ(r->t.viewOffset(), 0);
    r->t.scrollBy(3);              // scrolled-up view stays put while new lines arrive
    CHECK_STR(rowText(r->t, 0), "L06");
    for (int i = 0; i < 4; i++) r->feed("more\r\n");
    CHECK_STR(rowText(r->t, 0), "L06");
  }
  {  // pause (frozen) keeps the view; unfreeze jumps to live
    auto r = std::make_unique<Rig>();
    for (int i = 0; i < 30; i++) r->feed(num("L", i, 2) + "\r\n");
    r->t.setFrozen(true);
    CHECK_STR(rowText(r->t, 0), "L09");
    for (int i = 0; i < 5; i++) r->feed(num("M", i, 1) + "\r\n");
    CHECK_STR(rowText(r->t, 0), "L09");
    r->t.setFrozen(false);
    CHECK_STR(rowText(r->t, 0), "L14");
  }
  {  // ring wrap: oldest lines are discarded
    auto r = std::make_unique<Rig>();
    const int N = SCROLLBACK_LINES + 50;
    for (int i = 0; i < N; i++) r->feed(num("", i, 3) + "\r\n");
    r->t.scrollBy(1000000);
    CHECK_EQ(r->t.viewOffset(), SCROLLBACK_LINES - ROWS);
    CHECK_STR(rowText(r->t, 0), "051");        // 450 lines written -> cursor on line 450, oldest kept is line 51
  }
  {  // Review Focus 2: very long lines
    auto r = std::make_unique<Rig>();
    r->feed(std::string(5000, 'y'));
    CHECK_STR(rowText(r->t, 0), std::string(COLS, 'y'));
    CHECK_EQ(r->t.cursorCol(), 5000 % COLS);
    r = std::make_unique<Rig>();
    r->feed(std::string(30000, 'y'));            // more than SCROLLBACK_LINES lines
    CHECK_EQ(r->t.cursorCol(), 30000 % COLS);
    for (int row = 0; row < ROWS - 1; row++) CHECK_EQ((int)rowText(r->t, row).size(), COLS);
  }
  {  // clear screen
    auto r = std::make_unique<Rig>();
    for (int i = 0; i < 30; i++) r->feed("x\r\n");
    r->feed("\x1b[2J");
    CHECK_STR(rowText(r->t, 0), "");
    CHECK_EQ(r->t.totalLines(), 1);
    CHECK_EQ(r->t.viewOffset(), 0);
  }
}
