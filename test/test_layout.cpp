#include "check.h"
#include "layout.h"
#include <string.h>

SUITE(layout_tests) {
  CHECK_EQ(buttonAt(0, BTN_Y), 0);
  CHECK_EQ(buttonAt(64, BTN_Y), 1);
  CHECK_EQ(buttonAt(0, BTN_Y + 24), 5);
  CHECK_EQ(buttonAt(319, 239), 9);
  CHECK_EQ(buttonAt(10, BTN_Y - 1), -1);
  CHECK_EQ(buttonAt(10, SCREEN_H), -1);
  CHECK_EQ(buttonAt(SCREEN_W, BTN_Y), -1);
  CHECK(CMD_BUTTONS[7].kind == BtnKind::CtrlC);
  CHECK(CMD_BUTTONS[8].kind == BtnKind::Keyboard);
  CHECK(CMD_BUTTONS[9].kind == BtnKind::Enter);

  for (int i = 0; i < BTN_COUNT; i++) {
    Rect r = buttonRect(i);
    CHECK(r.x >= 0 && r.y >= BTN_Y && r.x + r.w <= SCREEN_W && r.y + r.h <= SCREEN_H);
    CHECK_EQ(buttonAt(r.x + r.w / 2, r.y + r.h / 2), i);
    const CmdButton& b = CMD_BUTTONS[i];
    CHECK(b.label && b.label[0]);
    if (b.kind != BtnKind::Command) continue;
    CHECK(b.nItems >= 1 && b.nItems <= 4);
    for (int k = 0; k < b.nItems; k++) CHECK(b.items[k].text && b.items[k].text[0]);
    if (b.nItems < 2) continue;
    for (int k = 0; k < b.nItems; k++) {          // popup items on screen, stacked upward from the button row
      Rect p = popupItemRect(i, k);
      CHECK(p.x >= 0 && p.x + p.w <= SCREEN_W && p.y >= OVERLAY_TOP && p.y + p.h <= BTN_Y);
      CHECK_EQ(p.y + p.h, BTN_Y - k * POPUP_ITEM_H);
      CHECK_EQ(popupItemAt(i, p.x + 2, p.y + 2), k);
    }
    CHECK_EQ(popupItemAt(i, 0, 0), -1);
  }
  CHECK(popupItemRect(4, 0).x + POPUP_W <= SCREEN_W);   // rightmost button's popup is clamped
}
