#include "gesture.h"
#include <stdlib.h>

GestureEvent GestureRecognizer::update(bool pressed, int x, int y, uint32_t now) {
  const GestureEvent none{GestureType::None, 0, 0, 0};
  if (pressed) {
    lastX_ = x; lastY_ = y; lastSeen_ = now;
  } else if (down_ && now - lastSeen_ < RELEASE_DEBOUNCE_MS) {
    pressed = true; x = lastX_; y = lastY_;          // resistive-panel dropout: treat as still pressed
  }

  if (pressed) {
    if (!down_) {
      down_ = true; dragging_ = false; longFired_ = false;
      startX_ = x; startY_ = y; refY_ = y; startT_ = now;
      return none;
    }
    if (!dragging_ && (abs(x - startX_) >= DRAG_PX || abs(y - startY_) >= DRAG_PX)) dragging_ = true;
    if (dragging_) {
      int lines = (y - refY_) / CHAR_H;
      if (lines != 0) {
        refY_ += lines * CHAR_H;
        return GestureEvent{GestureType::Scroll, startX_, startY_, lines};
      }
      return none;
    }
    if (!longFired_ && now - startT_ >= LONG_PRESS_MS) {
      longFired_ = true;
      return GestureEvent{GestureType::LongPress, startX_, startY_, 0};
    }
    return none;
  }

  if (down_) {
    down_ = false;
    if (!dragging_ && !longFired_) return GestureEvent{GestureType::Tap, startX_, startY_, 0};
  }
  return none;
}
