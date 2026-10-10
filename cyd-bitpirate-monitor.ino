#include <Arduino.h>
#include <TFT_eSPI.h>
#include <string.h>
#include "src/ansi.h"
#include "src/bytering.h"
#ifdef REPLAY_MODE
#include "src/capture_data.h"
#include "src/replaylink.h"
#else
#include "src/wslink.h"
#endif
#include "src/config.h"
#include "src/gesture.h"
#include "src/input.h"
#include "src/render.h"
#include "src/termbuf.h"
#include "src/ui.h"

static TFT_eSPI tft;
static TermBuf term;
static AnsiParser ansi(term);
static ByteRing rx(RX_RING_BYTES);
#ifdef REPLAY_MODE
#ifdef REPLAY_LOOP
static ReplayLink s3link(rx, CAPTURE, CAPTURE_LEN, true);
#else
static ReplayLink s3link(rx, CAPTURE, CAPTURE_LEN, false);
#endif
#else
static WsLink s3link(rx);
#endif
static Renderer renderer(tft);
static TouchInput touch;
static GestureRecognizer gestures;
static Ui ui(tft, term, renderer, s3link);

static void feedText(const char* s) { ansi.feed((const uint8_t*)s, strlen(s)); }

static void linkTask(void*) {
  for (;;) { s3link.loop(); vTaskDelay(1); }
}

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
  // BOOT (GPIO0) is a strapping pin: held at power-up it enters download mode.
  // So offer a 2 s window after start where holding it forces recalibration.
  pinMode(0, INPUT_PULLUP);
  bool recal = !touch.hasCalibration();
  if (!recal) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Hold BOOT to recalibrate", 60, 112, 2);
    for (uint32_t t0 = millis(); millis() - t0 < 2000 && !recal;) recal = digitalRead(0) == LOW;
    tft.fillScreen(TFT_BLACK);
  }
  if (recal) touch.runCalibration(tft);
  ui.begin();
  s3link.begin();
  xTaskCreatePinnedToCore(linkTask, "link", 8192, nullptr, 1, nullptr, 0);
}

void loop() {
  static LinkState prev = LinkState::Searching;
  static uint32_t lastHeap = 0;
  uint32_t now = millis();

  LinkState st = s3link.state();
  if (st != prev) {
    if (st == LinkState::Lost) feedText("\r\n[link lost]\r\n");
    else if (st == LinkState::Connected) feedText("\r\n[link up]\r\n");
    prev = st;
  }

  uint8_t buf[256]; size_t n;
  while ((n = rx.pop(buf, sizeof buf)) > 0) ansi.feed(buf, n);
  size_t dropped = rx.takeDropped();
  if (dropped) feedText("\r\n[overflow]\r\n");

  TouchSample s = touch.read();
  ui.handle(gestures.update(s.pressed, s.x, s.y, now));
  ui.update(now);

  if (now - lastHeap >= 10000) {
    Serial.printf("heap free=%u min=%u\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());
    lastHeap = now;
  }
  delay(1);
}
