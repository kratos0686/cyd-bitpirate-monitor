#include "input.h"
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

void TouchInput::begin() {
  spi_.begin(25, 39, 32, 33);           // CLK, MISO, MOSI, CS (the CYD's touch bus)
  ts_.begin(spi_);
  ts_.setRotation(1);
  load();
}

bool TouchInput::readRaw(int& rx, int& ry) {
  if (!ts_.touched()) return false;
  TS_Point p = ts_.getPoint();
  rx = p.x; ry = p.y;
  return true;
}

TouchSample TouchInput::read() {
  int rx, ry;
  if (!readRaw(rx, ry)) return TouchSample{false, 0, 0};
  return TouchSample{true, mapAxis(cal_.x, rx, 0, SCREEN_W - 1), mapAxis(cal_.y, ry, 0, SCREEN_H - 1)};
}

void TouchInput::load() {
  Preferences p;
  if (!p.begin("touch", true)) return;               // storage unavailable: keep the defaults
  if (p.isKey("ax")) {
    cal_.x = AxisCal{p.getInt("ax"), 20, p.getInt("bx"), 300};
    cal_.y = AxisCal{p.getInt("ay"), 20, p.getInt("by"), 220};
    saved_ = true;
  }
  p.end();
}

void TouchInput::save() {
  Preferences p;
  if (!p.begin("touch", false)) return;
  p.putInt("ax", cal_.x.raw1); p.putInt("bx", cal_.x.raw2);
  p.putInt("ay", cal_.y.raw1); p.putInt("by", cal_.y.raw2);
  p.end();
  saved_ = true;
}

static void waitRelease(XPT2046_Touchscreen& ts) {
  uint32_t quiet = millis();
  while (millis() - quiet < 100) { if (ts.touched()) quiet = millis(); delay(5); }
}

void TouchInput::runCalibration(TFT_eSPI& tft) {
  const int tx[2] = {20, 300}, ty[2] = {20, 220};
  int rawX[2], rawY[2];
  for (int i = 0; i < 2; i++) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("Tap the + (hold briefly)", SCREEN_W / 2, 110, 2);
    tft.drawLine(tx[i] - 10, ty[i], tx[i] + 10, ty[i], TFT_YELLOW);
    tft.drawLine(tx[i], ty[i] - 10, tx[i], ty[i] + 10, TFT_YELLOW);
    long sx = 0, sy = 0; int n = 0;
    while (n < 8) {                                   // average 8 samples while pressed
      int rx, ry;
      if (readRaw(rx, ry)) { sx += rx; sy += ry; n++; delay(15); }
      else { sx = sy = 0; n = 0; delay(5); }
    }
    rawX[i] = (int)(sx / n); rawY[i] = (int)(sy / n);
    waitRelease(ts_);
  }
  cal_.x = AxisCal{rawX[0], tx[0], rawX[1], tx[1]};
  cal_.y = AxisCal{rawY[0], ty[0], rawY[1], ty[1]};
  save();
  tft.fillScreen(TFT_BLACK);
}
