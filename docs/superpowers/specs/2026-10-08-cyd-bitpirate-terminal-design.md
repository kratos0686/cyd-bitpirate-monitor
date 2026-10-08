# CYD Bit Pirate Terminal — Design (Project 1: Core Terminal)

Date: 2026-10-08
Status: Draft, awaiting review

## 1. Purpose and scope

Firmware for the Cheap Yellow Display (CYD) that works as a handheld terminal for an
ESP32 Bit Pirate (ESP32-S3 running the community ESP32 Bus Pirate firmware, v1.7,
prompt `HIZ>`). The CYD joins the S3's Wi-Fi hotspot and talks to its web CLI.

This is project 1 of a larger decomposition. It delivers a usable terminal and nothing
more.

In scope:
- Wi-Fi link to the S3 hotspot with automatic reconnect
- Minimal ANSI parsing, character grid, scrollback
- Touch UI: terminal view, on-screen keyboard, command buttons, status bar
- Touch calibration stored in flash

Out of scope (later projects): hex/ASCII view, timestamps, SD logging, protocol
decoders (I2C, SPI, UART, 1-Wire, JTAG/SWD, CAN, I2S, LED, DIO), sub-GHz radio view,
editable button config, copy/paste, themes, multiple sessions.

## 2. Hardware (verified)

CYD: 2.8" ESP32-32E, 240x320, resistive touch (XPT2046), CH340C USB, ESP32-D0WD-V3
rev 3.1, 40 MHz crystal, no PSRAM assumed. Has UART header (5V/GND/TXD/RXD), SPI
header, microSD, speaker, BAT, RGB LED.

S3: ESP32-S3-N16R8 dev board, USB-C CLI on `ttyACM0` at 115200 8N1; USB descriptor
"ESP32 Bit Pirate" (303a:1002).

Finding: the firmware documents only USB serial and the Wi-Fi web CLI as CLI
transports. UART pins are not documented as a CLI transport, and the CYD cannot be a
USB host. The UART header wires are therefore not used by project 1.

## 3. Architecture

| Unit | Job | Depends on |
|---|---|---|
| `link` | Join hotspot, open web CLI connection, reconnect on drop. `send(bytes)`, received-bytes queue. | Wi-Fi |
| `ansi` | Byte stream to terminal operations: print char, backspace, erase to end of line, newline, a few color codes. Unknown sequences ignored. | none |
| `termbuf` | Character grid and scrollback ring buffer; applies terminal operations. | none |
| `render` | Draws grid and scrollback view; redraws only changed cells. | `termbuf`, TFT_eSPI |
| `input` | Reads XPT2046; emits tap, scroll, button events. | touch driver |
| `ui` | Layout, keyboard, command buttons, status bar; turns events into bytes for `link`. | `input`, `render`, `link` |

Data flow: `link` rx -> `ansi` -> `termbuf` -> `render`. Touch -> `input` -> `ui` ->
bytes -> `link` tx.

Concurrency: `link` on one core; `ansi`, `termbuf`, `render`, `input`, `ui` on the
other. A queue is the only connection between them.

`ansi` and `termbuf` have no Arduino dependencies so they can be unit tested on the host.

## 4. UI (landscape, 320x240)

- Status bar (16 px): Wi-Fi state, current S3 mode parsed from the prompt (`HIZ`, `I2C`, ...), pause indicator.
- Terminal area (~176 px): 6x8 font, about 53 columns x 22 rows. Sticks to bottom unless scrolled or paused.
- Button row (48 px, two rows of five): `mode`, `help`, `scan`, `read`, `write`, `sniff`, `config`, `^C`, `KB`, `Enter`. Long press opens a short list of related commands. Fixed defaults in project 1.
- `KB` button slides a QWERTY keyboard (plus symbols page, Backspace, Enter) over the button row and half the terminal. Keys are sent as typed.
- Gestures: drag to scroll, tap status bar to pause/resume (output keeps buffering), long-press terminal to clear.
- Scrollback: 400 lines in RAM (`SCROLLBACK_LINES`). About 2000 lines does not fit next to Wi-Fi on a plain ESP32: 53 columns x 2 bytes x 400 lines = 42 KB.
- Touch calibration screen at first boot; result saved to flash; hold BOOT at power-up to redo.

## 5. Error handling

| Situation | Behavior |
|---|---|
| Hotspot not found at boot | Status "searching"; retry every few seconds indefinitely. |
| Link drops mid-session | Status red "link lost"; scrollback kept; `[link lost]` marker line; auto-reconnect. Keys typed while disconnected are dropped, not queued. |
| Malformed/unknown ANSI | Ignored; no crash or grid corruption. |
| Receive queue full | Newly arriving bytes are dropped (a lock-free queue cannot discard from the consumer side); `[overflow]` marker; UI stays responsive. |
| Touch not calibrated | Calibration screen; BOOT at power-up redoes it. |
| Flash storage unavailable | Default calibration; keep running. |

## 6. Testing

- Host unit tests (g++) for `ansi` and `termbuf`, fed with real captured Bit Pirate sessions, including the `ESC[K` and backspace line editing observed on the S3.
- Replay harness: `link` swapped for a file-playback source so the display path runs on the CYD without the S3 or Wi-Fi; used to tune rendering speed.
- Hardware checklist (CYD + S3):
  1. Joins the S3 hotspot.
  2. Banner and `HIZ>` prompt render correctly.
  3. Typing `help` via the on-screen keyboard works.
  4. Command buttons send the right text.
  5. Scroll and pause behave.
  6. Power-cycling the S3 recovers on its own.
- Done = checklist passes and a 10-minute soak shows no crash or visible corruption; free heap is measured during the soak. The soak is the replay harness at full speed plus the real link idle with periodic `help` dumps (a continuously printing `sniff` source is not available without extra wiring).

## 7. Build and flashing

- Location: `~/cyd-bitpirate-monitor/` (git repository).
- `arduino-cli`, core `esp32:esp32` 3.3.11, board `esp32:esp32:esp32`.
- New libraries: TFT_eSPI (configured for the CYD's ILI9341 and pins), an XPT2046 touch library.
- Flash via USB-C (CH340, `/dev/ttyUSB0`). Before the first flash, back up the current flash with `esptool read_flash` and keep the file so the original firmware can be restored.

Layout:
```
cyd-bitpirate-monitor/
  cyd-bitpirate-monitor.ino
  src/link.h replaylink.* wslink.* ansi.* termbuf.* prompt.* calib.* gesture.* layout.* keyboard.* bytering.* render.* input.* ui.*
  test/        host unit tests + captured sessions
  tools/       replay harness, build/flash scripts
  docs/superpowers/specs/
```

## 8. Risks and open items

1. Web CLI wire protocol is undocumented (HTTP vs WebSocket, port, framing). First plan task: capture it from the S3. `link` hides the details, so only that unit changes.
2. RAM: Wi-Fi plus scrollback on a plain ESP32 is tight. Scrollback size is a constant to shrink if needed.
3. Resistive touch accuracy: handled by calibration.
4. The S3 hotspot may need enabling by hand (`mode wifi`, then `connect`, per the wiki). The plan will include exact steps.
5. Landscape orientation is used (the user approved the spec without asking for portrait).
6. The Wi-Fi hotspot name/password and whether it is enabled are not yet verified on the actual S3.
