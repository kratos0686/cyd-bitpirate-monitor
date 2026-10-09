#include <Arduino.h>
#include <TFT_eSPI.h>
#include "src/ansi.h"
#include "src/bytering.h"
#include "src/capture_data.h"
#include "src/config.h"
#include "src/render.h"
#include "src/replaylink.h"
#include "src/termbuf.h"

static TFT_eSPI tft;
static TermBuf term;
static AnsiParser ansi(term);
static ByteRing rx(RX_RING_BYTES);
static Renderer renderer(tft);
#ifdef REPLAY_LOOP
static ReplayLink s3link(rx, CAPTURE, CAPTURE_LEN, true);
#else
static ReplayLink s3link(rx, CAPTURE, CAPTURE_LEN, false);
#endif

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
  s3link.begin();
}

void loop() {
  static uint32_t lastDraw = 0, lastHeap = 0;
  s3link.loop();
  uint8_t buf[256]; size_t n;
  while ((n = rx.pop(buf, sizeof buf)) > 0) ansi.feed(buf, n);
  uint32_t now = millis();
  if (now - lastDraw >= 33) { renderer.draw(term, (now / 500) % 2 == 0); lastDraw = now; }
  if (now - lastHeap >= 10000) {
    Serial.printf("heap free=%u min=%u\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());
    lastHeap = now;
  }
}
