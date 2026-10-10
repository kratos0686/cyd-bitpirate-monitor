#include "calib.h"

int mapAxis(const AxisCal& c, int raw, int lo, int hi) {
  if (c.raw2 == c.raw1) return c.s1;
  long v = c.s1 + (long)(raw - c.raw1) * (c.s2 - c.s1) / (c.raw2 - c.raw1);
  return v < lo ? lo : (v > hi ? hi : (int)v);
}

int medianOf(const int* v, int n) {
  if (n <= 0) return 0;
  if (n > 16) n = 16;
  int s[16];
  for (int i = 0; i < n; i++) s[i] = v[i];
  for (int i = 1; i < n; i++) {                  // insertion sort, n is tiny
    int k = s[i], j = i - 1;
    while (j >= 0 && s[j] > k) { s[j + 1] = s[j]; j--; }
    s[j + 1] = k;
  }
  return s[n / 2];
}
