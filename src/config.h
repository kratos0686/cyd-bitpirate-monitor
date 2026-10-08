#pragma once
#include <stdint.h>

// Screen (landscape) - spec section 4
constexpr int SCREEN_W = 320, SCREEN_H = 240;
constexpr int CHAR_W = 6, CHAR_H = 8;
constexpr int COLS = 53, ROWS = 22;
constexpr int STATUS_H = 16;
constexpr int TERM_Y = STATUS_H;
constexpr int TERM_H = ROWS * CHAR_H;       // 176
constexpr int BTN_Y = TERM_Y + TERM_H;      // 192
constexpr int BTN_H = SCREEN_H - BTN_Y;     // 48

constexpr int SCROLLBACK_LINES = 400;       // 53 cols x 2 bytes x 400 = 42 KB
constexpr int RX_RING_BYTES = 4096;
constexpr int TX_RING_BYTES = 256;
constexpr uint32_t LONG_PRESS_MS = 600;

constexpr uint8_t BACKSPACE_BYTE = 0x7F;    // confirmed or changed in Task 2
#define ENTER_STR "\r"                      // confirmed or changed in Task 2
