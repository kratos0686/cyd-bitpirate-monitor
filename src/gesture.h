#pragma once
#include <stdint.h>
#include "config.h"

constexpr int DRAG_PX = 10;
constexpr uint32_t RELEASE_DEBOUNCE_MS = 50;

enum class GestureType : uint8_t { None, Tap, LongPress, Scroll };
struct GestureEvent { GestureType type; int x, y; int lines; };   // lines > 0: toward older output

class GestureRecognizer {
public:
  GestureEvent update(bool pressed, int x, int y, uint32_t nowMs);   // at most one event per call
private:
  bool down_ = false, dragging_ = false, longFired_ = false;
  int startX_ = 0, startY_ = 0, refY_ = 0, lastX_ = 0, lastY_ = 0;
  uint32_t startT_ = 0, lastSeen_ = 0;
};
