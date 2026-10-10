#include "ui.h"
#include <string.h>
#include "prompt.h"

namespace {
constexpr uint16_t C_BG = 0x0000, C_BAR = 0x18E3, C_BTN = 0x2945, C_KEY = 0x39E7, C_POP = 0x2A6F;
constexpr uint16_t C_TEXT = 0xFFFF, C_OK = 0x07E0, C_WARN = 0xFFE0, C_BAD = 0xF800;
}

void Ui::begin() {
  tft_.fillScreen(C_BG);
  drawButtons();
  drawStatus(true);
  rend_.invalidate();
}

// ---- sending -------------------------------------------------------------------------------

void Ui::sendByte(uint8_t b) {
  if (link_.state() != LinkState::Connected) return;      // spec: keys typed while disconnected are dropped
  if (!term_.frozen()) term_.scrollToLive();
  link_.send(&b, 1);
}

void Ui::sendText(const char* s) {
  if (link_.state() != LinkState::Connected) return;
  if (!term_.frozen()) term_.scrollToLive();
  link_.send((const uint8_t*)s, strlen(s));
}

// ---- events --------------------------------------------------------------------------------

void Ui::handle(const GestureEvent& e) {
  switch (e.type) {
    case GestureType::Tap: onTap(e.x, e.y); break;
    case GestureType::LongPress: onLongPress(e.x, e.y); break;
    case GestureType::Scroll: {
      int bottom = (kbOpen_ || popupBtn_ >= 0) ? OVERLAY_TOP : BTN_Y;
      if (e.y >= TERM_Y && e.y < bottom) term_.scrollBy(e.lines);
      break;
    }
    default: break;
  }
}

void Ui::onTap(int x, int y) {
  if (popupBtn_ >= 0) {
    int it = popupItemAt(popupBtn_, x, y);
    if (it >= 0) {
      const CmdItem& ci = CMD_BUTTONS[popupBtn_].items[it];
      sendText(ci.text);
      if (ci.enter) sendText(ENTER_STR);
    }
    closeOverlay();
    return;
  }
  if (y < STATUS_H) { togglePause(); return; }
  if (kbOpen_) { doKey(kb_.hit(x, y)); return; }
  int b = buttonAt(x, y);
  if (b < 0) return;
  const CmdButton& cb = CMD_BUTTONS[b];
  switch (cb.kind) {
    case BtnKind::Command:
      sendText(cb.items[0].text);
      if (cb.items[0].enter) sendText(ENTER_STR);
      break;
    case BtnKind::Enter: sendText(ENTER_STR); break;
    case BtnKind::CtrlC: sendByte(0x03); break;
    case BtnKind::Keyboard: openKeyboard(); break;
  }
}

void Ui::onLongPress(int x, int y) {
  if (popupBtn_ >= 0 || kbOpen_) return;
  int b = buttonAt(x, y);
  if (b >= 0) {
    if (CMD_BUTTONS[b].kind == BtnKind::Command && CMD_BUTTONS[b].nItems >= 2) openPopup(b);
    return;
  }
  if (y >= TERM_Y && y < BTN_Y) { term_.clearScreen(); rend_.invalidate(); }
}

void Ui::doKey(Key k) {
  switch (k.type) {
    case KeyType::Char: case KeyType::Space: sendByte((uint8_t)k.ch); break;
    case KeyType::Backspace: sendByte(BACKSPACE_BYTE); break;
    case KeyType::Enter: sendText(ENTER_STR); break;
    case KeyType::CtrlC: sendByte(0x03); break;
    case KeyType::Page: kb_.setPage(1 - kb_.page()); drawKeyboard(); break;
    case KeyType::Hide: closeOverlay(); break;
    default: break;
  }
}

void Ui::togglePause() {
  term_.setFrozen(!term_.frozen());
  drawStatus(false);
}

// ---- overlays ------------------------------------------------------------------------------

void Ui::openKeyboard() {
  kbOpen_ = true;
  rend_.setVisibleRows(OVERLAY_ROWS);
  drawKeyboard();
}

void Ui::openPopup(int btn) {
  popupBtn_ = btn;
  rend_.setVisibleRows(OVERLAY_ROWS);
  drawPopup();
}

