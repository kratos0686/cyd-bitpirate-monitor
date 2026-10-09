#pragma once
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "calib.h"

struct TouchSample { bool pressed; int x, y; };

class TouchInput {
public:
  void begin();
  TouchSample read();
  bool hasCalibration() const { return saved_; }
  void runCalibration(TFT_eSPI& tft);
private:
  bool readRaw(int& rx, int& ry);
  void load();
  void save();
  SPIClass spi_{VSPI};
  XPT2046_Touchscreen ts_{33, 36};      // CS, IRQ
  TouchCal cal_ = DEFAULT_TOUCH_CAL;
  bool saved_ = false;
};
