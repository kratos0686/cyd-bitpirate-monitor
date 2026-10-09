#include "render.h"

namespace {
// VGA-style 16-colour palette in RGB565; index 7 is the default text colour.
const uint16_t PALETTE[16] = {0x0000, 0xA800, 0x0540, 0xAAA0, 0x0015, 0xA815, 0x0555, 0xAD55,
                              0x52AA, 0xFAAA, 0x57EA, 0xFFEA, 0x52BF, 0xFABF, 0x5FFF, 0xFFFF};
}

void Renderer::invalidate() {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) shown_[r][c] = Shown{0, 0, false};   // ch 0 never matches a real cell
}

void Renderer::setVisibleRows(int n) {
  if (n < 1) n = 1;
  if (n > ROWS) n = ROWS;
  if (n != vis_) { vis_ = n; invalidate(); }
}

void Renderer::draw(const TermBuf& tb, bool cursorOn) {
  int curRow = tb.cursorVisible() ? tb.cursorRow() : -1;
  int curCol = tb.cursorCol();
  int base = 0;                                   // first terminal row shown
  if (vis_ < ROWS && curRow >= 0) {               // keep the cursor's row in the window when an overlay is open
    base = curRow - (vis_ - 1);
    if (base < 0) base = 0;
    if (base > ROWS - vis_) base = ROWS - vis_;
  }
  for (int sr = 0; sr < vis_; sr++) {
    int tr = base + sr;
    for (int c = 0; c < COLS; c++) {
      Cell cell = tb.cellAt(tr, c);
      bool inv = cursorOn && tr == curRow && c == curCol;
      Shown want{cell.ch, cell.color, inv};
      Shown& have = shown_[sr][c];
      if (have.ch == want.ch && have.color == want.color && have.inv == want.inv) continue;
      have = want;
      uint16_t fg = PALETTE[cell.color & 15], bg = 0x0000;
      if (inv) { uint16_t t = fg; fg = bg; bg = t; }
      tft_.drawChar(c * CHAR_W, TERM_Y + sr * CHAR_H, cell.ch, fg, bg, 1);
    }
  }
}
