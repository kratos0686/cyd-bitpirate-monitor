#include <Arduino.h>
#include <TFT_eSPI.h>
#include "src/input.h"

static TFT_eSPI tft;
static TouchInput touch;

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  // Init workaround, see docs/hardware.md
  tft.init();
  tft.getSPIinstance().end();
  tft.getSPIinstance().begin(TFT_SCLK, TFT_MISO, TFT_MOSI, -1);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  touch.begin();
  pinMode(0, INPUT_PULLUP);                           // BOOT button held at power-up = recalibrate
  if (!touch.hasCalibration() || digitalRead(0) == LOW) touch.runCalibration(tft);
  tft.drawCentreString("Touch anywhere", 160, 110, 2);
}

void loop() {
  TouchSample s = touch.read();
  if (s.pressed) {
    tft.fillCircle(s.x, s.y, 3, TFT_GREEN);
    Serial.printf("touch %d,%d\n", s.x, s.y);
  }
  delay(5);
}
