#include "termbuf.h"

static const Cell BLANK = {' ', 7};

TermBuf::TermBuf() { clearScreen(); }

void TermBuf::clearLine(long n) {
  Cell* l = line(n);
  for (int i = 0; i < COLS; i++) l[i] = BLANK;
}

void TermBuf::clearScreen() {
  for (int r = 0; r < SCROLLBACK_LINES; r++)
    for (int c = 0; c < COLS; c++) lines_[r][c] = BLANK;
  lineNo_ = 0; col_ = 0; viewOffset_ = 0;
}

int TermBuf::maxOffset() const {
  long count = lineNo_ - oldest() + 1;
  return count > ROWS ? (int)(count - ROWS) : 0;
}

long TermBuf::topLine() const {
  long t = lineNo_ - (ROWS - 1) - viewOffset_;
  return t < 0 ? 0 : t;
}

void TermBuf::putChar(char c) {
  if (col_ >= COLS) lineFeed();
  line(lineNo_)[col_++] = Cell{c, color_};
}

void TermBuf::lineFeed() {
  lineNo_++;
  clearLine(lineNo_);
  col_ = 0;
  if (viewOffset_ > 0 || frozen_) {
    viewOffset_++;
    int m = maxOffset();
    if (viewOffset_ > m) viewOffset_ = m;
  }
}

void TermBuf::cursorLeft(int n) {
  col_ -= n;
  if (col_ < 0) col_ = 0;
}

void TermBuf::cursorRight(int n) {
  int t = col_ + n;
  if (t > COLS - 1) t = COLS - 1;
  if (t > col_) col_ = t;
}

void TermBuf::eraseInLine(int mode) {
  Cell* l = line(lineNo_);
  int c = col_ >= COLS ? COLS - 1 : col_;
  int from = 0, to = COLS - 1;
  if (mode == 0) from = c;
  else if (mode == 1) to = c;
  for (int i = from; i <= to; i++) l[i] = BLANK;
}

Cell TermBuf::cellAt(int row, int col) const {
  long n = topLine() + row;
  if (row < 0 || row >= ROWS || col < 0 || col >= COLS || n > lineNo_ || n < oldest()) return BLANK;
  return line(n)[col];
}

void TermBuf::scrollBy(int lines) {
  long v = (long)viewOffset_ + lines;
  int m = maxOffset();
  viewOffset_ = v < 0 ? 0 : (v > m ? m : (int)v);
}

void TermBuf::setFrozen(bool f) {
  frozen_ = f;
  if (!f) viewOffset_ = 0;
}

int TermBuf::currentLineText(char* out, int cap) const {
  const Cell* l = line(lineNo_);
  int end = COLS;
  while (end > 0 && l[end - 1].ch == ' ') end--;
  int n = end < cap - 1 ? end : cap - 1;
  for (int i = 0; i < n; i++) out[i] = l[i].ch;
  out[n] = 0;
  return n;
}
