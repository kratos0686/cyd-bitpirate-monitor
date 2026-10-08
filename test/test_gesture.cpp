#include "check.h"
#include "gesture.h"

SUITE(gesture_tests) {
  {  // tap
    GestureRecognizer g;
    CHECK(g.update(true, 100, 100, 0).type == GestureType::None);
    CHECK(g.update(true, 101, 100, 50).type == GestureType::None);
    GestureEvent e = g.update(false, 0, 0, 200);
    CHECK(e.type == GestureType::Tap);
    CHECK_EQ(e.x, 100); CHECK_EQ(e.y, 100);
    CHECK(g.update(false, 0, 0, 300).type == GestureType::None);
  }
  {  // jitter under the drag threshold is still a tap
    GestureRecognizer g;
    g.update(true, 50, 50, 0); g.update(true, 55, 56, 20);
    CHECK(g.update(false, 0, 0, 100).type == GestureType::Tap);
  }
  {  // long press fires once, release afterwards is silent
    GestureRecognizer g;
    g.update(true, 40, 60, 0);
    CHECK(g.update(true, 40, 60, 599).type == GestureType::None);
    GestureEvent e = g.update(true, 40, 60, 600);
    CHECK(e.type == GestureType::LongPress);
    CHECK_EQ(e.x, 40); CHECK_EQ(e.y, 60);
    CHECK(g.update(true, 40, 60, 900).type == GestureType::None);
    CHECK(g.update(false, 0, 0, 1000).type == GestureType::None);
    CHECK(g.update(false, 0, 0, 1100).type == GestureType::None);
  }
  {  // scroll: drag down 24 px = 3 lines older, then 8 more = 1
    GestureRecognizer g;
    g.update(true, 100, 100, 0);
    GestureEvent e = g.update(true, 100, 124, 50);
    CHECK(e.type == GestureType::Scroll); CHECK_EQ(e.lines, 3);
    e = g.update(true, 100, 132, 80);
    CHECK(e.type == GestureType::Scroll); CHECK_EQ(e.lines, 1);
    CHECK(g.update(true, 100, 133, 100).type == GestureType::None);
    CHECK(g.update(false, 0, 0, 400).type == GestureType::None);   // a drag is not a tap
  }
  {  // drag up is negative
    GestureRecognizer g;
    g.update(true, 100, 100, 0);
    GestureEvent e = g.update(true, 100, 76, 50);
    CHECK(e.type == GestureType::Scroll); CHECK_EQ(e.lines, -3);
  }
  {  // moving cancels the long press
    GestureRecognizer g;
    g.update(true, 100, 100, 0); g.update(true, 100, 112, 100);
    CHECK(g.update(true, 100, 112, 1000).type == GestureType::None);
  }
  {  // Review Focus 5: a 20 ms touch dropout mid-press does not end the gesture
    GestureRecognizer g;
    g.update(true, 50, 50, 0); g.update(true, 50, 50, 10);
    CHECK(g.update(false, 0, 0, 20).type == GestureType::None);
    CHECK(g.update(true, 50, 50, 30).type == GestureType::None);
    CHECK(g.update(false, 0, 0, 100).type == GestureType::Tap);
  }
  {  // dropout across the long-press moment still fires the long press once
    GestureRecognizer g;
    g.update(true, 70, 70, 0); g.update(true, 70, 70, 580);
    CHECK(g.update(false, 0, 0, 600).type == GestureType::LongPress);
    CHECK(g.update(true, 70, 70, 620).type == GestureType::None);
    CHECK(g.update(false, 0, 0, 900).type == GestureType::None);
  }
  {  // a release lasting >= the debounce ends the gesture; the next press starts a new one
    GestureRecognizer g;
    g.update(true, 10, 10, 0);
    CHECK(g.update(false, 0, 0, 60).type == GestureType::Tap);
    g.update(true, 200, 200, 100);
    GestureEvent e = g.update(false, 0, 0, 200);
    CHECK(e.type == GestureType::Tap); CHECK_EQ(e.x, 200);
  }
}
