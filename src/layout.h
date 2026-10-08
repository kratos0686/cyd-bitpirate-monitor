#pragma once
#include <stdint.h>
#include "config.h"

struct Rect {
  int x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

enum class BtnKind : uint8_t { Command, Enter, CtrlC, Keyboard };
struct CmdItem { const char* text; bool enter; };           // enter: send Enter after the text
struct CmdButton { const char* label; BtnKind kind; int nItems; CmdItem items[4]; };   // items[0] is the tap action

constexpr int BTN_COLS = 5, BTN_COUNT = 10, BTN_W = 64, BTN_ROW_H = 24;
constexpr int POPUP_W = 128, POPUP_ITEM_H = 22;
constexpr int OVERLAY_TOP = 104;                              // keyboard / popup region starts here
constexpr int OVERLAY_ROWS = (OVERLAY_TOP - TERM_Y) / CHAR_H;   // terminal rows still visible above an overlay (11)

extern const CmdButton CMD_BUTTONS[BTN_COUNT];

Rect buttonRect(int idx);
int buttonAt(int x, int y);
Rect popupItemRect(int btn, int item);        // item 0 is nearest the button row
int popupItemAt(int btn, int x, int y);