void Ui::closeOverlay() {
  kbOpen_ = false;
  popupBtn_ = -1;
  tft_.fillRect(0, OVERLAY_TOP, SCREEN_W, BTN_Y - OVERLAY_TOP, C_BG);
  drawButtons();
  rend_.setVisibleRows(ROWS);                              // also invalidates, so the terminal repaints
}

// ---- drawing -------------------------------------------------------------------------------

void Ui::drawButtons() {
  tft_.setTextDatum(MC_DATUM);
  for (int i = 0; i < BTN_COUNT; i++) {
    Rect r = buttonRect(i);
    tft_.fillRect(r.x, r.y, r.w, r.h, C_BG);
    tft_.fillRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 4, C_BTN);
    tft_.setTextColor(C_TEXT, C_BTN);
    tft_.drawString(CMD_BUTTONS[i].label, r.x + r.w / 2, r.y + r.h / 2, 2);
  }
}

void Ui::drawKeyboard() {
  tft_.fillRect(0, OVERLAY_TOP, SCREEN_W, SCREEN_H - OVERLAY_TOP, C_BG);
  tft_.setTextDatum(MC_DATUM);
  for (int i = 0; i < kb_.keyCount(); i++) {
    const KeyRect& k = kb_.key(i);
    tft_.fillRoundRect(k.x + 1, k.y + 1, k.w - 2, k.h - 2, 3, C_KEY);
    tft_.setTextColor(C_TEXT, C_KEY);
    tft_.drawString(k.label, k.x + k.w / 2, k.y + k.h / 2, 2);
  }
}

void Ui::drawPopup() {
  tft_.fillRect(0, OVERLAY_TOP, SCREEN_W, BTN_Y - OVERLAY_TOP, C_BG);
  tft_.setTextDatum(ML_DATUM);
  const CmdButton& cb = CMD_BUTTONS[popupBtn_];
  for (int i = 0; i < cb.nItems; i++) {
    Rect r = popupItemRect(popupBtn_, i);
    tft_.fillRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, C_POP);
    tft_.setTextColor(C_TEXT, C_POP);
    tft_.drawString(cb.items[i].text, r.x + 6, r.y + r.h / 2, 2);
  }
}

void Ui::drawStatus(bool force) {
  const char* link; uint16_t lc;
  switch (link_.state()) {
    case LinkState::Connected: link = "ONLINE"; lc = C_OK; break;
    case LinkState::Connecting: link = "LINKING"; lc = C_WARN; break;
    case LinkState::Lost: link = "LOST"; lc = C_BAD; break;
    default: link = "WIFI..."; lc = C_WARN; break;
  }
  int linkCode = (int)link_.state();
  int rightCode = term_.frozen() ? 2 : (term_.viewOffset() > 0 ? 1 : 0);
  const char* mode = mode_[0] ? mode_ : "-";
  if (!force && linkCode == shownLink_ && rightCode == shownRight_ && strcmp(mode, shownMode_) == 0) return;
  shownLink_ = linkCode; shownRight_ = rightCode;
  strncpy(shownMode_, mode, sizeof shownMode_ - 1);

  tft_.fillRect(0, 0, SCREEN_W, STATUS_H, C_BAR);
  tft_.setTextDatum(ML_DATUM); tft_.setTextColor(lc, C_BAR);     tft_.drawString(link, 4, STATUS_H / 2, 1);
  tft_.setTextDatum(MC_DATUM); tft_.setTextColor(C_TEXT, C_BAR); tft_.drawString(mode, SCREEN_W / 2, STATUS_H / 2, 1);
  tft_.setTextDatum(MR_DATUM); tft_.setTextColor(C_WARN, C_BAR);
  tft_.drawString(rightCode == 2 ? "PAUSED" : (rightCode == 1 ? "SCROLL" : ""), SCREEN_W - 4, STATUS_H / 2, 1);
}

void Ui::update(uint32_t now) {
  char line[COLS + 1], m[16];
  term_.currentLineText(line, sizeof line);
  if (parsePromptMode(line, m, sizeof m)) strncpy(mode_, m, sizeof mode_ - 1);   // keep last mode when no prompt shows
  drawStatus(false);
  if (now - lastDraw_ >= 33) {
    rend_.draw(term_, (now / 500) % 2 == 0 && term_.cursorVisible());
    lastDraw_ = now;
  }
}
