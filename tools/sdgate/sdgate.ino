// SD gate: proves the CYD's microSD slot works while the touch panel is polled and Wi-Fi is joined.
// Diagnostic only. Prints a status line every 2 s on the USB serial at 115200.
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <XPT2046_Touchscreen.h>
#include "../../src/secrets.h"

static SPIClass sdSpi(VSPI), touchSpi(VSPI);
static XPT2046_Touchscreen ts(33, 36);
static volatile uint32_t gPasses = 0, gErrors = 0, gMaxWriteMs = 0;
static volatile bool gMounted = false;
static uint32_t gHz = 0;

static uint8_t pat(uint32_t pass, uint32_t i) { return (uint8_t)(i * 7 + pass); }

static void sdTask(void*) {
  static uint8_t buf[4096];
  for (;;) {
    if (!gMounted) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
    uint32_t pass = gPasses;
    SD.remove("/gate.bin");
    File f = SD.open("/gate.bin", FILE_WRITE);
    if (!f) { gErrors = gErrors + 1; vTaskDelay(pdMS_TO_TICKS(500)); continue; }
    for (uint32_t c = 0; c < 64; c++) {                       // 64 x 4 KB = 256 KB per pass
      for (uint32_t i = 0; i < sizeof buf; i++) buf[i] = pat(pass, c * sizeof buf + i);
      uint32_t t0 = millis();
      if (f.write(buf, sizeof buf) != sizeof buf) gErrors = gErrors + 1;
      uint32_t dt = millis() - t0;
      if (dt > gMaxWriteMs) gMaxWriteMs = dt;
    }
    f.close();
    f = SD.open("/gate.bin", FILE_READ);
    for (uint32_t c = 0; f && c < 64; c++) {
      if (f.read(buf, sizeof buf) != sizeof buf) { gErrors = gErrors + 1; break; }
      for (uint32_t i = 0; i < sizeof buf; i++)
        if (buf[i] != pat(pass, c * sizeof buf + i)) { gErrors = gErrors + 1; break; }
    }
    if (f) f.close();
    gPasses = gPasses + 1;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  touchSpi.begin(25, 39, 32, 33);
  ts.begin(touchSpi);
  ts.setRotation(1);
  sdSpi.begin(18, 19, 23, 5);
  const uint32_t speeds[] = {20000000, 10000000, 4000000};
  for (uint32_t hz : speeds) {
    if (SD.begin(5, sdSpi, hz)) { gMounted = true; gHz = hz; break; }
    SD.end();
  }
  Serial.printf("sd mounted=%d hz=%lu type=%d size_mb=%llu\n", (int)gMounted, (unsigned long)gHz,
                (int)SD.cardType(), (unsigned long long)(SD.cardSize() / (1024 * 1024)));
  WiFi.mode(WIFI_STA);
  WiFi.begin(WS_SSID, WS_PASS);
  xTaskCreatePinnedToCore(sdTask, "sd", 6144, nullptr, 1, nullptr, 1);
}

void loop() {
  static uint32_t lastPrint = 0, polls = 0, touches = 0;
  static int lastX = 0, lastY = 0;
  polls++;
  if (ts.touched()) { TS_Point p = ts.getPoint(); touches++; lastX = p.x; lastY = p.y; }
  uint32_t now = millis();
  if (now - lastPrint >= 2000) {
    lastPrint = now;
    Serial.printf("t=%lu polls=%lu touches=%lu last=(%d,%d) sd: passes=%lu errors=%lu maxWriteMs=%lu wifi=%d heap=%u\n",
                  (unsigned long)now, (unsigned long)polls, (unsigned long)touches, lastX, lastY,
                  (unsigned long)gPasses, (unsigned long)gErrors, (unsigned long)gMaxWriteMs,
                  (int)WiFi.status(), ESP.getFreeHeap());
  }
  delay(10);
}
