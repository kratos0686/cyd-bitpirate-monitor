#pragma once
#include <TFT_eSPI.h>
#include "termbuf.h"

// Draws the TermBuf's visible window. Keeps a shadow of what is on screen and only redraws cells that changed.
class Renderer {
public:
  explicit Renderer(TFT_eSPI& tft) : tft_(tft) { invalidate(); }
  void setVisibleRows(int n);            // terminal rows shown; fewer than ROWS while an overlay is open
  void invalidate();                     // redraw everything on the next draw()
  void draw(const TermBuf& tb, bool cursorOn);
private:
  struct Shown { char ch; uint8_t color; bool inv; };
  TFT_eSPI& tft_;
  Shown shown_[ROWS][COLS];
  int vis_ = ROWS;
};
