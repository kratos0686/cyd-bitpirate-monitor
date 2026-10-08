#pragma once
#include "ansi.h"
#include "config.h"

struct Cell { char ch; uint8_t color; };

class TermBuf : public TermSink {
public:
  TermBuf();
  // TermSink
  void putChar(char c) override;
  void carriageReturn() override { col_ = 0; }
  void lineFeed() override;                 // also returns to column 0
  void cursorLeft(int n) override;
  void cursorRight(int n) override;
  void eraseInLine(int mode) override;
  void clearScreen() override;              // wipes scrollback too
  void setColor(uint8_t idx) override { color_ = idx; }

  // view
  Cell cellAt(int row, int col) const;
  void scrollBy(int lines);
  void scrollToLive() { viewOffset_ = 0; }
  void setFrozen(bool f);
  bool frozen() const { return frozen_; }
  int viewOffset() const { return viewOffset_; }
  bool cursorVisible() const { return cursorRow() < ROWS; }
  int cursorRow() const { return (int)(lineNo_ - topLine()); }
  int cursorCol() const { return col_ >= COLS ? COLS - 1 : col_; }
  int totalLines() const { return (int)(lineNo_ + 1); }
  int currentLineText(char* out, int cap) const;

private:
  Cell* line(long n) { return lines_[n % SCROLLBACK_LINES]; }
  const Cell* line(long n) const { return lines_[n % SCROLLBACK_LINES]; }
  void clearLine(long n);
  long oldest() const { return lineNo_ >= SCROLLBACK_LINES ? lineNo_ - SCROLLBACK_LINES + 1 : 0; }
  int maxOffset() const;
  long topLine() const;

  Cell lines_[SCROLLBACK_LINES][COLS];
  long lineNo_ = 0;       // absolute index of the cursor's line
  int col_ = 0;
  uint8_t color_ = 7;
  int viewOffset_ = 0;    // lines scrolled back from live
  bool frozen_ = false;
};
