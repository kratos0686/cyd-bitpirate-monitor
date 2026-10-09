#include <Arduino.h>
#include <TFT_eSPI.h>
#include "src/config.h"

TFT_eSPI tft;

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  // Workaround (core 3.3.11 + TFT_eSPI 2.5.43): the SPI bus set up inside the first init() never
  // reaches the panel, so restart the bus and init again. See docs/hardware.md.
  tft.init();
  tft.getSPIinstance().end();
  tft.getSPIinstance().begin(TFT_SCLK, TFT_MISO, TFT_MOSI, -1);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 80, 40, TFT_RED);
  tft.fillRect(80, 0, 80, 40, TFT_GREEN);
  tft.fillRect(160, 0, 80, 40, TFT_BLUE);
  tft.fillRect(240, 0, 80, 40, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("CYD OK 320x240", 4, 48, 2);
  tft.drawRect(0, TERM_Y, COLS * CHAR_W, TERM_H, TFT_YELLOW);   // outline of the 53x22 terminal area
  tft.drawRect(0, BTN_Y, SCREEN_W, BTN_H, TFT_CYAN);            // outline of the button row
  Serial.println("display up");
}

void loop() {}
