#pragma once
#include <stddef.h>
#include <stdint.h>

struct TermSink {
  virtual ~TermSink() {}
  virtual void putChar(char c) = 0;
  virtual void carriageReturn() = 0;
  virtual void lineFeed() = 0;
  virtual void cursorLeft(int n) = 0;
  virtual void cursorRight(int n) = 0;
  virtual void eraseInLine(int mode) = 0;   // 0 cursor->end, 1 start->cursor, 2 whole line
  virtual void clearScreen() = 0;
  virtual void setColor(uint8_t idx) = 0;   // 0..15, 7 = default
};

// Minimal VT100 subset: printable ASCII, CR, LF, BS, TAB(4 spaces), CSI K D C G J m.
// Everything else is ignored. Non-ASCII UTF-8 lead bytes print '?'.
class AnsiParser {
public:
  explicit AnsiParser(TermSink& sink) : sink_(sink) {}
  void feed(uint8_t b);
  void feed(const uint8_t* data, size_t n);

private:
  enum State { GROUND, ESC, SKIP, CSI };
  static constexpr int MAX_PARAMS = 4;
  void startCsi();
  void finishCsi(uint8_t final);
  void applySgr();

  TermSink& sink_;
  State state_ = GROUND;
  int params_[MAX_PARAMS] = {0, 0, 0, 0};
  int idx_ = 0;
  bool priv_ = false;
  int fg_ = 7;
  bool bold_ = false;
};
