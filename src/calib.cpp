#include "calib.h"

int mapAxis(const AxisCal& c, int raw, int lo, int hi) {
  if (c.raw2 == c.raw1) return c.s1;
  long v = c.s1 + (long)(raw - c.raw1) * (c.s2 - c.s1) / (c.raw2 - c.raw1);
  return v < lo ? lo : (v > hi ? hi : (int)v);
}
