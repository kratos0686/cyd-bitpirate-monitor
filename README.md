# cyd-bitpirate-monitor

Handheld terminal for the [ESP32 Bit Pirate](https://github.com/geo-tp/ESP32-Bus-Pirate) on a
Cheap Yellow Display (CYD, ESP32-32E 2.8", 240x320, landscape). The CYD joins the Bit Pirate's
Wi-Fi hotspot and talks to its web CLI over a WebSocket, so you get the `HIZ>` prompt, command
buttons and an on-screen keyboard without a laptop.

Project 1 of 5: the core terminal only (no hex view, logging or protocol decoders yet).

## Features
- Auto-join and reconnect to the S3 hotspot (`[link lost]` / `[link up]` markers, scrollback kept)
- Minimal ANSI terminal: 400-line scrollback, line editing as the S3 redraws it
- Touch UI: 10 command buttons (`mode help scan read write sniff config ^C KB Enter`), QWERTY
  keyboard, drag to scroll, tap the status bar to pause, long-press to clear
- Status bar shows link state and the current S3 mode parsed from the prompt
- Touch calibration saved to flash; hold BOOT during the 2 s "Hold BOOT to recalibrate" prompt
  after start to redo it (BOOT held at power-up enters download mode instead)

## Build and flash
Needs `arduino-cli` with core `esp32:esp32` 3.3.11 and libraries TFT_eSPI 2.5.43,
XPT2046_Touchscreen 1.4, WebSockets 2.6.1.

```
cp src/secrets.h.example src/secrets.h   # fill in WS_PASS with your S3 hotspot password
tools/build.sh
PORT=/dev/ttyUSB0 tools/flash.sh
tools/run-tests.sh                        # host unit tests, no hardware needed
```

`src/secrets.h` is git-ignored. Display pins and a required TFT_eSPI init workaround are in
`docs/hardware.md`. The S3 side (hotspot from the boot menu, WebSocket `/ws`, echo behavior) is in
`docs/protocol.md`.

Debug builds: `EXTRA_FLAGS="-DREPLAY_MODE=1 -DREPLAY_LOOP=1" tools/build.sh` replays a captured S3
session with no Wi-Fi; `-DSOAK_HELP=1` sends `help` every 20 s for soak testing.

## Measured
About 165 KB free heap with Wi-Fi up (minimum 153 KB over a 10-minute soak). Flash is 85% used.
