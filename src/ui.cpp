#include "ui.h"
#include <stdio.h>
#include <string.h>
#include "prompt.h"

namespace {
constexpr uint16_t C_BG = 0x0000, C_BAR = 0x18E3, C_BTN = 0x2945, C_KEY = 0x39E7, C_POP = 0x2A6F;
constexpr uint16_t C_TEXT = 0xFFFF, C_OK = 0x07E0, C_WARN = 0xFFE0, C_BAD = 0xF800;
constexpr int WIFI_LIST_Y = 52, WIFI_ROW_H = 19, WIFI_VISIBLE = 7;
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
      if (wifiScreen_ == WifiScreen::Networks) {
        int maxScroll = link_.wifiNetworkCount() - WIFI_VISIBLE;
        if (maxScroll < 0) maxScroll = 0;
        wifiScroll_ += e.lines;
        if (wifiScroll_ < 0) wifiScroll_ = 0;
        if (wifiScroll_ > maxScroll) wifiScroll_ = maxScroll;
        drawWifiScreen();
        break;
      }
      if (wifiScreen_ != WifiScreen::None) break;
      int bottom = (kbOpen_ || popupBtn_ >= 0) ? OVERLAY_TOP : BTN_Y;
      if (e.y >= TERM_Y && e.y < bottom) term_.scrollBy(e.lines);
      break;
    }
    default: break;
  }
}

