#pragma once
struct AxisCal { int raw1, s1, raw2, s2; };   // two reference points: raw reading -> screen coordinate
struct TouchCal { AxisCal x, y; };
// Typical CYD values for landscape (TFT rotation 1, touch rotation 1); replaced by the calibration screen.
constexpr TouchCal DEFAULT_TOUCH_CAL = {{200, 0, 3700, 319}, {240, 0, 3800, 239}};
int mapAxis(const AxisCal& c, int raw, int lo, int hi);
