# AGENTS.md

Guide for AI coding agents working in this repo. Read this first, then `README.md`.

## What this is

Arduino firmware for a Cheap Yellow Display (CYD, ESP32-32E 2.8", ILI9341 240x320, XPT2046
resistive touch) that acts as a handheld terminal for an ESP32 Bit Pirate (ESP32-S3 running the
ESP32 Bus Pirate firmware, prompt `HIZ>`). The CYD joins the S3's Wi-Fi hotspot and talks to its
web CLI over a WebSocket (`ws://192.168.4.1:80/ws`, text frames).

This is **Project 1 of 5: core terminal only.** Do not add hex view, SD logging, protocol
decoders, radio view, editable button config, copy/paste, themes or multiple sessions unless the
user explicitly starts a later project.

## Source of truth

| File | What it holds |
|---|---|
| `docs/superpowers/specs/2026-10-08-cyd-bitpirate-terminal-design.md` | Design spec: scope, architecture, UI, error handling, testing |
| `docs/superpowers/plans/2026-10-08-cyd-bitpirate-terminal.md` | Task-by-task implementation plan and global constraints |
| `docs/hardware.md` | Verified CYD pins and the required TFT_eSPI init workaround |
| `docs/protocol.md` | Measured S3 web CLI behavior (echo, Enter, Backspace, escape sequences) |

If hardware or a capture contradicts the spec, **stop and amend the spec/plan first**, then code.

## Toolchain

- `arduino-cli`, core `esp32:esp32` **3.3.11**, FQBN `esp32:esp32:esp32`. Arduino framework only
  (no PlatformIO, no ESP-IDF projects).
- Libraries (pinned): TFT_eSPI 2.5.43, XPT2046_Touchscreen 1.4, WebSockets 2.6.1.
- TFT_eSPI is configured entirely by `-D` flags in `tools/build.sh` (`USER_SETUP_LOADED=1`).
  Do not edit the library's `User_Setup.h`.

## Commands

```bash
tools/run-tests.sh                         # host unit tests (g++ + ASan/UBSan), no hardware
tools/build.sh                             # firmware build -> build/
PORT=/dev/ttyUSB0 tools/flash.sh           # flash the CYD (CH340, 115200)
EXTRA_FLAGS="-DREPLAY_MODE=1 -DREPLAY_LOOP=1" tools/build.sh   # replay capture, no Wi-Fi
EXTRA_FLAGS="-DSOAK_HELP=1" tools/build.sh                     # send `help` every 20 s (soak)
tools/commit.sh "feat: message"            # commit with the required Co-Authored-By trailer
```

Before a firmware build: `cp src/secrets.h.example src/secrets.h` and fill in `WS_PASS`.
`src/secrets.h` is git-ignored — never commit it or print the password.

Baseline: `tools/run-tests.sh` passes with **0 failures** (1508 checks at time of writing). Keep it
that way; run it after every change to a host-tested unit.

## Architecture

```
link (core 0 task) --rx ByteRing--> ansi --> termbuf --> render --> TFT
touch --> input --> gesture --> ui --> link.send() (tx ring, drained on core 0)
```

- `cyd-bitpirate-monitor.ino` wires everything; `loop()` runs on core 1, `linkTask` on core 0.
  The **only** cross-core connection is the lock-free SPSC `ByteRing`. Keep it that way.
- `Link` (`src/link.h`) is the transport interface. `WsLink` = real Wi-Fi/WebSocket;
  `ReplayLink` = plays `src/capture_data.h` (built with `REPLAY_MODE`). Transport changes stay
  inside a `Link` implementation.

### Host-tested units (no Arduino headers allowed)

`ansi`, `termbuf`, `bytering`, `prompt`, `calib`, `gesture`, `layout`, `keyboard`.
These must compile with plain g++. Do not include `Arduino.h`, `TFT_eSPI.h` or any ESP header in
them. A new pure-logic unit goes in `UNITS` in `tools/run-tests.sh` and gets a `test/test_<unit>.cpp`.

### Hardware-only units

`render`, `input` (XPT2046 on VSPI, CS 33 / IRQ 36, calibration in NVS), `ui`, `wslink`,
`replaylink`. Verify these on the device; describe what you could not test.

## Tests

- Framework is the tiny one in `test/check.h`: `SUITE(name) { CHECK(...); CHECK_EQ(a,b); CHECK_STR(a,b); }`.
  Suites self-register; `test/test_main.cpp` runs them all.
- `test/captures/s3_session.bin` is a real S3 USB session. `test_capture.cpp` replays it and the
  web CLI frame stream through the parser — add regression cases there for new protocol findings.
- Regenerate the replay header after a new capture:
  `tools/capture.py /dev/ttyACM0 test/captures/s3_session.bin help` then
  `tools/bin2h.py test/captures/s3_session.bin src/capture_data.h CAPTURE`.

Failure modes that must stay covered (from the plan's review focus): bare `\n` must not stair-step;
very long lines (30k chars) wrap without corrupting the ring; emoji/garbage/unterminated escapes
never crash or corrupt the grid; bursts larger than the rx ring drop and count the excess in order;
resistive-touch dropouts must not end a gesture or fire a false tap.

## Hard rules and gotchas

- **TFT init workaround is mandatory.** Every sketch that initialises the display must do
  `init()`, `getSPIinstance().end()`, `begin(TFT_SCLK, TFT_MISO, TFT_MOSI, -1)`, `init()` again.
  Without it the panel stays white. See `docs/hardware.md`.
- **GPIO0 (BOOT) is a strapping pin.** Recalibration is only checked in the 2 s window after
  start; do not read BOOT at power-up.
- **RAM is tight** (no PSRAM; ~165 KB free with Wi-Fi up, min 153 KB). Scrollback is
  400 lines x 53 cols x 2 bytes = 42 KB. Shrink `SCROLLBACK_LINES` before adding big buffers.
  Flash is ~85% used — watch binary size.
- **Protocol constants** (`config.h`): Backspace = `0x7F`, Enter = `"\r"`. S3 echoes by redrawing
  the line (`\rHIZ> ab\e[K`); output lines end in bare `\n`. No local echo.
- Keystrokes typed while the link is down are **dropped, not queued**; stale tx bytes are flushed
  on reconnect. `[link lost]` / `[link up]` / `[overflow]` markers are injected into the terminal.
- The S3 accepts one WebSocket client at a time; a new connection kicks the old one.
- Back up the CYD's flash (`esptool read_flash`) before flashing a board for the first time.
- Screen layout is fixed landscape 320x240: status 16 px, terminal 176 px (6x8 font,
  53x22), button row 48 px. Geometry lives in `config.h` and `layout.cpp`.

## Style

- C++17, Arduino style, small units with a header + `.cpp`. 2-space indent, `trailing_` members,
  `constexpr` constants in `config.h`. Keep comments short and explain *why*.
- Commits: conventional prefixes (`feat:`, `fix:`, `test:`, `docs:`, `chore:`), via `tools/commit.sh`.
- Update `docs/protocol.md` / `docs/hardware.md` when you measure something new on hardware, and
  the spec when behavior changes.