void Ui::onTap(int x, int y) {
  if (wifiScreen_ != WifiScreen::None) { onWifiTap(x, y); return; }
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
  if (wifiScreen_ != WifiScreen::None) return;
  if (y < STATUS_H) { openWifiScreen(); return; }
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

void Ui::openWifiScreen() {
  kbOpen_ = false;
  popupBtn_ = -1;
  rend_.setVisibleRows(ROWS);
  wifiScreen_ = WifiScreen::Networks;
  wifiScroll_ = 0;
  shownWifiCount_ = shownScanState_ = shownWifiLink_ = -1;
  link_.scanWifiNetworks();
  drawWifiScreen();
}

void Ui::closeWifiScreen() {
  wifiScreen_ = WifiScreen::None;
  kbOpen_ = false;
  popupBtn_ = -1;
  kb_.setPage(0);
  kb_.setCaps(false);
  kb_.setCaseToggle(false);
  tft_.fillScreen(C_BG);
  drawButtons();
  rend_.invalidate();
  drawStatus(true);
}

void Ui::enterWifiPassword() {
  if (!link_.setWifiCredentials(wifiSsid_, wifiPassword_)) return;
  wifiScreen_ = WifiScreen::Connecting;
  wifiSawDisconnect_ = false;
  shownWifiLink_ = -1;
  drawWifiScreen();
}

void Ui::onWifiTap(int x, int y) {
  if (wifiScreen_ == WifiScreen::Password) {
    Key key = kb_.hit(x, y);
    switch (key.type) {
      case KeyType::Char:
      case KeyType::Space: {
        size_t n = strlen(wifiPassword_);
        if (n < sizeof wifiPassword_ - 1) {
          wifiPassword_[n] = key.type == KeyType::Space ? ' ' : key.ch;
          wifiPassword_[n + 1] = 0;
        }
        break;
      }
      case KeyType::Backspace: {
        size_t n = strlen(wifiPassword_);
        if (n) wifiPassword_[n - 1] = 0;
        break;
      }
      case KeyType::CtrlC:
        wifiCaps_ = !wifiCaps_;
        kb_.setCaps(wifiCaps_);
        break;
      case KeyType::Page: kb_.setPage((kb_.page() + 1) % 3); break;
      case KeyType::Enter: enterWifiPassword(); return;
      case KeyType::Hide:
        kb_.setCaseToggle(false);
        wifiScreen_ = WifiScreen::Networks;
        shownWifiCount_ = shownScanState_ = -1;
        drawWifiScreen();
        return;
      default: return;
    }
    drawWifiPassword();
    return;
  }

  if (wifiScreen_ == WifiScreen::Networks) {
    if (y >= BTN_Y) {
      if (x < SCREEN_W / 2) {
        wifiScroll_ = 0;
        link_.scanWifiNetworks();
        shownScanState_ = -1;
        drawWifiScreen();
      } else closeWifiScreen();
      return;
    }
    if (y < WIFI_LIST_Y || y >= WIFI_LIST_Y + WIFI_ROW_H * WIFI_VISIBLE) return;
    int index = wifiScroll_ + (y - WIFI_LIST_Y) / WIFI_ROW_H;
    WifiNetwork network;
    if (!link_.wifiNetwork(index, network)) return;
    strncpy(wifiSsid_, network.ssid, sizeof wifiSsid_ - 1);
    wifiSsid_[sizeof wifiSsid_ - 1] = 0;
    wifiPassword_[0] = 0;
    wifiCaps_ = false;
    kb_.setPage(0);
    kb_.setCaps(false);
    kb_.setCaseToggle(true);
    wifiScreen_ = WifiScreen::Password;
    drawWifiPassword();
    return;
  }

  if (wifiScreen_ == WifiScreen::Connecting && y >= BTN_Y) {
    if (x < SCREEN_W / 2) {
      wifiScreen_ = WifiScreen::Networks;
      shownWifiCount_ = shownScanState_ = -1;
      drawWifiScreen();
    } else closeWifiScreen();
  }
}

// ---- overlays ------------------------------------------------------------------------------

void Ui::openKeyboard() {
  kb_.setPage(0);
  kb_.setCaps(false);
  kb_.setCaseToggle(false);
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

void Ui::drawWifiActions(const char* left, const char* right) {
  tft_.fillRect(0, BTN_Y, SCREEN_W, BTN_H, C_BG);
  tft_.fillRoundRect(4, BTN_Y + 3, SCREEN_W / 2 - 8, BTN_H - 6, 4, C_BTN);
  tft_.fillRoundRect(SCREEN_W / 2 + 4, BTN_Y + 3, SCREEN_W / 2 - 8, BTN_H - 6, 4, C_BTN);
  tft_.setTextDatum(MC_DATUM);
  tft_.setTextColor(C_TEXT, C_BTN);
  tft_.drawString(left, SCREEN_W / 4, BTN_Y + BTN_H / 2, 2);
  tft_.drawString(right, SCREEN_W * 3 / 4, BTN_Y + BTN_H / 2, 2);
}

void Ui::drawWifiScreen() {
  tft_.fillRect(0, STATUS_H, SCREEN_W, SCREEN_H - STATUS_H, C_BG);
  tft_.setTextDatum(ML_DATUM);
  tft_.setTextColor(C_TEXT, C_BG);
  if (wifiScreen_ == WifiScreen::Networks) {
    tft_.drawString("Wi-Fi networks", 6, 23, 2);
    WifiScanState state = link_.wifiScanState();
    const char* message = state == WifiScanState::Scanning ? "Scanning..." :
                          state == WifiScanState::Failed ? "Scan failed; tap Scan" :
                          state == WifiScanState::Complete && link_.wifiNetworkCount() == 0 ? "No networks found" :
                          state == WifiScanState::Complete ? "Tap a network to connect" : "Tap Scan to find networks";
    tft_.setTextColor(C_WARN, C_BG);
    tft_.drawString(message, 6, 40, 1);
    int count = link_.wifiNetworkCount();
    for (int row = 0; row < WIFI_VISIBLE; ++row) {
      int index = wifiScroll_ + row;
      if (index >= count) break;
      WifiNetwork network;
      if (!link_.wifiNetwork(index, network)) continue;
      int y = WIFI_LIST_Y + row * WIFI_ROW_H;
      tft_.setTextColor(C_TEXT, C_BG);
      char name[23];
      snprintf(name, sizeof name, "%.22s", network.ssid);
      tft_.drawString(name, 6, y + WIFI_ROW_H / 2, 1);
      char info[20];
      snprintf(info, sizeof info, "%ddBm %s", network.rssi, network.secure ? "LOCK" : "OPEN");
      tft_.setTextDatum(MR_DATUM);
      tft_.setTextColor(network.secure ? C_WARN : C_OK, C_BG);
      tft_.drawString(info, SCREEN_W - 6, y + WIFI_ROW_H / 2, 1);
      tft_.setTextDatum(ML_DATUM);
    }
    if (count > WIFI_VISIBLE) {
      char scroll[24];
      snprintf(scroll, sizeof scroll, "%d-%d of %d", wifiScroll_ + 1,
               wifiScroll_ + WIFI_VISIBLE < count ? wifiScroll_ + WIFI_VISIBLE : count, count);
      tft_.setTextColor(C_WARN, C_BG);
      tft_.setTextDatum(MR_DATUM);
      tft_.drawString(scroll, SCREEN_W - 6, 184, 1);
      tft_.setTextDatum(ML_DATUM);
    }
    drawWifiActions("SCAN", "DONE");
  } else if (wifiScreen_ == WifiScreen::Password) {
    drawWifiPassword();
  } else if (wifiScreen_ == WifiScreen::Connecting) {
    tft_.drawString("Connecting to", 6, 26, 2);
    tft_.setTextColor(C_WARN, C_BG);
    tft_.drawString(wifiSsid_, 6, 48, 2);
    LinkState state = link_.state();
    const char* message = state == LinkState::Connected ? "Reconnecting to the Bit Pirate..." :
                          state == LinkState::Connecting ? "Joining Wi-Fi..." : "Waiting for Wi-Fi...";
    tft_.setTextColor(C_TEXT, C_BG);
    tft_.drawString(message, 6, 75, 2);
    drawWifiActions("NETWORKS", "DONE");
  }
}

void Ui::drawWifiPassword() {
  tft_.fillRect(0, STATUS_H, SCREEN_W, OVERLAY_TOP - STATUS_H, C_BG);
  tft_.setTextDatum(ML_DATUM);
  tft_.setTextColor(C_TEXT, C_BG);
  tft_.drawString("Wi-Fi password", 6, 26, 2);
  char label[42];
  snprintf(label, sizeof label, "Network: %.25s", wifiSsid_);
  tft_.drawString(label, 6, 48, 1);
  tft_.setTextColor(C_WARN, C_BG);
  size_t n = strlen(wifiPassword_);
  char masked[sizeof wifiPassword_];
  memset(masked, '*', n);
  masked[n] = 0;
  snprintf(label, sizeof label, "Password: %s", masked);
  tft_.drawString(label, 6, 70, 1);
  tft_.setTextColor(C_TEXT, C_BG);
  tft_.drawString("Enter connects; KB cancels", 6, 91, 1);
  drawKeyboard();
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
  if (wifiScreen_ != WifiScreen::None) {
    if (wifiScreen_ == WifiScreen::Networks) {
      int count = link_.wifiNetworkCount();
      int scanState = (int)link_.wifiScanState();
      if (count != shownWifiCount_ || scanState != shownScanState_) {
        shownWifiCount_ = count;
        shownScanState_ = scanState;
        drawWifiScreen();
      }
    } else if (wifiScreen_ == WifiScreen::Connecting) {
      LinkState state = link_.state();
      if (state != LinkState::Connected) wifiSawDisconnect_ = true;
      if (wifiSawDisconnect_ && state == LinkState::Connected) {
        closeWifiScreen();
      } else if ((int)state != shownWifiLink_) {
        shownWifiLink_ = (int)state;
        drawWifiScreen();
      }
    }
    return;
  }
  if (now - lastDraw_ >= 33) {
    rend_.draw(term_, (now / 500) % 2 == 0 && term_.cursorVisible());
    lastDraw_ = now;
  }
}
