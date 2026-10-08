#include "ansi.h"

void AnsiParser::feed(const uint8_t* d, size_t n) {
  for (size_t i = 0; i < n; i++) feed(d[i]);
}

void AnsiParser::startCsi() {
  idx_ = 0;
  priv_ = false;
  for (int i = 0; i < MAX_PARAMS; i++) params_[i] = 0;
  state_ = CSI;
}

void AnsiParser::feed(uint8_t b) {
  switch (state_) {
    case SKIP:
      state_ = GROUND;
      return;
    case ESC:
      if (b == '[') startCsi();
      else if (b == '(' || b == ')' || b == '*' || b == '+' || b == '#' || b == '%') state_ = SKIP;
      else state_ = GROUND;
      return;
    case CSI:
      if (b >= '0' && b <= '9') {
        int v = params_[idx_] * 10 + (b - '0');
        params_[idx_] = v > 9999 ? 9999 : v;
      } else if (b == ';') {
        if (idx_ < MAX_PARAMS - 1) idx_++;
      } else if (b >= 0x3C && b <= 0x3F) {
        priv_ = true;
      } else if (b >= 0x40 && b <= 0x7E) {
        finishCsi(b);
        state_ = GROUND;
      } else if (b == 0x1B) {
        state_ = ESC;
      }
      return;   // intermediates and control bytes inside a CSI are ignored
    case GROUND:
      break;
  }
  if (b == 0x1B) { state_ = ESC; return; }
  if (b == '\r') { sink_.carriageReturn(); return; }
  if (b == '\n') { sink_.lineFeed(); return; }
  if (b == 0x08) { sink_.cursorLeft(1); return; }
  if (b == '\t') { for (int i = 0; i < 4; i++) sink_.putChar(' '); return; }
  if (b >= 0x20 && b <= 0x7E) { sink_.putChar((char)b); return; }
  if (b >= 0xC0) { sink_.putChar('?'); return; }
  // 0x80-0xBF (UTF-8 continuation), DEL, BEL, other controls: ignored
}

void AnsiParser::finishCsi(uint8_t f) {
  if (priv_) return;
  int n = params_[0] > 0 ? params_[0] : 1;
  switch (f) {
    case 'K': if (params_[0] <= 2) sink_.eraseInLine(params_[0]); break;
    case 'D': sink_.cursorLeft(n); break;
    case 'C': sink_.cursorRight(n); break;
    case 'G': sink_.carriageReturn(); if (n > 1) sink_.cursorRight(n - 1); break;
    case 'J': if (params_[0] == 2 || params_[0] == 3) sink_.clearScreen(); break;
    case 'm': applySgr(); break;
    default: break;
  }
}

void AnsiParser::applySgr() {
  for (int i = 0; i <= idx_; i++) {
    int p = params_[i];
    if (p == 0) { fg_ = 7; bold_ = false; }
    else if (p == 1) bold_ = true;
    else if (p == 22) bold_ = false;
    else if (p >= 30 && p <= 37) fg_ = p - 30;
    else if (p == 39) fg_ = 7;
    else if (p >= 90 && p <= 97) fg_ = p - 90 + 8;
  }
  uint8_t c = (uint8_t)fg_;
  if (bold_ && c < 8) c += 8;
  sink_.setColor(c);
}
