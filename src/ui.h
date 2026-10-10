#pragma once
#include <TFT_eSPI.h>
#include "gesture.h"
#include "keyboard.h"
#include "layout.h"
#include "link.h"
#include "render.h"
#include "termbuf.h"

class Ui {
public:
  Ui(TFT_eSPI& tft, TermBuf& term, Renderer& rend, Link& link)
    : tft_(tft), term_(term), rend_(rend), link_(link), kb_(0, OVERLAY_TOP, SCREEN_W, SCREEN_H - OVERLAY_TOP) {}
  void begin();
  void handle(const GestureEvent& e);
  void update(uint32_t nowMs);

private:
  void onTap(int x, int y);
  void onLongPress(int x, int y);
  void doKey(Key k);
  void sendText(const char* s);
  void sendByte(uint8_t b);
  void togglePause();
  void openKeyboard();
  void openPopup(int btn);
  void closeOverlay();
  void drawStatus(bool force);
  void drawButtons();
  void drawKeyboard();
  void drawPopup();

  TFT_eSPI& tft_;
  TermBuf& term_;
  Renderer& rend_;
  Link& link_;
  Keyboard kb_;
  bool kbOpen_ = false;
  int popupBtn_ = -1;
  char mode_[16] = {0};
  int shownLink_ = -1, shownRight_ = -1;
  char shownMode_[16] = {0};
  uint32_t lastDraw_ = 0;
};
