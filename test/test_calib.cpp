#include "check.h"
#include "calib.h"

SUITE(calib_tests) {
  AxisCal a{200, 0, 3700, 319};
  CHECK_EQ(mapAxis(a, 200, 0, 319), 0);
  CHECK_EQ(mapAxis(a, 3700, 0, 319), 319);
  CHECK_EQ(mapAxis(a, 1950, 0, 319), 159);
  CHECK_EQ(mapAxis(a, 0, 0, 319), 0);           // clamped
  CHECK_EQ(mapAxis(a, 4095, 0, 319), 319);      // clamped
  AxisCal inv{3800, 0, 240, 239};               // inverted axis
  CHECK_EQ(mapAxis(inv, 3800, 0, 239), 0);
  CHECK_EQ(mapAxis(inv, 240, 0, 239), 239);
  CHECK_EQ(mapAxis(inv, 2020, 0, 239), 119);
  AxisCal bad{100, 7, 100, 50};                 // degenerate: no divide by zero
  CHECK_EQ(mapAxis(bad, 123, 0, 319), 7);
}

SUITE(median_tests) {
  int a[5] = {900, 100, 120, 110, 3000};          // two wild outliers around a steady ~110 reading
  CHECK_EQ(medianOf(a, 5), 120);
  int b[3] = {5, 1, 3};
  CHECK_EQ(medianOf(b, 3), 3);
  int c[1] = {42};
  CHECK_EQ(medianOf(c, 1), 42);
  int d[4] = {10, 40, 20, 30};                    // even count: upper middle
  CHECK_EQ(medianOf(d, 4), 30);
  CHECK_EQ(medianOf(a, 0), 0);                    // empty input is safe
}
