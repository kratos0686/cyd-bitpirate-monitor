#include "layout.h"

const CmdButton CMD_BUTTONS[BTN_COUNT] = {
  {"mode",   BtnKind::Command, 4, {{"mode", true}, {"system", true}, {"profile", true}, {"man", true}}},
  {"help",   BtnKind::Command, 4, {{"help", true}, {"man", true}, {"alias", true}, {"hex ", false}}},
  {"scan",   BtnKind::Command, 3, {{"scan", true}, {"ping", true}, {"autobaud", true}}},
  {"read",   BtnKind::Command, 3, {{"read", true}, {"raw", true}, {"temp", true}}},
  {"write",  BtnKind::Command, 3, {{"write ", false}, {"spam ", false}, {"trigger ", false}}},
  {"sniff",  BtnKind::Command, 2, {{"sniff", true}, {"sniff raw", true}}},
  {"config", BtnKind::Command, 3, {{"config", true}, {"profile", true}, {"alias", true}}},
  {"^C",     BtnKind::CtrlC,   0, {}},
  {"KB",     BtnKind::Keyboard, 0, {}},
  {"Enter",  BtnKind::Enter,   0, {}},
};

Rect buttonRect(int idx) {
  return Rect{(idx % BTN_COLS) * BTN_W, BTN_Y + (idx / BTN_COLS) * BTN_ROW_H, BTN_W, BTN_ROW_H};
}

int buttonAt(int x, int y) {
  if (x < 0 || x >= SCREEN_W || y < BTN_Y || y >= SCREEN_H) return -1;
  return ((y - BTN_Y) / BTN_ROW_H) * BTN_COLS + x / BTN_W;
}

Rect popupItemRect(int btn, int item) {
  int x = buttonRect(btn).x;
  if (x > SCREEN_W - POPUP_W) x = SCREEN_W - POPUP_W;
  return Rect{x, BTN_Y - (item + 1) * POPUP_ITEM_H, POPUP_W, POPUP_ITEM_H};
}

int popupItemAt(int btn, int x, int y) {
  for (int i = 0; i < CMD_BUTTONS[btn].nItems; i++)
    if (popupItemRect(btn, i).contains(x, y)) return i;
  return -1;
}
