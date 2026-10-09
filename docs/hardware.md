# CYD hardware notes

Board: 2.8" ESP32-32E CYD, 240x320 ILI9341-compatible panel, RST tied to EN.

Verified display pins: MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, RST none, backlight GPIO21 (HIGH = on).
Build uses `ILI9341_DRIVER`, HSPI port, 40 MHz, `LOAD_GLCD` + `LOAD_FONT2`.

## TFT_eSPI init workaround

With arduino-esp32 3.3.11 and TFT_eSPI 2.5.43 the panel stays plain white after a single `tft.init()`.
The bus set up inside that first `init()` never reaches the panel, even though the pins and macros resolve
correctly. Bisected in this order: a bit-banged ILI9341 init worked, plain Arduino hardware SPI worked,
TFT_eSPI's SPI object without `init()` worked, and the same object after `init()` did not.
Ending and restarting the SPI bus after the first `init()` fixes it:

    tft.init();
    tft.getSPIinstance().end();
    tft.getSPIinstance().begin(TFT_SCLK, TFT_MISO, TFT_MOSI, -1);
    tft.init();   // second init only repeats reset + register setup

Every sketch that initialises the display must do this. The root cause was not found.
