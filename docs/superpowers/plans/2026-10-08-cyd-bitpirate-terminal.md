# CYD Bit Pirate Terminal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Firmware for the CYD (ESP32-32E, 2.8" 240x320 resistive touch) that joins the ESP32 Bit Pirate's Wi-Fi hotspot and works as a handheld touch terminal for its web CLI.

**Architecture:** Six small units (`link`, `ansi`, `termbuf`, `render`, `input`, `ui`) joined by byte queues. `ansi`, `termbuf`, and the pure helpers (`bytering`, `prompt`, `calib`, `gesture`, `layout`, `keyboard`) have no Arduino dependencies and are unit-tested on the desktop with g++. Hardware units are verified on the CYD, first against a replay of captured S3 output (no Wi-Fi), then against the real S3.

**Tech Stack:** Arduino framework via `arduino-cli` (core `esp32:esp32` 3.3.11), TFT_eSPI, XPT2046_Touchscreen, WebSockets (Links2004), g++ for host tests, Python 3 + pyserial for capture tools.

**Spec:** `docs/superpowers/specs/2026-10-08-cyd-bitpirate-terminal-design.md`

## Global Constraints

- Screen is landscape 320x240: status bar 16 px, terminal 176 px (6x8 font, 53 cols x 22 rows), button row 48 px.
- Board `esp32:esp32:esp32`, core `esp32:esp32` 3.3.11, Arduino framework only (no PlatformIO, no ESP-IDF).
- Display/touch libraries: TFT_eSPI 2.5.43, XPT2046_Touchscreen 1.4, WebSockets 2.6.1.
- CYD USB serial is the CH340 at 115200; the S3's CLI is native USB `/dev/ttyACM0` at 115200 8N1.
- **Back up the CYD's current flash (Task 1) before the first flash of any firmware.**
- Project 1 is the terminal only. No hex view, SD logging, decoders, radio view, config files, copy/paste, themes.
- Every commit uses `tools/commit.sh "<message>"` (adds the required `Co-Authored-By` trailer).
- `ansi`, `termbuf`, `bytering`, `prompt`, `calib`, `gesture`, `layout`, `keyboard` must not include any Arduino header, so `tools/run-tests.sh` compiles them with plain g++.
- Wi-Fi credentials and host values live in `src/secrets.h`, which is gitignored.
- If a capture or hardware check contradicts the spec (for example the web CLI is not a WebSocket), stop and amend the spec and plan before continuing.

## Review Focus

Failure modes the spec implies but its tasks would not otherwise exercise, most likely first. Each has a test in the task named.

1. **Output with bare `\n` (no `\r`)** must not stair-step across the screen; a line feed returns the cursor to column 0 (Task 5).
2. **A very long line with no newline** (5,000 and 30,000 characters) wraps cleanly and the ring wrapping does not corrupt the visible rows (Task 5).
3. **Non-ASCII and garbage bytes** (the menus contain emoji; random bytes; unterminated escape sequences) never crash the parser or corrupt the grid (Task 4).
4. **A burst bigger than the receive queue** drops the excess, counts it, and the consumer still gets every accepted byte in order (Task 3).
5. **Resistive-touch dropouts** (a few milliseconds of "not touched" in the middle of a press or drag) must not end the gesture or fire a false tap (Task 7).

## File Structure

```
cyd-bitpirate-monitor/
  cyd-bitpirate-monitor.ino      entry point; wires units together (Tasks 9-14)
  src/config.h                   screen/layout/queue constants
  src/bytering.{h,cpp}           lock-free single-producer/single-consumer byte queue (host-tested)
  src/ansi.{h,cpp}               byte stream -> TermSink operations (host-tested)
  src/termbuf.{h,cpp}            cell grid + scrollback ring, implements TermSink (host-tested)
  src/prompt.{h,cpp}             parse "HIZ> " prompt -> mode name (host-tested)
  src/calib.{h,cpp}              raw touch -> screen mapping (host-tested)
  src/gesture.{h,cpp}            tap / long-press / scroll recognizer with dropout debounce (host-tested)
  src/layout.{h,cpp}             command-button table, geometry, hit tests (host-tested)
  src/keyboard.{h,cpp}           on-screen keyboard model and hit test (host-tested)
  src/link.h                     Link interface + LinkState
  src/replaylink.{h,cpp}         plays a capture into the rx queue, echoes typed bytes
  src/wslink.{h,cpp}             Wi-Fi + WebSocket link to the S3
  src/render.{h,cpp}             draws TermBuf to the TFT, redraws only changed cells
  src/input.{h,cpp}              XPT2046 touch, calibration screen, NVS storage
  src/ui.{h,cpp}                 status bar, buttons, keyboard, popup, gesture handling
  src/capture_data.h             generated from test/captures/s3_session.bin
  src/secrets.h(.example)        Wi-Fi / host constants (secrets.h gitignored)
  test/*.cpp                     host unit tests, test/captures/*.bin captured S3 output
  tools/                         build.sh flash.sh run-tests.sh commit.sh capture.py bin2h.py
  docs/protocol.md               web CLI protocol findings (Task 13)
```

---

### Task 1: Environment, scripts, spec amendments, flash backup

**Files:**
- Create: `.gitignore`, `tools/commit.sh`, `tools/run-tests.sh`, `tools/build.sh`, `tools/flash.sh`, `test/check.h`, `test/test_main.cpp`, `src/config.h`
- Modify: `docs/superpowers/specs/2026-10-08-cyd-bitpirate-terminal-design.md`

**Interfaces:**
- Produces: `tools/run-tests.sh` (builds and runs all host tests), `tools/build.sh` (compiles the sketch to `build/`; env `EXTRA_FLAGS`, `DISPLAY_DRIVER`), `tools/flash.sh` (uploads `build/`; env `PORT`), `tools/commit.sh "msg"`, test macros `CHECK`, `CHECK_EQ`, `CHECK_STR`, `SUITE(name)`, constants in `src/config.h`.

- [ ] **Step 1: Install libraries**

```bash
arduino-cli lib install "TFT_eSPI@2.5.43" "XPT2046_Touchscreen@1.4" "WebSockets@2.6.1"
arduino-cli lib list | grep -E "TFT_eSPI|XPT2046|WebSockets"
```
Expected: three lines with versions 2.5.43, 1.4, 2.6.1. If a version is rejected, run `arduino-cli lib search <name>` and use the closest available version, then record it in this plan's Global Constraints.

- [ ] **Step 2: Create `.gitignore`**

```
build/
src/secrets.h
*.o
```

- [ ] **Step 3: Create `tools/commit.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
git add -A
git commit -m "$1" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 4: Create `tools/run-tests.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
UNITS="ansi termbuf prompt calib keyboard layout gesture bytering"
SRCS=""
for u in $UNITS; do [ -f "src/$u.cpp" ] && SRCS="$SRCS src/$u.cpp"; done
mkdir -p build
g++ -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined -pthread -Isrc -Itest test/*.cpp $SRCS -o build/host-tests
./build/host-tests
```

- [ ] **Step 5: Create `test/check.h` and `test/test_main.cpp`**

`test/check.h`:
```cpp
#pragma once
#include <cstdio>
#include <string>
#include <vector>

extern int g_checks, g_failures;

#define CHECK(c) do { g_checks++; if (!(c)) { g_failures++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { g_checks++; long long _a = (long long)(a), _b = (long long)(b); \
  if (_a != _b) { g_failures++; printf("FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { g_checks++; std::string _a = (a), _b = (b); \
  if (_a != _b) { g_failures++; printf("FAIL %s:%d  %s == %s\n   got:  [%s]\n   want: [%s]\n", __FILE__, __LINE__, #a, #b, _a.c_str(), _b.c_str()); } } while (0)

inline std::vector<void (*)()>& suites() { static std::vector<void (*)()> v; return v; }
struct SuiteReg { explicit SuiteReg(void (*f)()) { suites().push_back(f); } };
#define SUITE(fn) static void fn(); static SuiteReg reg_##fn(fn); static void fn()
```

`test/test_main.cpp`:
```cpp
#include "check.h"
int g_checks = 0, g_failures = 0;
int main() {
  for (auto f : suites()) f();
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
```

- [ ] **Step 6: Create `src/config.h`**

```cpp
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
```

- [ ] **Step 7: Create `tools/build.sh` and `tools/flash.sh`**

`tools/build.sh`:
```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
FQBN=esp32:esp32:esp32
DRIVER="${DISPLAY_DRIVER:-ILI9341_2_DRIVER}"
FLAGS="-DUSER_SETUP_LOADED=1 -D${DRIVER}=1 -DTFT_WIDTH=240 -DTFT_HEIGHT=320 \
-DTFT_MISO=12 -DTFT_MOSI=13 -DTFT_SCLK=14 -DTFT_CS=15 -DTFT_DC=2 -DTFT_RST=-1 \
-DTFT_BL=21 -DTFT_BACKLIGHT_ON=HIGH -DUSE_HSPI_PORT=1 -DLOAD_GLCD=1 -DLOAD_FONT2=1 \
-DSPI_FREQUENCY=40000000 ${EXTRA_FLAGS:-}"
arduino-cli compile --fqbn "$FQBN" --output-dir build --build-property "build.extra_flags=${FLAGS}" .
```

`tools/flash.sh`:
```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
arduino-cli upload -p "${PORT:-/dev/ttyUSB0}" --fqbn esp32:esp32:esp32 --input-dir build
```

Then `chmod +x tools/*.sh`.

- [ ] **Step 8: Amend the spec** (apply each replacement exactly in `docs/superpowers/specs/2026-10-08-cyd-bitpirate-terminal-design.md`)

1. Replace `- Scrollback: about 2000 lines in RAM (constant, can be reduced).` with
   `- Scrollback: 400 lines in RAM (`SCROLLBACK_LINES`). About 2000 lines does not fit next to Wi-Fi on a plain ESP32: 53 columns x 2 bytes x 400 lines = 42 KB.`
2. Replace ``- Button row (48 px, two rows): `mode`, `help`, `scan`, `read`, `write`, `sniff`, `config`, `Enter`.`` with
   ``- Button row (48 px, two rows of five): `mode`, `help`, `scan`, `read`, `write`, `sniff`, `config`, `^C`, `KB`, `Enter`.``
3. Replace `| Receive queue full | Oldest bytes dropped; `[overflow]` marker; UI stays responsive. |` with
   `| Receive queue full | Newly arriving bytes are dropped (a lock-free queue cannot discard from the consumer side); `[overflow]` marker; UI stays responsive. |`
4. Replace ``- Done = checklist passes and a 10-minute `sniff` soak shows no crash or visible corruption; free heap is measured during the soak.`` with
   ``- Done = checklist passes and a 10-minute soak shows no crash or visible corruption; free heap is measured during the soak. The soak is the replay harness at full speed plus the real link idle with periodic `help` dumps (a continuously printing `sniff` source is not available without extra wiring).``
5. Replace the line `  src/link.*  ansi.*  termbuf.*  render.*  input.*  ui.*` with
   `  src/link.h replaylink.* wslink.* ansi.* termbuf.* prompt.* calib.* gesture.* layout.* keyboard.* bytering.* render.* input.* ui.*`
6. Replace ``## 8. Risks and open items`` list item 5 text `Landscape orientation is assumed; portrait is a possible alternative (not yet confirmed by the user).` with `Landscape orientation is used (the user approved the spec without asking for portrait).`

- [ ] **Step 9: Run the (empty) host test build**

Run: `tools/run-tests.sh`
Expected: `0 checks, 0 failures`

- [ ] **Step 10: Back up the CYD flash**

```bash
ls /dev/ttyUSB*                       # the CYD (CH340); replug it if absent
mkdir -p ~/cyd-backup
esptool --port /dev/ttyUSB0 flash_id | grep -E "Detected flash size|Flash size"
esptool --port /dev/ttyUSB0 read_flash 0 0x400000 ~/cyd-backup/cyd-original-4MB.bin
esptool --port /dev/ttyUSB0 read_flash 0 0x400000 ~/cyd-backup/cyd-original-4MB-verify.bin
sha256sum ~/cyd-backup/cyd-original-4MB*.bin
```
Expected: flash size 4MB; the two files are 4194304 bytes and have identical sha256. If the size is not 4MB, change `0x400000` to the reported size and read again. Keep `~/cyd-backup/`; to restore later: `esptool --port /dev/ttyUSB0 write_flash 0 ~/cyd-backup/cyd-original-4MB.bin`.

- [ ] **Step 11: Commit**

```bash
tools/commit.sh "chore: scripts, test harness, config, spec amendments"
```

---

### Task 2: Capture S3 sessions and confirm Enter/Backspace/echo

**Files:**
- Create: `tools/capture.py`, `test/captures/s3_session.bin`, `docs/protocol.md`

**Interfaces:**
- Produces: `test/captures/s3_session.bin` (raw S3 USB-CLI bytes: prompt + full `help` output) used by Tasks 5 and 10; confirmed `BACKSPACE_BYTE` / `ENTER_STR` values in `src/config.h`; the first section of `docs/protocol.md`.

- [ ] **Step 1: Create `tools/capture.py`**

```python
#!/usr/bin/env python3
"""Usage: capture.py PORT OUT.bin [command ...]   (an empty-string command sends just Enter)"""
import sys, time, serial

port, out = sys.argv[1], sys.argv[2]
cmds = sys.argv[3:]
s = serial.Serial(port, 115200, timeout=0.2)
data = bytearray()

def drain(sec):
    end = time.time() + sec
    while time.time() < end:
        data.extend(s.read(4096))

s.reset_input_buffer()
s.write(b"\r\n"); drain(1.5)
for c in cmds:
    s.write(c.encode() + b"\r"); drain(2.5)
open(out, "wb").write(data)
print(f"{len(data)} bytes -> {out}")
```

- [ ] **Step 2: Capture a session** (S3 plugged in at `/dev/ttyACM0`, sitting at the `HIZ>` prompt)

```bash
mkdir -p test/captures
python3 -I tools/capture.py /dev/ttyACM0 test/captures/s3_session.bin "help" ""
```
Expected: roughly 4000+ bytes. (`help` opens a menu; the empty command accepts the default "ALL" entry, which prints every command.)

- [ ] **Step 3: Inspect the capture**

```bash
python3 -I - <<'E'
import re
d = open("test/captures/s3_session.bin","rb").read()
print("bytes:", len(d))
print("bare LF (no CR before):", len(re.findall(rb"(?<!\r)\n", d)))
print("escape sequences:", sorted(set(re.findall(rb"\x1b\[[0-9;?]*[A-Za-z]", d))))
print("non-ASCII bytes:", sum(1 for b in d if b >= 0x80))
print("has 'Commands by Mode':", b"Commands by Mode" in d)
E
```
Expected: `has 'Commands by Mode': True`. Write the other three numbers/lists into `docs/protocol.md` under "USB capture notes". If the escape list contains sequences other than `ESC[K`, `ESC[...m`, `ESC[nD`, `ESC[nC`, `ESC[nG`, `ESC[2J`, add them to the Task 4 tests and the parser.

- [ ] **Step 4: Confirm the Backspace byte and Enter**

```bash
python3 -I - <<'E'
import serial, time, re
s = serial.Serial("/dev/ttyACM0", 115200, timeout=0.2)
def run(bs):
    s.reset_input_buffer(); s.write(b"\r"); time.sleep(0.5); s.reset_input_buffer()
    s.write(b"ab"); time.sleep(0.3); s.write(bytes([bs])); time.sleep(0.5)
    out = s.read(4096)
    s.write(b"\x03"); time.sleep(0.2); s.write(b"\r"); time.sleep(0.5); s.read(4096)
    return re.sub(rb"\x1b\[[0-9;]*[A-Za-z]", b"", out).decode("utf-8","replace")
for bs in (0x7f, 0x08):
    print(hex(bs), repr(run(bs))[-60:])
E
```
Expected: for the correct Backspace byte the last redraw of the line ends with `HIZ> a` (not `HIZ> ab`). Set `BACKSPACE_BYTE` in `src/config.h` to the byte that works (leave `0x7F` if both do). Enter was already shown to work as `\r`; leave `ENTER_STR "\r"`.

- [ ] **Step 5: Create `docs/protocol.md`**

```markdown
# Bit Pirate CLI protocol notes

## USB capture notes (Task 2)
- Capture file: test/captures/s3_session.bin
- Bare LF count: <value from Step 3>
- Escape sequences seen: <list from Step 3>
- Non-ASCII byte count: <value from Step 3>
- Backspace byte that edits the line: <0x7F or 0x08>
- Enter sent as: \r

## Web CLI over Wi-Fi (Task 13)
(filled in by Task 13)
```
Replace each `<...>` with the measured value.

- [ ] **Step 6: Commit**

```bash
tools/commit.sh "feat: capture S3 USB session, confirm Enter/Backspace"
```

---

### Task 3: ByteRing (lock-free SPSC queue)

**Files:**
- Create: `src/bytering.h`, `src/bytering.cpp`, `test/test_bytering.cpp`

**Interfaces:**
- Produces: `class ByteRing { explicit ByteRing(size_t cap); size_t push(const uint8_t*, size_t); size_t pop(uint8_t*, size_t); size_t size() const; size_t takeDropped(); }`. One thread calls `push`, one calls `pop`.

- [ ] **Step 1: Write the failing test** `test/test_bytering.cpp`

```cpp
#include "check.h"
#include "bytering.h"
#include <thread>
#include <vector>

SUITE(bytering_tests) {
  {  // basic push/pop
    ByteRing r(8);
    const uint8_t in[3] = {1, 2, 3};
    CHECK_EQ(r.push(in, 3), 3);
    CHECK_EQ(r.size(), 3);
    uint8_t out[8];
    CHECK_EQ(r.pop(out, 8), 3);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3);
    CHECK_EQ(r.pop(out, 8), 0);
  }
  {  // full: excess is dropped and counted (Review Focus 4)
    ByteRing r(8);
    uint8_t in[12];
    for (int i = 0; i < 12; i++) in[i] = (uint8_t)i;
    CHECK_EQ(r.push(in, 12), 8);
    CHECK_EQ(r.takeDropped(), 4);
    CHECK_EQ(r.takeDropped(), 0);
    uint8_t out[16];
    CHECK_EQ(r.pop(out, 16), 8);
    for (int i = 0; i < 8; i++) CHECK_EQ(out[i], i);   // the accepted bytes are the first 8, in order
  }
  {  // wrap-around
    ByteRing r(8);
    uint8_t in[6] = {1, 2, 3, 4, 5, 6}, out[8];
    r.push(in, 6); r.pop(out, 4);
    uint8_t in2[5] = {7, 8, 9, 10, 11};
    CHECK_EQ(r.push(in2, 5), 5);
    CHECK_EQ(r.pop(out, 8), 7);
    const uint8_t want[7] = {5, 6, 7, 8, 9, 10, 11};
    for (int i = 0; i < 7; i++) CHECK_EQ(out[i], want[i]);
  }
  {  // two threads, 2 MB through a 64-byte ring, order preserved
    ByteRing r(64);
    const size_t TOTAL = 2000000;
    std::thread prod([&] {
      size_t sent = 0; uint8_t buf[50];
      while (sent < TOTAL) {
        size_t n = 1 + (sent % 50);
        if (n > TOTAL - sent) n = TOTAL - sent;
        for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)((sent + i) & 0xFF);
        size_t off = 0;
        while (off < n) {            // retry the part that did not fit
          size_t acc = r.push(buf + off, n - off);
          off += acc;
          if (off < n) std::this_thread::yield();
        }
        sent += n;
      }
    });
    size_t got = 0; bool ok = true; uint8_t out[40];
    while (got < TOTAL) {
      size_t n = r.pop(out, sizeof out);
      for (size_t i = 0; i < n; i++) if (out[i] != (uint8_t)((got + i) & 0xFF)) ok = false;
      got += n;
      if (n == 0) std::this_thread::yield();
    }
    prod.join();
    CHECK(ok);
    CHECK_EQ(got, TOTAL);
  }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `tools/run-tests.sh`
Expected: FAIL at compile time: `bytering.h: No such file or directory` (and `src/bytering.cpp` not found is silently skipped).

- [ ] **Step 3: Implement** `src/bytering.h`

```cpp
#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>

// Single-producer / single-consumer byte queue. One task calls push(), another calls pop().
// Bytes that do not fit are dropped and counted.
class ByteRing {
public:
  explicit ByteRing(size_t cap) : cap_(cap), buf_(new uint8_t[cap]) {}
  ~ByteRing() { delete[] buf_; }
  ByteRing(const ByteRing&) = delete;
  ByteRing& operator=(const ByteRing&) = delete;

  size_t push(const uint8_t* d, size_t n);      // returns bytes accepted
  size_t pop(uint8_t* out, size_t max);         // returns bytes read
  size_t size() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }
  size_t takeDropped() { return dropped_.exchange(0); }

private:
  const size_t cap_;
  uint8_t* const buf_;
  std::atomic<size_t> head_{0}, tail_{0}, dropped_{0};   // head/tail are monotonic counters
};
```

`src/bytering.cpp`:
```cpp
#include "bytering.h"

size_t ByteRing::push(const uint8_t* d, size_t n) {
  size_t head = head_.load(std::memory_order_relaxed);
  size_t tail = tail_.load(std::memory_order_acquire);
  size_t freeB = cap_ - (head - tail);
  size_t acc = n < freeB ? n : freeB;
  for (size_t i = 0; i < acc; i++) buf_[(head + i) % cap_] = d[i];
  head_.store(head + acc, std::memory_order_release);
  if (acc < n) dropped_.fetch_add(n - acc);
  return acc;
}

size_t ByteRing::pop(uint8_t* out, size_t max) {
  size_t tail = tail_.load(std::memory_order_relaxed);
  size_t head = head_.load(std::memory_order_acquire);
  size_t avail = head - tail;
  size_t n = avail < max ? avail : max;
  for (size_t i = 0; i < n; i++) out[i] = buf_[(tail + i) % cap_];
  tail_.store(tail + n, std::memory_order_release);
  return n;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `tools/run-tests.sh`
Expected: `... checks, 0 failures`

- [ ] **Step 5: Commit**

```bash
tools/commit.sh "feat: lock-free SPSC byte ring with drop counting"
```

---

### Task 4: ANSI parser

**Files:**
- Create: `src/ansi.h`, `src/ansi.cpp`, `test/test_ansi.cpp`

**Interfaces:**
- Produces: `struct TermSink` (pure virtual: `putChar(char)`, `carriageReturn()`, `lineFeed()`, `cursorLeft(int)`, `cursorRight(int)`, `eraseInLine(int mode)`, `clearScreen()`, `setColor(uint8_t)`), and `class AnsiParser { explicit AnsiParser(TermSink&); void feed(uint8_t); void feed(const uint8_t*, size_t); }`. Colors are 0..15, default 7.

- [ ] **Step 1: Write the failing test** `test/test_ansi.cpp`

```cpp
#include "check.h"
#include "ansi.h"
#include <string>

namespace {
struct RecSink : TermSink {
  std::string log;
  void putChar(char c) override { log += c; }
  void carriageReturn() override { log += "<CR>"; }
  void lineFeed() override { log += "<LF>"; }
  void cursorLeft(int n) override { log += "<L" + std::to_string(n) + ">"; }
  void cursorRight(int n) override { log += "<R" + std::to_string(n) + ">"; }
  void eraseInLine(int m) override { log += "<E" + std::to_string(m) + ">"; }
  void clearScreen() override { log += "<CLS>"; }
  void setColor(uint8_t i) override { log += "<C" + std::to_string(i) + ">"; }
};
struct NullSink : TermSink {
  void putChar(char) override {} void carriageReturn() override {} void lineFeed() override {}
  void cursorLeft(int) override {} void cursorRight(int) override {} void eraseInLine(int) override {}
  void clearScreen() override {} void setColor(uint8_t) override {}
};
std::string run(const std::string& in) {
  RecSink s; AnsiParser p(s);
  p.feed((const uint8_t*)in.data(), in.size());
  return s.log;
}
}  // namespace

SUITE(ansi_tests) {
  CHECK_STR(run("ab\r\n"), "ab<CR><LF>");
  CHECK_STR(run("\b"), "<L1>");
  CHECK_STR(run("\t"), "    ");
  CHECK_STR(run("\x7f"), "");
  CHECK_STR(run("\x07"), "");

  CHECK_STR(run("\x1b[K"), "<E0>");
  CHECK_STR(run("\x1b[0K"), "<E0>");
  CHECK_STR(run("\x1b[1K"), "<E1>");
  CHECK_STR(run("\x1b[2K"), "<E2>");
  CHECK_STR(run("\x1b[3K"), "");

  CHECK_STR(run("\x1b[D"), "<L1>");
  CHECK_STR(run("\x1b[3D"), "<L3>");
  CHECK_STR(run("\x1b[2C"), "<R2>");
  CHECK_STR(run("\x1b[5G"), "<CR><R4>");
  CHECK_STR(run("\x1b[1G"), "<CR>");

  CHECK_STR(run("\x1b[31mred\x1b[0m"), "<C1>red<C7>");
  CHECK_STR(run("\x1b[1;32m"), "<C10>");
  CHECK_STR(run("\x1b[m"), "<C7>");
  CHECK_STR(run("\x1b[91m"), "<C9>");
  CHECK_STR(run("\x1b[1m"), "<C15>");
  CHECK_STR(run("\x1b[1;31m\x1b[22m"), "<C9><C1>");

  CHECK_STR(run("\x1b[2J"), "<CLS>");
  CHECK_STR(run("\x1b[0J"), "");
  CHECK_STR(run("\x1b[?25lX"), "X");           // private sequences ignored

  {  // sequence split across feed() calls
    RecSink s; AnsiParser p(s);
    p.feed(0x1b); p.feed('['); p.feed('3'); p.feed('1'); p.feed('m');
    CHECK_STR(s.log, "<C1>");
  }

  // Review Focus 3: non-ASCII, garbage, unterminated sequences
  CHECK_STR(run("a\xF0\x9F\x93\x8A b"), "a? b");        // 4-byte UTF-8 emoji -> one '?'
  CHECK_STR(run("\xE2\x98\xA0"), "?");                  // 3-byte UTF-8
  CHECK_STR(run("\x1b(Bx"), "x");                       // charset designator swallowed
  CHECK_STR(run("\x1b[3\x1b[K"), "<E0>");               // ESC aborts an unfinished CSI
  CHECK_STR(run("\x1b[99999999999K"), "");              // huge parameter is clamped, not an erase mode
  CHECK_STR(run("\x1b[1;2;3;4;5;6;7;8m"), "<C15>");     // too many parameters tolerated

  {  // fuzz: random bytes, then escape-heavy bytes, must not crash
    NullSink s; AnsiParser p(s);
    unsigned x = 12345;
    for (int i = 0; i < 200000; i++) { x = x * 1664525u + 1013904223u; p.feed((uint8_t)(x >> 24)); }
    const char alphabet[] = "\x1b[;?0123456789mKDCGJH(B";
    for (int i = 0; i < 200000; i++) { x = x * 1664525u + 1013904223u; p.feed((uint8_t)alphabet[(x >> 24) % (sizeof alphabet - 1)]); }
    CHECK(true);
  }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `tools/run-tests.sh`
Expected: FAIL at compile time: `ansi.h: No such file or directory`

- [ ] **Step 3: Implement** `src/ansi.h`

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

struct TermSink {
  virtual ~TermSink() {}
  virtual void putChar(char c) = 0;
  virtual void carriageReturn() = 0;
  virtual void lineFeed() = 0;
  virtual void cursorLeft(int n) = 0;
  virtual void cursorRight(int n) = 0;
  virtual void eraseInLine(int mode) = 0;   // 0 cursor->end, 1 start->cursor, 2 whole line
  virtual void clearScreen() = 0;
  virtual void setColor(uint8_t idx) = 0;   // 0..15, 7 = default
};

// Minimal VT100 subset: printable ASCII, CR, LF, BS, TAB(4 spaces), CSI K D C G J m.
// Everything else is ignored. Non-ASCII UTF-8 lead bytes print '?'.
class AnsiParser {
public:
  explicit AnsiParser(TermSink& sink) : sink_(sink) {}
  void feed(uint8_t b);
  void feed(const uint8_t* data, size_t n);

private:
  enum State { GROUND, ESC, SKIP, CSI };
  static constexpr int MAX_PARAMS = 4;
  void startCsi();
  void finishCsi(uint8_t final);
  void applySgr();

  TermSink& sink_;
  State state_ = GROUND;
  int params_[MAX_PARAMS] = {0, 0, 0, 0};
  int idx_ = 0;
  bool priv_ = false;
  int fg_ = 7;
  bool bold_ = false;
};
```

`src/ansi.cpp`:
```cpp
#include "ansi.h"

void AnsiParser::feed(const uint8_t* d, size_t n) {
  for (size_t i = 0; i < n; i++) feed(d[i]);
}

void AnsiParser::startCsi() {
  idx_ = 0;
  priv_ = false;
  for (int i = 0; i < MAX_PARAMS; i++) params_[i] = 0;
  state_ = CSI;
}

void AnsiParser::feed(uint8_t b) {
  switch (state_) {
    case SKIP:
      state_ = GROUND;
      return;
    case ESC:
      if (b == '[') startCsi();
      else if (b == '(' || b == ')' || b == '*' || b == '+' || b == '#' || b == '%') state_ = SKIP;
      else state_ = GROUND;
      return;
    case CSI:
      if (b >= '0' && b <= '9') {
        int v = params_[idx_] * 10 + (b - '0');
        params_[idx_] = v > 9999 ? 9999 : v;
      } else if (b == ';') {
        if (idx_ < MAX_PARAMS - 1) idx_++;
      } else if (b >= 0x3C && b <= 0x3F) {
        priv_ = true;
      } else if (b >= 0x40 && b <= 0x7E) {
        finishCsi(b);
        state_ = GROUND;
      } else if (b == 0x1B) {
        state_ = ESC;
      }
      return;   // intermediates and control bytes inside a CSI are ignored
    case GROUND:
      break;
  }
  if (b == 0x1B) { state_ = ESC; return; }
  if (b == '\r') { sink_.carriageReturn(); return; }
  if (b == '\n') { sink_.lineFeed(); return; }
  if (b == 0x08) { sink_.cursorLeft(1); return; }
  if (b == '\t') { for (int i = 0; i < 4; i++) sink_.putChar(' '); return; }
  if (b >= 0x20 && b <= 0x7E) { sink_.putChar((char)b); return; }
  if (b >= 0xC0) { sink_.putChar('?'); return; }
  // 0x80-0xBF (UTF-8 continuation), DEL, BEL, other controls: ignored
}

void AnsiParser::finishCsi(uint8_t f) {
  if (priv_) return;
  int n = params_[0] > 0 ? params_[0] : 1;
  switch (f) {
    case 'K': if (params_[0] <= 2) sink_.eraseInLine(params_[0]); break;
    case 'D': sink_.cursorLeft(n); break;
    case 'C': sink_.cursorRight(n); break;
    case 'G': sink_.carriageReturn(); if (n > 1) sink_.cursorRight(n - 1); break;
    case 'J': if (params_[0] == 2 || params_[0] == 3) sink_.clearScreen(); break;
    case 'm': applySgr(); break;
    default: break;
  }
}

void AnsiParser::applySgr() {
  for (int i = 0; i <= idx_; i++) {
    int p = params_[i];
    if (p == 0) { fg_ = 7; bold_ = false; }
    else if (p == 1) bold_ = true;
    else if (p == 22) bold_ = false;
    else if (p >= 30 && p <= 37) fg_ = p - 30;
    else if (p == 39) fg_ = 7;
    else if (p >= 90 && p <= 97) fg_ = p - 90 + 8;
  }
  uint8_t c = (uint8_t)fg_;
  if (bold_ && c < 8) c += 8;
  sink_.setColor(c);
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `tools/run-tests.sh`
Expected: `... checks, 0 failures` (AddressSanitizer and UBSan report nothing)

- [ ] **Step 5: Commit**

```bash
tools/commit.sh "feat: ANSI subset parser with fuzz and UTF-8 tests"
```

---

### Task 5: TermBuf (grid, scrollback, view)

**Files:**
- Create: `src/termbuf.h`, `src/termbuf.cpp`, `test/test_termbuf.cpp`, `test/test_capture.cpp`

**Interfaces:**
- Consumes: `TermSink`, `AnsiParser` (Task 4); `COLS`, `ROWS`, `SCROLLBACK_LINES` (config.h).
- Produces:
  ```cpp
  struct Cell { char ch; uint8_t color; };
  class TermBuf : public TermSink {
    Cell cellAt(int row, int col) const;     // row 0..ROWS-1 of the visible window
    void scrollBy(int lines);                // + = toward older output
    void scrollToLive();
    void setFrozen(bool); bool frozen() const; int viewOffset() const;
    bool cursorVisible() const; int cursorRow() const; int cursorCol() const;
    int totalLines() const;
    int currentLineText(char* out, int cap) const;   // text of the cursor's line, trailing spaces trimmed
  };
  ```
  Line feed also returns the cursor to column 0.

- [ ] **Step 1: Write the failing tests** `test/test_termbuf.cpp`

```cpp
#include "check.h"
#include "termbuf.h"
#include <memory>
#include <string>

namespace {
struct Rig {
  TermBuf t; AnsiParser p{t};
  void feed(const std::string& s) { p.feed((const uint8_t*)s.data(), s.size()); }
};
std::string rowText(const TermBuf& t, int r) {
  std::string s;
  for (int c = 0; c < COLS; c++) s += t.cellAt(r, c).ch;
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
}
std::string num(const char* prefix, int i, int width) {
  char b[16]; snprintf(b, sizeof b, "%s%0*d", prefix, width, i); return b;
}
}  // namespace

SUITE(termbuf_tests) {
  {  // basics
    auto r = std::make_unique<Rig>();
    r->feed("abc");
    CHECK_STR(rowText(r->t, 0), "abc");
    CHECK_EQ(r->t.cursorCol(), 3);
    r->feed("\r\ncd");
    CHECK_STR(rowText(r->t, 1), "cd");
  }
  {  // Review Focus 1: bare LF returns to column 0
    auto r = std::make_unique<Rig>();
    r->feed("ab\ncd");
    CHECK_STR(rowText(r->t, 0), "ab");
    CHECK_STR(rowText(r->t, 1), "cd");
  }
  {  // wrap
    auto r = std::make_unique<Rig>();
    r->feed(std::string(60, 'x'));
    CHECK_EQ((int)rowText(r->t, 0).size(), COLS);
    CHECK_STR(rowText(r->t, 1), std::string(60 - COLS, 'x'));
    CHECK_EQ(r->t.cursorCol(), 60 - COLS);
  }
  {  // erase modes and line redraw as the S3 does it
    auto r = std::make_unique<Rig>(); r->feed("hello\x1b[3D\x1b[K");
    CHECK_STR(rowText(r->t, 0), "he");
    r = std::make_unique<Rig>(); r->feed("hello\x1b[2D\x1b[1K");
    CHECK_STR(rowText(r->t, 0), "    o");
    r = std::make_unique<Rig>(); r->feed("hello\x1b[2K");
    CHECK_STR(rowText(r->t, 0), "");
    r = std::make_unique<Rig>(); r->feed("\rHIZ> h\x1b[K\rHIZ> he\x1b[K");
    CHECK_STR(rowText(r->t, 0), "HIZ> he");
    r = std::make_unique<Rig>(); r->feed("\x1b[9Dz");
    CHECK_STR(rowText(r->t, 0), "z");
  }
  {  // color and current line text
    auto r = std::make_unique<Rig>(); r->feed("\x1b[31mr");
    CHECK_EQ(r->t.cellAt(0, 0).color, 1);
    r = std::make_unique<Rig>(); r->feed("HIZ> ");
    char buf[64]; r->t.currentLineText(buf, sizeof buf);
    CHECK_STR(buf, "HIZ>");
  }
  {  // scrolling window: 30 lines, cursor on blank line 30
    auto r = std::make_unique<Rig>();
    for (int i = 0; i < 30; i++) r->feed(num("L", i, 2) + "\r\n");
    CHECK_STR(rowText(r->t, 0), "L09");
    CHECK_STR(rowText(r->t, ROWS - 1), "");
    CHECK_EQ(r->t.cursorRow(), ROWS - 1);
    CHECK(r->t.cursorVisible());
    r->t.scrollBy(5);
    CHECK_STR(rowText(r->t, 0), "L04");
    CHECK(!r->t.cursorVisible());
    r->t.scrollBy(1000);
    CHECK_EQ(r->t.viewOffset(), 9);
    CHECK_STR(rowText(r->t, 0), "L00");
    r->t.scrollToLive();
    CHECK_EQ(r->t.viewOffset(), 0);
    r->t.scrollBy(3);              // scrolled-up view stays put while new lines arrive
    CHECK_STR(rowText(r->t, 0), "L06");
    for (int i = 0; i < 4; i++) r->feed("more\r\n");
    CHECK_STR(rowText(r->t, 0), "L06");
  }
  {  // pause (frozen) keeps the view; unfreeze jumps to live
    auto r = std::make_unique<Rig>();
    for (int i = 0; i < 30; i++) r->feed(num("L", i, 2) + "\r\n");
    r->t.setFrozen(true);
    CHECK_STR(rowText(r->t, 0), "L09");
    for (int i = 0; i < 5; i++) r->feed(num("M", i, 1) + "\r\n");
    CHECK_STR(rowText(r->t, 0), "L09");
    r->t.setFrozen(false);
    CHECK_STR(rowText(r->t, 0), "L14");
  }
  {  // ring wrap: oldest lines are discarded
    auto r = std::make_unique<Rig>();
    const int N = SCROLLBACK_LINES + 50;
    for (int i = 0; i < N; i++) r->feed(num("", i, 3) + "\r\n");
    r->t.scrollBy(1000000);
    CHECK_EQ(r->t.viewOffset(), SCROLLBACK_LINES - ROWS);
    CHECK_STR(rowText(r->t, 0), "051");        // 450 lines written -> cursor on line 450, oldest kept is line 51
  }
  {  // Review Focus 2: very long lines
    auto r = std::make_unique<Rig>();
    r->feed(std::string(5000, 'y'));
    CHECK_STR(rowText(r->t, 0), std::string(COLS, 'y'));
    CHECK_EQ(r->t.cursorCol(), 5000 % COLS);
    r = std::make_unique<Rig>();
    r->feed(std::string(30000, 'y'));            // more than SCROLLBACK_LINES lines
    CHECK_EQ(r->t.cursorCol(), 30000 % COLS);
    for (int row = 0; row < ROWS - 1; row++) CHECK_EQ((int)rowText(r->t, row).size(), COLS);
  }
  {  // clear screen
    auto r = std::make_unique<Rig>();
    for (int i = 0; i < 30; i++) r->feed("x\r\n");
    r->feed("\x1b[2J");
    CHECK_STR(rowText(r->t, 0), "");
    CHECK_EQ(r->t.totalLines(), 1);
    CHECK_EQ(r->t.viewOffset(), 0);
  }
}
```

`test/test_capture.cpp` (uses the real S3 capture from Task 2):
```cpp
#include "check.h"
#include "termbuf.h"
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

SUITE(capture_tests) {
  std::ifstream f("test/captures/s3_session.bin", std::ios::binary);
  CHECK(f.good());
  std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  CHECK(data.size() > 1000);

  auto t = std::make_unique<TermBuf>();
  AnsiParser p(*t);
  p.feed((const uint8_t*)data.data(), data.size());

  bool sawHelp = false, sawPrompt = false, allPrintable = true;
  t->scrollBy(1000000);
  for (;;) {
    for (int r = 0; r < ROWS; r++) {
      std::string s;
      for (int c = 0; c < COLS; c++) {
        char ch = t->cellAt(r, c).ch;
        if (ch < 0x20 || ch > 0x7e) allPrintable = false;
        s += ch;
      }
      if (s.find("Commands by Mode") != std::string::npos) sawHelp = true;
      if (s.find("HIZ>") != std::string::npos) sawPrompt = true;
    }
    if (t->viewOffset() == 0) break;
    t->scrollBy(-ROWS);
    if (t->viewOffset() < 0) break;
  }
  CHECK(sawHelp);
  CHECK(sawPrompt);
  CHECK(allPrintable);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `tools/run-tests.sh`
Expected: FAIL at compile time: `termbuf.h: No such file or directory`

- [ ] **Step 3: Implement** `src/termbuf.h`

```cpp
#pragma once
#include "ansi.h"
#include "config.h"

struct Cell { char ch; uint8_t color; };

class TermBuf : public TermSink {
public:
  TermBuf();
  // TermSink
  void putChar(char c) override;
  void carriageReturn() override { col_ = 0; }
  void lineFeed() override;                 // also returns to column 0
  void cursorLeft(int n) override;
  void cursorRight(int n) override;
  void eraseInLine(int mode) override;
  void clearScreen() override;              // wipes scrollback too
  void setColor(uint8_t idx) override { color_ = idx; }

  // view
  Cell cellAt(int row, int col) const;
  void scrollBy(int lines);
  void scrollToLive() { viewOffset_ = 0; }
  void setFrozen(bool f);
  bool frozen() const { return frozen_; }
  int viewOffset() const { return viewOffset_; }
  bool cursorVisible() const { return cursorRow() < ROWS; }
  int cursorRow() const { return (int)(lineNo_ - topLine()); }
  int cursorCol() const { return col_ >= COLS ? COLS - 1 : col_; }
  int totalLines() const { return (int)(lineNo_ + 1); }
  int currentLineText(char* out, int cap) const;

private:
  Cell* line(long n) { return lines_[n % SCROLLBACK_LINES]; }
  const Cell* line(long n) const { return lines_[n % SCROLLBACK_LINES]; }
  void clearLine(long n);
  long oldest() const { return lineNo_ >= SCROLLBACK_LINES ? lineNo_ - SCROLLBACK_LINES + 1 : 0; }
  int maxOffset() const;
  long topLine() const;

  Cell lines_[SCROLLBACK_LINES][COLS];
  long lineNo_ = 0;       // absolute index of the cursor's line
  int col_ = 0;
  uint8_t color_ = 7;
  int viewOffset_ = 0;    // lines scrolled back from live
  bool frozen_ = false;
};
```

`src/termbuf.cpp`:
```cpp
#include "termbuf.h"

static const Cell BLANK = {' ', 7};

TermBuf::TermBuf() { clearScreen(); }

void TermBuf::clearLine(long n) {
  Cell* l = line(n);
  for (int i = 0; i < COLS; i++) l[i] = BLANK;
}

void TermBuf::clearScreen() {
  for (int r = 0; r < SCROLLBACK_LINES; r++)
    for (int c = 0; c < COLS; c++) lines_[r][c] = BLANK;
  lineNo_ = 0; col_ = 0; viewOffset_ = 0;
}

int TermBuf::maxOffset() const {
  long count = lineNo_ - oldest() + 1;
  return count > ROWS ? (int)(count - ROWS) : 0;
}

long TermBuf::topLine() const {
  long t = lineNo_ - (ROWS - 1) - viewOffset_;
  return t < 0 ? 0 : t;
}

void TermBuf::putChar(char c) {
  if (col_ >= COLS) lineFeed();
  line(lineNo_)[col_++] = Cell{c, color_};
}

void TermBuf::lineFeed() {
  lineNo_++;
  clearLine(lineNo_);
  col_ = 0;
  if (viewOffset_ > 0 || frozen_) {
    viewOffset_++;
    int m = maxOffset();
    if (viewOffset_ > m) viewOffset_ = m;
  }
}

void TermBuf::cursorLeft(int n) {
  col_ -= n;
  if (col_ < 0) col_ = 0;
}

void TermBuf::cursorRight(int n) {
  int t = col_ + n;
  if (t > COLS - 1) t = COLS - 1;
  if (t > col_) col_ = t;
}

void TermBuf::eraseInLine(int mode) {
  Cell* l = line(lineNo_);
  int c = col_ >= COLS ? COLS - 1 : col_;
  int from = 0, to = COLS - 1;
  if (mode == 0) from = c;
  else if (mode == 1) to = c;
  for (int i = from; i <= to; i++) l[i] = BLANK;
}

Cell TermBuf::cellAt(int row, int col) const {
  long n = topLine() + row;
  if (row < 0 || row >= ROWS || col < 0 || col >= COLS || n > lineNo_ || n < oldest()) return BLANK;
  return line(n)[col];
}

void TermBuf::scrollBy(int lines) {
  long v = (long)viewOffset_ + lines;
  int m = maxOffset();
  viewOffset_ = v < 0 ? 0 : (v > m ? m : (int)v);
}

void TermBuf::setFrozen(bool f) {
  frozen_ = f;
  if (!f) viewOffset_ = 0;
}

int TermBuf::currentLineText(char* out, int cap) const {
  const Cell* l = line(lineNo_);
  int end = COLS;
  while (end > 0 && l[end - 1].ch == ' ') end--;
  int n = end < cap - 1 ? end : cap - 1;
  for (int i = 0; i < n; i++) out[i] = l[i].ch;
  out[n] = 0;
  return n;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `tools/run-tests.sh`
Expected: `... checks, 0 failures`. If the ring-wrap check fails, re-derive the expected text from the arithmetic in the note above before touching the implementation.

- [ ] **Step 5: Commit**

```bash
tools/commit.sh "feat: TermBuf grid, scrollback, pause and view window"
```

---

### Task 6: Prompt parser

**Files:**
- Create: `src/prompt.h`, `src/prompt.cpp`, `test/test_prompt.cpp`

**Interfaces:**
- Produces: `bool parsePromptMode(const char* line, char* modeOut, int cap)` — true and fills `modeOut` when `line` starts with `NAME>` followed by end-of-string or a space; `NAME` is 1-12 chars of `[A-Za-z0-9_]`.

- [ ] **Step 1: Write the failing test** `test/test_prompt.cpp`

```cpp
#include "check.h"
#include "prompt.h"

SUITE(prompt_tests) {
  char m[16];
  CHECK(parsePromptMode("HIZ>", m, sizeof m));       CHECK_STR(m, "HIZ");
  CHECK(parsePromptMode("HIZ> help", m, sizeof m));  CHECK_STR(m, "HIZ");
  CHECK(parsePromptMode("I2C> ", m, sizeof m));      CHECK_STR(m, "I2C");
  CHECK(!parsePromptMode("HIZ>foo", m, sizeof m));
  CHECK(!parsePromptMode(">", m, sizeof m));
  CHECK(!parsePromptMode("", m, sizeof m));
  CHECK(!parsePromptMode("Enter index (default 1): ", m, sizeof m));
  CHECK(!parsePromptMode("=== Help Shell ===", m, sizeof m));
  CHECK(!parsePromptMode("a b> ", m, sizeof m));
  CHECK(!parsePromptMode("VERYLONGMODENAMEXX> ", m, sizeof m));
  char tiny[3];
  CHECK(!parsePromptMode("HIZ>", tiny, sizeof tiny));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `tools/run-tests.sh`
Expected: FAIL at compile time: `prompt.h: No such file or directory`

- [ ] **Step 3: Implement** `src/prompt.h`

```cpp
#pragma once
bool parsePromptMode(const char* line, char* modeOut, int cap);
```

`src/prompt.cpp`:
```cpp
#include "prompt.h"
#include <ctype.h>
#include <string.h>

bool parsePromptMode(const char* line, char* out, int cap) {
  int n = 0;
  while (line[n] && (isalnum((unsigned char)line[n]) || line[n] == '_')) n++;
  if (n == 0 || n > 12 || n >= cap) return false;
  if (line[n] != '>') return false;
  if (line[n + 1] != '\0' && line[n + 1] != ' ') return false;
  memcpy(out, line, n);
  out[n] = 0;
  return true;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `tools/run-tests.sh`
Expected: `... checks, 0 failures`

- [ ] **Step 5: Commit**

```bash
tools/commit.sh "feat: prompt parser for current Bit Pirate mode"
```

---

### Task 7: Touch calibration math and gesture recognizer

**Files:**
- Create: `src/calib.h`, `src/calib.cpp`, `src/gesture.h`, `src/gesture.cpp`, `test/test_calib.cpp`, `test/test_gesture.cpp`

**Interfaces:**
- Consumes: `CHAR_H`, `LONG_PRESS_MS` (config.h).
- Produces:
  ```cpp
  struct AxisCal { int raw1, s1, raw2, s2; };
  struct TouchCal { AxisCal x, y; };
  constexpr TouchCal DEFAULT_TOUCH_CAL = {{200,0,3700,319},{240,0,3800,239}};
  int mapAxis(const AxisCal&, int raw, int lo, int hi);

  enum class GestureType : uint8_t { None, Tap, LongPress, Scroll };
  struct GestureEvent { GestureType type; int x, y; int lines; };   // lines > 0: toward older output
  class GestureRecognizer { GestureEvent update(bool pressed, int x, int y, uint32_t nowMs); };
  constexpr int DRAG_PX = 10; constexpr uint32_t RELEASE_DEBOUNCE_MS = 50;
  ```

- [ ] **Step 1: Write the failing tests**

`test/test_calib.cpp`:
```cpp
#include "check.h"
#include "calib.h"

SUITE(calib_tests) {
  AxisCal a{200, 0, 3700, 319};
  CHECK_EQ(mapAxis(a, 200, 0, 319), 0);
  CHECK_EQ(mapAxis(a, 3700, 0, 319), 319);
  CHECK_EQ(mapAxis(a, 1950, 0, 319), 159);
  CHECK_EQ(mapAxis(a, 0, 0, 319), 0);           // clamped
  CHECK_EQ(mapAxis(a, 4095, 0, 319), 319);      // clamped
  AxisCal inv{3800, 0, 240, 239};               // inverted axis
  CHECK_EQ(mapAxis(inv, 3800, 0, 239), 0);
  CHECK_EQ(mapAxis(inv, 240, 0, 239), 239);
  CHECK_EQ(mapAxis(inv, 2020, 0, 239), 119);
  AxisCal bad{100, 7, 100, 50};                 // degenerate: no divide by zero
  CHECK_EQ(mapAxis(bad, 123, 0, 319), 7);
}
```

`test/test_gesture.cpp`:
```cpp
#include "check.h"
#include "gesture.h"

SUITE(gesture_tests) {
  {  // tap
    GestureRecognizer g;
    CHECK(g.update(true, 100, 100, 0).type == GestureType::None);
    CHECK(g.update(true, 101, 100, 50).type == GestureType::None);
    GestureEvent e = g.update(false, 0, 0, 200);
    CHECK(e.type == GestureType::Tap);
    CHECK_EQ(e.x, 100); CHECK_EQ(e.y, 100);
    CHECK(g.update(false, 0, 0, 300).type == GestureType::None);
  }
  {  // jitter under the drag threshold is still a tap
    GestureRecognizer g;
    g.update(true, 50, 50, 0); g.update(true, 55, 56, 20);
    CHECK(g.update(false, 0, 0, 100).type == GestureType::Tap);
  }
  {  // long press fires once, release afterwards is silent
    GestureRecognizer g;
    g.update(true, 40, 60, 0);
    CHECK(g.update(true, 40, 60, 599).type == GestureType::None);
    GestureEvent e = g.update(true, 40, 60, 600);
    CHECK(e.type == GestureType::LongPress);
    CHECK_EQ(e.x, 40); CHECK_EQ(e.y, 60);
    CHECK(g.update(true, 40, 60, 900).type == GestureType::None);
    CHECK(g.update(false, 0, 0, 1000).type == GestureType::None);
    CHECK(g.update(false, 0, 0, 1100).type == GestureType::None);
  }
  {  // scroll: drag down 24 px = 3 lines older, then 8 more = 1
    GestureRecognizer g;
    g.update(true, 100, 100, 0);
    GestureEvent e = g.update(true, 100, 124, 50);
    CHECK(e.type == GestureType::Scroll); CHECK_EQ(e.lines, 3);
    e = g.update(true, 100, 132, 80);
    CHECK(e.type == GestureType::Scroll); CHECK_EQ(e.lines, 1);
    CHECK(g.update(true, 100, 133, 100).type == GestureType::None);
    CHECK(g.update(false, 0, 0, 400).type == GestureType::None);   // a drag is not a tap
  }
  {  // drag up is negative
    GestureRecognizer g;
    g.update(true, 100, 100, 0);
    GestureEvent e = g.update(true, 100, 76, 50);
    CHECK(e.type == GestureType::Scroll); CHECK_EQ(e.lines, -3);
  }
  {  // moving cancels the long press
    GestureRecognizer g;
    g.update(true, 100, 100, 0); g.update(true, 100, 112, 100);
    CHECK(g.update(true, 100, 112, 1000).type == GestureType::None);
  }
  {  // Review Focus 5: a 20 ms touch dropout mid-press does not end the gesture
    GestureRecognizer g;
    g.update(true, 50, 50, 0); g.update(true, 50, 50, 10);
    CHECK(g.update(false, 0, 0, 20).type == GestureType::None);
    CHECK(g.update(true, 50, 50, 30).type == GestureType::None);
    CHECK(g.update(false, 0, 0, 100).type == GestureType::Tap);
  }
  {  // dropout across the long-press moment still fires the long press once
    GestureRecognizer g;
    g.update(true, 70, 70, 0); g.update(true, 70, 70, 580);
    CHECK(g.update(false, 0, 0, 600).type == GestureType::LongPress);
    CHECK(g.update(true, 70, 70, 620).type == GestureType::None);
    CHECK(g.update(false, 0, 0, 900).type == GestureType::None);
  }
  {  // a release lasting >= the debounce ends the gesture; the next press starts a new one
    GestureRecognizer g;
    g.update(true, 10, 10, 0);
    CHECK(g.update(false, 0, 0, 60).type == GestureType::Tap);
    g.update(true, 200, 200, 100);
    GestureEvent e = g.update(false, 0, 0, 200);
    CHECK(e.type == GestureType::Tap); CHECK_EQ(e.x, 200);
  }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `tools/run-tests.sh`
Expected: FAIL at compile time: `calib.h: No such file or directory`

- [ ] **Step 3: Implement** `src/calib.h`

```cpp
#pragma once
struct AxisCal { int raw1, s1, raw2, s2; };   // two reference points: raw reading -> screen coordinate
struct TouchCal { AxisCal x, y; };
// Typical CYD values for landscape (TFT rotation 1, touch rotation 1); replaced by the calibration screen.
constexpr TouchCal DEFAULT_TOUCH_CAL = {{200, 0, 3700, 319}, {240, 0, 3800, 239}};
int mapAxis(const AxisCal& c, int raw, int lo, int hi);
```

`src/calib.cpp`:
```cpp
#include "calib.h"

int mapAxis(const AxisCal& c, int raw, int lo, int hi) {
  if (c.raw2 == c.raw1) return c.s1;
  long v = c.s1 + (long)(raw - c.raw1) * (c.s2 - c.s1) / (c.raw2 - c.raw1);
  return v < lo ? lo : (v > hi ? hi : (int)v);
}
```

`src/gesture.h`:
```cpp
#pragma once
#include <stdint.h>
#include "config.h"

constexpr int DRAG_PX = 10;
constexpr uint32_t RELEASE_DEBOUNCE_MS = 50;

enum class GestureType : uint8_t { None, Tap, LongPress, Scroll };
struct GestureEvent { GestureType type; int x, y; int lines; };   // lines > 0: toward older output

class GestureRecognizer {
public:
  GestureEvent update(bool pressed, int x, int y, uint32_t nowMs);   // at most one event per call
private:
  bool down_ = false, dragging_ = false, longFired_ = false;
  int startX_ = 0, startY_ = 0, refY_ = 0, lastX_ = 0, lastY_ = 0;
  uint32_t startT_ = 0, lastSeen_ = 0;
};
```

`src/gesture.cpp`:
```cpp
#include "gesture.h"
#include <stdlib.h>

GestureEvent GestureRecognizer::update(bool pressed, int x, int y, uint32_t now) {
  const GestureEvent none{GestureType::None, 0, 0, 0};
  if (pressed) {
    lastX_ = x; lastY_ = y; lastSeen_ = now;
  } else if (down_ && now - lastSeen_ < RELEASE_DEBOUNCE_MS) {
    pressed = true; x = lastX_; y = lastY_;          // resistive-panel dropout: treat as still pressed
  }

  if (pressed) {
    if (!down_) {
      down_ = true; dragging_ = false; longFired_ = false;
      startX_ = x; startY_ = y; refY_ = y; startT_ = now;
      return none;
    }
    if (!dragging_ && (abs(x - startX_) >= DRAG_PX || abs(y - startY_) >= DRAG_PX)) dragging_ = true;
    if (dragging_) {
      int lines = (y - refY_) / CHAR_H;
      if (lines != 0) {
        refY_ += lines * CHAR_H;
        return GestureEvent{GestureType::Scroll, startX_, startY_, lines};
      }
      return none;
    }
    if (!longFired_ && now - startT_ >= LONG_PRESS_MS) {
      longFired_ = true;
      return GestureEvent{GestureType::LongPress, startX_, startY_, 0};
    }
    return none;
  }

  if (down_) {
    down_ = false;
    if (!dragging_ && !longFired_) return GestureEvent{GestureType::Tap, startX_, startY_, 0};
  }
  return none;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `tools/run-tests.sh`
Expected: `... checks, 0 failures`

- [ ] **Step 5: Commit**

```bash
tools/commit.sh "feat: touch calibration mapping and gesture recognizer with dropout debounce"
```

---

### Task 8: Layout and on-screen keyboard model

**Files:**
- Create: `src/layout.h`, `src/layout.cpp`, `src/keyboard.h`, `src/keyboard.cpp`, `test/test_layout.cpp`, `test/test_keyboard.cpp`

**Interfaces:**
- Consumes: constants from `config.h`.
- Produces:
  ```cpp
  struct Rect { int x, y, w, h; bool contains(int px, int py) const; };
  enum class BtnKind : uint8_t { Command, Enter, CtrlC, Keyboard };
  struct CmdItem { const char* text; bool enter; };
  struct CmdButton { const char* label; BtnKind kind; int nItems; CmdItem items[4]; };
  constexpr int BTN_COLS = 5, BTN_COUNT = 10, BTN_W = 64, BTN_ROW_H = 24, POPUP_W = 128, POPUP_ITEM_H = 22;
  constexpr int OVERLAY_TOP = 104, OVERLAY_ROWS = 11;
  extern const CmdButton CMD_BUTTONS[BTN_COUNT];
  Rect buttonRect(int idx);  int buttonAt(int x, int y);               // -1 if none
  Rect popupItemRect(int btn, int item);  int popupItemAt(int btn, int x, int y);   // -1 if none

  enum class KeyType : uint8_t { None, Char, Backspace, Enter, Space, CtrlC, Page, Hide };
  struct Key { KeyType type; char ch; };
  struct KeyRect { int x, y, w, h; Key key; char label[5]; };
  class Keyboard { Keyboard(int x, int y, int w, int h); void setPage(int p); int page() const;
                   int keyCount() const; const KeyRect& key(int i) const; Key hit(int px, int py) const; };
  ```

- [ ] **Step 1: Write the failing tests**

`test/test_layout.cpp`:
```cpp
#include "check.h"
#include "layout.h"
#include <string.h>

SUITE(layout_tests) {
  CHECK_EQ(buttonAt(0, BTN_Y), 0);
  CHECK_EQ(buttonAt(64, BTN_Y), 1);
  CHECK_EQ(buttonAt(0, BTN_Y + 24), 5);
  CHECK_EQ(buttonAt(319, 239), 9);
  CHECK_EQ(buttonAt(10, BTN_Y - 1), -1);
  CHECK_EQ(buttonAt(10, SCREEN_H), -1);
  CHECK_EQ(buttonAt(SCREEN_W, BTN_Y), -1);
  CHECK(CMD_BUTTONS[7].kind == BtnKind::CtrlC);
  CHECK(CMD_BUTTONS[8].kind == BtnKind::Keyboard);
  CHECK(CMD_BUTTONS[9].kind == BtnKind::Enter);

  for (int i = 0; i < BTN_COUNT; i++) {
    Rect r = buttonRect(i);
    CHECK(r.x >= 0 && r.y >= BTN_Y && r.x + r.w <= SCREEN_W && r.y + r.h <= SCREEN_H);
    CHECK_EQ(buttonAt(r.x + r.w / 2, r.y + r.h / 2), i);
    const CmdButton& b = CMD_BUTTONS[i];
    CHECK(b.label && b.label[0]);
    if (b.kind != BtnKind::Command) continue;
    CHECK(b.nItems >= 1 && b.nItems <= 4);
    for (int k = 0; k < b.nItems; k++) CHECK(b.items[k].text && b.items[k].text[0]);
    if (b.nItems < 2) continue;
    for (int k = 0; k < b.nItems; k++) {          // popup items on screen, stacked upward from the button row
      Rect p = popupItemRect(i, k);
      CHECK(p.x >= 0 && p.x + p.w <= SCREEN_W && p.y >= OVERLAY_TOP && p.y + p.h <= BTN_Y);
      CHECK_EQ(p.y + p.h, BTN_Y - k * POPUP_ITEM_H);
      CHECK_EQ(popupItemAt(i, p.x + 2, p.y + 2), k);
    }
    CHECK_EQ(popupItemAt(i, 0, 0), -1);
  }
  CHECK(popupItemRect(4, 0).x + POPUP_W <= SCREEN_W);   // rightmost button's popup is clamped
}
```

`test/test_keyboard.cpp`:
```cpp
#include "check.h"
#include "keyboard.h"

SUITE(keyboard_tests) {
  Keyboard kb(0, 104, 320, 136);
  CHECK_EQ(kb.page(), 0);
  Key k = kb.hit(16, 121);       CHECK(k.type == KeyType::Char);      CHECK_EQ(k.ch, 'q');
  k = kb.hit(20, 148 + 10);      CHECK(k.type == KeyType::Char);      CHECK_EQ(k.ch, 'a');   // row 1 starts half a key in
  k = kb.hit(5, 148 + 10);       CHECK(k.type == KeyType::None);
  k = kb.hit(2, 172 + 5);        CHECK(k.type == KeyType::Page);
  k = kb.hit(318, 172 + 5);      CHECK(k.type == KeyType::Backspace);
  k = kb.hit(5, 211);            CHECK(k.type == KeyType::CtrlC);
  k = kb.hit(50, 211);           CHECK(k.type == KeyType::Hide);
  k = kb.hit(160, 211);          CHECK(k.type == KeyType::Space);     CHECK_EQ(k.ch, ' ');
  k = kb.hit(310, 211);          CHECK(k.type == KeyType::Enter);
  k = kb.hit(160, 50);           CHECK(k.type == KeyType::None);      // above the keyboard

  kb.setPage(1);
  CHECK_EQ(kb.page(), 1);
  k = kb.hit(16, 121);           CHECK_EQ(k.ch, '1');
  k = kb.hit(2, 172 + 5);        CHECK(k.type == KeyType::Page);
  k = kb.hit(200, 211);          CHECK(k.type == KeyType::Char);      CHECK_EQ(k.ch, '#');   // x 192..223

  for (int page = 0; page < 2; page++) {
    kb.setPage(page);
    for (int i = 0; i < kb.keyCount(); i++) {
      const KeyRect& a = kb.key(i);
      CHECK(a.w > 0 && a.h > 0 && a.x >= 0 && a.y >= 104 && a.x + a.w <= 320 && a.y + a.h <= 240);
      for (int j = i + 1; j < kb.keyCount(); j++) {      // no overlaps
        const KeyRect& b = kb.key(j);
        bool apart = a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
        CHECK(apart);
      }
    }
  }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `tools/run-tests.sh`
Expected: FAIL at compile time: `layout.h: No such file or directory`

- [ ] **Step 3: Implement** `src/layout.h`

```cpp
#pragma once
#include <stdint.h>
#include "config.h"

struct Rect {
  int x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

enum class BtnKind : uint8_t { Command, Enter, CtrlC, Keyboard };
struct CmdItem { const char* text; bool enter; };           // enter: send Enter after the text
struct CmdButton { const char* label; BtnKind kind; int nItems; CmdItem items[4]; };   // items[0] is the tap action

constexpr int BTN_COLS = 5, BTN_COUNT = 10, BTN_W = 64, BTN_ROW_H = 24;
constexpr int POPUP_W = 128, POPUP_ITEM_H = 22;
constexpr int OVERLAY_TOP = 104;                              // keyboard / popup region starts here
constexpr int OVERLAY_ROWS = (OVERLAY_TOP - TERM_Y) / CHAR_H;   // terminal rows still visible above an overlay (11)

extern const CmdButton CMD_BUTTONS[BTN_COUNT];

Rect buttonRect(int idx);
int buttonAt(int x, int y);
Rect popupItemRect(int btn, int item);        // item 0 is nearest the button row
int popupItemAt(int btn, int x, int y);
```

`src/layout.cpp`:
```cpp
#include "layout.h"

const CmdButton CMD_BUTTONS[BTN_COUNT] = {
  {"mode",   BtnKind::Command, 4, {{"mode", true}, {"system", true}, {"profile", true}, {"man", true}}},
  {"help",   BtnKind::Command, 4, {{"help", true}, {"man", true}, {"alias", true}, {"hex ", false}}},
  {"scan",   BtnKind::Command, 3, {{"scan", true}, {"ping", true}, {"autobaud", true}}},
  {"read",   BtnKind::Command, 3, {{"read", true}, {"raw", true}, {"temp", true}}},
  {"write",  BtnKind::Command, 3, {{"write ", false}, {"spam ", false}, {"trigger ", false}}},
  {"sniff",  BtnKind::Command, 2, {{"sniff", true}, {"sniff raw", true}}},
  {"config", BtnKind::Command, 3, {{"config", true}, {"profile", true}, {"alias", true}}},
  {"^C",     BtnKind::CtrlC,   0, {}},
  {"KB",     BtnKind::Keyboard, 0, {}},
  {"Enter",  BtnKind::Enter,   0, {}},
};

Rect buttonRect(int idx) {
  return Rect{(idx % BTN_COLS) * BTN_W, BTN_Y + (idx / BTN_COLS) * BTN_ROW_H, BTN_W, BTN_ROW_H};
}

int buttonAt(int x, int y) {
  if (x < 0 || x >= SCREEN_W || y < BTN_Y || y >= SCREEN_H) return -1;
  return ((y - BTN_Y) / BTN_ROW_H) * BTN_COLS + x / BTN_W;
}

Rect popupItemRect(int btn, int item) {
  int x = buttonRect(btn).x;
  if (x > SCREEN_W - POPUP_W) x = SCREEN_W - POPUP_W;
  return Rect{x, BTN_Y - (item + 1) * POPUP_ITEM_H, POPUP_W, POPUP_ITEM_H};
}

int popupItemAt(int btn, int x, int y) {
  for (int i = 0; i < CMD_BUTTONS[btn].nItems; i++)
    if (popupItemRect(btn, i).contains(x, y)) return i;
  return -1;
}
```

`src/keyboard.h`:
```cpp
#pragma once
#include <stdint.h>

enum class KeyType : uint8_t { None, Char, Backspace, Enter, Space, CtrlC, Page, Hide };
struct Key { KeyType type; char ch; };
struct KeyRect { int x, y, w, h; Key key; char label[5]; };

class Keyboard {
public:
  Keyboard(int x, int y, int w, int h);
  void setPage(int p);                       // 0 letters, 1 symbols
  int page() const { return page_; }
  int keyCount() const { return count_; }
  const KeyRect& key(int i) const { return keys_[i]; }
  Key hit(int px, int py) const;             // {None,0} when no key is under the point
private:
  static constexpr int MAX_KEYS = 48;
  void build();
  int x_, y_, w_, h_, page_ = 0, count_ = 0;
  KeyRect keys_[MAX_KEYS];
};
```

`src/keyboard.cpp`:
```cpp
#include "keyboard.h"
#include <string.h>

Keyboard::Keyboard(int x, int y, int w, int h) : x_(x), y_(y), w_(w), h_(h) { build(); }

void Keyboard::setPage(int p) { page_ = p ? 1 : 0; build(); }

Key Keyboard::hit(int px, int py) const {
  for (int i = 0; i < count_; i++) {
    const KeyRect& k = keys_[i];
    if (px >= k.x && px < k.x + k.w && py >= k.y && py < k.y + k.h) return k.key;
  }
  return Key{KeyType::None, 0};
}

// Rows are 20 half-units wide. put() takes a width in half-units.
void Keyboard::build() {
  count_ = 0;
  int rowH = h_ / 4;
  for (int r = 0; r < 4; r++) {
    int acc = 0;
    int top = y_ + r * rowH;
    auto put = [&](int hu, KeyType t, char ch, const char* label) {
      if (count_ >= MAX_KEYS) return;
      KeyRect& k = keys_[count_++];
      k.x = x_ + acc * w_ / 20;
      k.w = x_ + (acc + hu) * w_ / 20 - k.x;
      k.y = top; k.h = rowH;
      k.key = Key{t, ch};
      strncpy(k.label, label, sizeof k.label - 1);
      k.label[sizeof k.label - 1] = 0;
      acc += hu;
    };
    auto chars = [&](const char* s) {
      for (; *s; s++) { char l[2] = {*s, 0}; put(2, KeyType::Char, *s, l); }
    };
    if (page_ == 0) {
      if (r == 0) chars("qwertyuiop");
      else if (r == 1) { acc = 1; chars("asdfghjkl"); }
      else if (r == 2) { put(3, KeyType::Page, 0, "123"); chars("zxcvbnm"); put(3, KeyType::Backspace, 0, "<-"); }
      else { put(3, KeyType::CtrlC, 0, "^C"); put(3, KeyType::Hide, 0, "KB"); put(10, KeyType::Space, ' ', "spc"); put(4, KeyType::Enter, 0, "Ent"); }
    } else {
      if (r == 0) chars("1234567890");
      else if (r == 1) chars("-_/\\.,:;=+");
      else if (r == 2) { put(3, KeyType::Page, 0, "abc"); chars("[]<>|&\""); put(3, KeyType::Backspace, 0, "<-"); }
      else { put(3, KeyType::CtrlC, 0, "^C"); put(3, KeyType::Hide, 0, "KB"); put(6, KeyType::Space, ' ', "spc");
             put(2, KeyType::Char, '#', "#"); put(2, KeyType::Char, '@', "@"); put(4, KeyType::Enter, 0, "Ent"); }
    }
  }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `tools/run-tests.sh`
Expected: `... checks, 0 failures`.

- [ ] **Step 5: Commit**

```bash
tools/commit.sh "feat: button layout table and on-screen keyboard model"
```

---

### Task 9: Display bring-up on the CYD

**Files:**
- Create: `cyd-bitpirate-monitor.ino` (bring-up version, replaced in Task 12)

**Interfaces:**
- Consumes: `tools/build.sh`, `tools/flash.sh`, libraries from Task 1, backup from Task 1.

- [ ] **Step 1: Confirm the backup exists**

Run: `ls -l ~/cyd-backup/cyd-original-4MB.bin`
Expected: 4194304 bytes. Do not continue without it.

- [ ] **Step 2: Create the bring-up sketch** `cyd-bitpirate-monitor.ino`

```cpp
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "src/config.h"

TFT_eSPI tft;

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
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
```

- [ ] **Step 3: Build**

Run: `tools/build.sh`
Expected: compile succeeds, ends with a "Sketch uses ... bytes" summary. If it fails with "no driver defined" the build flags were ignored: re-run with `--build-property "compiler.cpp.extra_flags=..."` in place of `build.extra_flags` in `tools/build.sh` (same flag string).

- [ ] **Step 4: Flash**

```bash
ls /dev/ttyUSB*          # the CYD
PORT=/dev/ttyUSB0 tools/flash.sh
```
Expected: `Hard resetting via RTS pin...`

- [ ] **Step 5: Verify on the device (ask the user to look)**

Expected on screen: four colour bars left to right **red, green, blue, white** on a black background, the text "CYD OK 320x240", a yellow outline 318x176 at y=16 (it overlaps the bars), a cyan outline 320x48 touching the bottom edge, landscape orientation, backlight on.

If wrong, fix with these rebuilds (flash after each):
- Blank/white screen: `DISPLAY_DRIVER=ST7789_DRIVER tools/build.sh`
- Colours swapped (red shows as blue): `EXTRA_FLAGS="-DTFT_RGB_ORDER=TFT_BGR" tools/build.sh`
- Colours inverted: `EXTRA_FLAGS="-DTFT_INVERSION_ON=1" tools/build.sh`
- Text mirrored/upside-down: change `setRotation(1)` to `3`, and remember it for Task 11.

Write the working flags into `tools/build.sh` (edit `DRIVER` default or append to `FLAGS`) so every later build uses them.

- [ ] **Step 6: Commit**

```bash
tools/commit.sh "feat: display bring-up sketch and verified TFT build flags"
```

---

### Task 10: Renderer and replay link (terminal view without Wi-Fi)

**Files:**
- Create: `src/link.h`, `src/replaylink.h`, `src/replaylink.cpp`, `src/render.h`, `src/render.cpp`, `tools/bin2h.py`, `src/capture_data.h` (generated)
- Modify: `cyd-bitpirate-monitor.ino`

**Interfaces:**
- Consumes: `TermBuf`, `AnsiParser` (Tasks 4-5), `ByteRing` (Task 3), `TFT_eSPI`.
- Produces:
  ```cpp
  enum class LinkState : uint8_t { Searching, Connecting, Connected, Lost };
  class Link { virtual void begin(); virtual void loop(); virtual LinkState state() const; virtual bool send(const uint8_t*, size_t); };
  class ReplayLink : public Link { ReplayLink(ByteRing& rx, const uint8_t* data, size_t len, bool loop); };
  class Renderer { explicit Renderer(TFT_eSPI&); void setVisibleRows(int n); void invalidate(); void draw(const TermBuf&, bool cursorOn); };
  ```

- [ ] **Step 1: Create `src/link.h`**

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

enum class LinkState : uint8_t { Searching, Connecting, Connected, Lost };

class Link {
public:
  virtual ~Link() {}
  virtual void begin() = 0;
  virtual void loop() = 0;                                  // called repeatedly from the link task
  virtual LinkState state() const = 0;
  virtual bool send(const uint8_t* d, size_t n) = 0;        // queues bytes; false (and drops them) if not connected
};
```

- [ ] **Step 2: Create `tools/bin2h.py` and generate the capture header**

```python
#!/usr/bin/env python3
"""Usage: bin2h.py IN.bin OUT.h NAME"""
import sys
src, dst, name = sys.argv[1:4]
d = open(src, "rb").read()
with open(dst, "w") as f:
    f.write("#pragma once\n#include <stddef.h>\n#include <stdint.h>\n")
    f.write(f"static const uint8_t {name}[] = {{\n")
    for i in range(0, len(d), 16):
        f.write("  " + ", ".join(f"0x{b:02x}" for b in d[i:i + 16]) + ",\n")
    f.write("};\n")
    f.write(f"static const size_t {name}_LEN = {len(d)};\n")
```

Run: `python3 -I tools/bin2h.py test/captures/s3_session.bin src/capture_data.h CAPTURE`
Expected: `src/capture_data.h` exists and is about 25 KB.

- [ ] **Step 3: Create `src/replaylink.h` and `src/replaylink.cpp`**

`src/replaylink.h`:
```cpp
#pragma once
#include "bytering.h"
#include "config.h"
#include "link.h"

// Plays a captured byte stream into the rx queue (32 bytes / 20 ms) and echoes whatever the UI "sends"
// back into it, so the whole display path can be exercised without Wi-Fi or the S3.
class ReplayLink : public Link {
public:
  ReplayLink(ByteRing& rx, const uint8_t* data, size_t len, bool loop)
    : rx_(rx), tx_(TX_RING_BYTES), data_(data), len_(len), loop_(loop) {}
  void begin() override { state_ = LinkState::Connected; }
  void loop() override;
  LinkState state() const override { return state_; }
  bool send(const uint8_t* d, size_t n) override { return tx_.push(d, n) == n; }
private:
  ByteRing& rx_;
  ByteRing tx_;
  const uint8_t* data_; size_t len_; bool loop_;
  size_t pos_ = 0; uint32_t last_ = 0;
  LinkState state_ = LinkState::Searching;
};
```

`src/replaylink.cpp`:
```cpp
#include "replaylink.h"
#include <Arduino.h>

void ReplayLink::loop() {
  uint32_t now = millis();
  if (now - last_ < 20) return;
  last_ = now;

  uint8_t b[32]; size_t n;
  while ((n = tx_.pop(b, sizeof b)) > 0) {
    for (size_t i = 0; i < n; i++) {
      if (b[i] == '\r') rx_.push((const uint8_t*)"\r\n", 2);
      else if (b[i] == BACKSPACE_BYTE) rx_.push((const uint8_t*)"\b \b", 3);
      else rx_.push(&b[i], 1);
    }
  }

  if (pos_ < len_) {
    size_t take = len_ - pos_ < 32 ? len_ - pos_ : 32;
    rx_.push(data_ + pos_, take);
    pos_ += take;
  } else if (loop_) {
    pos_ = 0;
  }
}
```

- [ ] **Step 4: Create `src/render.h` and `src/render.cpp`**

`src/render.h`:
```cpp
#pragma once
#include <TFT_eSPI.h>
#include "termbuf.h"

// Draws the TermBuf's visible window. Keeps a shadow of what is on screen and only redraws cells that changed.
class Renderer {
public:
  explicit Renderer(TFT_eSPI& tft) : tft_(tft) { invalidate(); }
  void setVisibleRows(int n);            // terminal rows shown; fewer than ROWS while an overlay is open
  void invalidate();                     // redraw everything on the next draw()
  void draw(const TermBuf& tb, bool cursorOn);
private:
  struct Shown { char ch; uint8_t color; bool inv; };
  TFT_eSPI& tft_;
  Shown shown_[ROWS][COLS];
  int vis_ = ROWS;
};
```

`src/render.cpp`:
```cpp
#include "render.h"

namespace {
// VGA-style 16-colour palette in RGB565; index 7 is the default text colour.
const uint16_t PALETTE[16] = {0x0000, 0xA800, 0x0540, 0xAAA0, 0x0015, 0xA815, 0x0555, 0xAD55,
                              0x52AA, 0xFAAA, 0x57EA, 0xFFEA, 0x52BF, 0xFABF, 0x5FFF, 0xFFFF};
}

void Renderer::invalidate() {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) shown_[r][c] = Shown{0, 0, false};   // ch 0 never matches a real cell
}

void Renderer::setVisibleRows(int n) {
  if (n < 1) n = 1;
  if (n > ROWS) n = ROWS;
  if (n != vis_) { vis_ = n; invalidate(); }
}

void Renderer::draw(const TermBuf& tb, bool cursorOn) {
  int curRow = tb.cursorVisible() ? tb.cursorRow() : -1;
  int curCol = tb.cursorCol();
  int base = 0;                                   // first terminal row shown
  if (vis_ < ROWS && curRow >= 0) {               // keep the cursor's row in the window when an overlay is open
    base = curRow - (vis_ - 1);
    if (base < 0) base = 0;
    if (base > ROWS - vis_) base = ROWS - vis_;
  }
  for (int sr = 0; sr < vis_; sr++) {
    int tr = base + sr;
    for (int c = 0; c < COLS; c++) {
      Cell cell = tb.cellAt(tr, c);
      bool inv = cursorOn && tr == curRow && c == curCol;
      Shown want{cell.ch, cell.color, inv};
      Shown& have = shown_[sr][c];
      if (have.ch == want.ch && have.color == want.color && have.inv == want.inv) continue;
      have = want;
      uint16_t fg = PALETTE[cell.color & 15], bg = 0x0000;
      if (inv) { uint16_t t = fg; fg = bg; bg = t; }
      tft_.drawChar(c * CHAR_W, TERM_Y + sr * CHAR_H, cell.ch, fg, bg, 1);
    }
  }
}
```
(The `drawChar(x, y, c, color, bg, size)` overload draws the 6x8 GLCD font and fills the background.)

- [ ] **Step 5: Replace `cyd-bitpirate-monitor.ino` with the replay viewer**

```cpp
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
```

- [ ] **Step 6: Build and flash**

```bash
tools/build.sh && PORT=/dev/ttyUSB0 tools/flash.sh
```
Expected: builds; `Sketch uses` summary; flashes.

- [ ] **Step 7: Verify on the device**

Expected on screen: the captured session plays in: the `HIZ>` prompt, then the "Help Shell" menu text, then the full command list; text is crisp 6x8 light grey on black, no wrapped garbage, the block cursor blinks at the end. Over serial (`python3 -I -c "import serial;s=serial.Serial('/dev/ttyUSB0',115200,timeout=12);print(s.read(200))"`): a `heap free=... min=...` line. Record the free heap value; it must stay above 100000 bytes (this is the budget Task 14 builds on).

- [ ] **Step 8: Commit**

```bash
tools/commit.sh "feat: renderer and replay link; terminal view verified on CYD"
```

---

### Task 11: Touch input and calibration screen

**Files:**
- Create: `src/input.h`, `src/input.cpp`
- Modify: `cyd-bitpirate-monitor.ino`

**Interfaces:**
- Consumes: `TouchCal`, `DEFAULT_TOUCH_CAL`, `mapAxis` (Task 7), `TFT_eSPI`.
- Produces:
  ```cpp
  struct TouchSample { bool pressed; int x, y; };       // screen coordinates, landscape 320x240
  class TouchInput {
    void begin();                       // touch SPI bus + load calibration from NVS
    TouchSample read();
    bool hasCalibration() const;
    void runCalibration(TFT_eSPI& tft); // blocking, stores to NVS
  };
  ```

- [ ] **Step 1: Create `src/input.h`**

```cpp
#pragma once
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "calib.h"

struct TouchSample { bool pressed; int x, y; };

class TouchInput {
public:
  void begin();
  TouchSample read();
  bool hasCalibration() const { return saved_; }
  void runCalibration(TFT_eSPI& tft);
private:
  bool readRaw(int& rx, int& ry);
  void load();
  void save();
  SPIClass spi_{VSPI};
  XPT2046_Touchscreen ts_{33, 36};      // CS, IRQ
  TouchCal cal_ = DEFAULT_TOUCH_CAL;
  bool saved_ = false;
};
```

- [ ] **Step 2: Create `src/input.cpp`**

```cpp
#include "input.h"
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

void TouchInput::begin() {
  spi_.begin(25, 39, 32, 33);           // CLK, MISO, MOSI, CS (the CYD's touch bus)
  ts_.begin(spi_);
  ts_.setRotation(1);
  load();
}

bool TouchInput::readRaw(int& rx, int& ry) {
  if (!ts_.touched()) return false;
  TS_Point p = ts_.getPoint();
  rx = p.x; ry = p.y;
  return true;
}

TouchSample TouchInput::read() {
  int rx, ry;
  if (!readRaw(rx, ry)) return TouchSample{false, 0, 0};
  return TouchSample{true, mapAxis(cal_.x, rx, 0, SCREEN_W - 1), mapAxis(cal_.y, ry, 0, SCREEN_H - 1)};
}

void TouchInput::load() {
  Preferences p;
  if (!p.begin("touch", true)) return;               // storage unavailable: keep the defaults
  if (p.isKey("ax")) {
    cal_.x = AxisCal{p.getInt("ax"), 20, p.getInt("bx"), 300};
    cal_.y = AxisCal{p.getInt("ay"), 20, p.getInt("by"), 220};
    saved_ = true;
  }
  p.end();
}

void TouchInput::save() {
  Preferences p;
  if (!p.begin("touch", false)) return;
  p.putInt("ax", cal_.x.raw1); p.putInt("bx", cal_.x.raw2);
  p.putInt("ay", cal_.y.raw1); p.putInt("by", cal_.y.raw2);
  p.end();
  saved_ = true;
}

static void waitRelease(XPT2046_Touchscreen& ts) {
  uint32_t quiet = millis();
  while (millis() - quiet < 100) { if (ts.touched()) quiet = millis(); delay(5); }
}

void TouchInput::runCalibration(TFT_eSPI& tft) {
  const int tx[2] = {20, 300}, ty[2] = {20, 220};
  int rawX[2], rawY[2];
  for (int i = 0; i < 2; i++) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("Tap the + (hold briefly)", SCREEN_W / 2, 110, 2);
    tft.drawLine(tx[i] - 10, ty[i], tx[i] + 10, ty[i], TFT_YELLOW);
    tft.drawLine(tx[i], ty[i] - 10, tx[i], ty[i] + 10, TFT_YELLOW);
    long sx = 0, sy = 0; int n = 0;
    while (n < 8) {                                   // average 8 samples while pressed
      int rx, ry;
      if (readRaw(rx, ry)) { sx += rx; sy += ry; n++; delay(15); }
      else { sx = sy = 0; n = 0; delay(5); }
    }
    rawX[i] = (int)(sx / n); rawY[i] = (int)(sy / n);
    waitRelease(ts_);
  }
  cal_.x = AxisCal{rawX[0], tx[0], rawX[1], tx[1]};
  cal_.y = AxisCal{rawY[0], ty[0], rawY[1], ty[1]};
  save();
  tft.fillScreen(TFT_BLACK);
}
```

- [ ] **Step 3: Replace the sketch with a touch test**

```cpp
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "src/input.h"

static TFT_eSPI tft;
static TouchInput touch;

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
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
```

- [ ] **Step 4: Build, flash, calibrate**

```bash
tools/build.sh && PORT=/dev/ttyUSB0 tools/flash.sh
```
On the device: tap the two yellow crosses when asked, using a fingernail or stylus.

- [ ] **Step 5: Verify accuracy**

Expected: after calibration, touching each corner of the screen puts the green dot within about 10 pixels of the finger; the printed coordinates are near `(0..15, 0..15)` top-left and `(305..319, 225..239)` bottom-right, increasing x to the right and y downward. Power-cycle: calibration is not asked again (stored in NVS). Hold BOOT while resetting: calibration screen returns. If x and y are swapped (a dot at the wrong place when you drag along one axis), set `ts_.setRotation(3)` in `begin()` and re-test; record the working value.

- [ ] **Step 6: Commit**

```bash
tools/commit.sh "feat: touch input with two-point calibration stored in NVS"
```

---

### Task 12: UI (status bar, buttons, keyboard, popup, gestures) on the replay link

**Files:**
- Create: `src/ui.h`, `src/ui.cpp`
- Modify: `cyd-bitpirate-monitor.ino`

**Interfaces:**
- Consumes: `TermBuf`, `Renderer`, `Link`/`LinkState`, `GestureEvent`, `Keyboard`/`Key`, `CMD_BUTTONS`/`buttonAt`/`popupItemRect`/`popupItemAt`/`OVERLAY_TOP`/`OVERLAY_ROWS`, `parsePromptMode`, `BACKSPACE_BYTE`, `ENTER_STR`.
- Produces: `class Ui { Ui(TFT_eSPI&, TermBuf&, Renderer&, Link&); void begin(); void handle(const GestureEvent&); void update(uint32_t nowMs); }`.

- [ ] **Step 1: Create `src/ui.h`**

```cpp
#pragma once
#include <TFT_eSPI.h>
#include "gesture.h"
#include "keyboard.h"
#include "layout.h"
#include "link.h"
#include "render.h"
#include "termbuf.h"

class Ui {
public:
  Ui(TFT_eSPI& tft, TermBuf& term, Renderer& rend, Link& link)
    : tft_(tft), term_(term), rend_(rend), link_(link), kb_(0, OVERLAY_TOP, SCREEN_W, SCREEN_H - OVERLAY_TOP) {}
  void begin();
  void handle(const GestureEvent& e);
  void update(uint32_t nowMs);

private:
  void onTap(int x, int y);
  void onLongPress(int x, int y);
  void doKey(Key k);
  void sendText(const char* s);
  void sendByte(uint8_t b);
  void togglePause();
  void openKeyboard();
  void openPopup(int btn);
  void closeOverlay();
  void drawStatus(bool force);
  void drawButtons();
  void drawKeyboard();
  void drawPopup();

  TFT_eSPI& tft_;
  TermBuf& term_;
  Renderer& rend_;
  Link& link_;
  Keyboard kb_;
  bool kbOpen_ = false;
  int popupBtn_ = -1;
  char mode_[16] = {0};
  int shownLink_ = -1, shownRight_ = -1;
  char shownMode_[16] = {0};
  uint32_t lastDraw_ = 0;
};
```

- [ ] **Step 2: Create `src/ui.cpp`**

```cpp
#include "ui.h"
#include <string.h>
#include "prompt.h"

namespace {
constexpr uint16_t C_BG = 0x0000, C_BAR = 0x18E3, C_BTN = 0x2945, C_KEY = 0x39E7, C_POP = 0x2A6F;
constexpr uint16_t C_TEXT = 0xFFFF, C_OK = 0x07E0, C_WARN = 0xFFE0, C_BAD = 0xF800;
}

void Ui::begin() {
  tft_.fillScreen(C_BG);
  drawButtons();
  drawStatus(true);
  rend_.invalidate();
}

// ---- sending -------------------------------------------------------------------------------

void Ui::sendByte(uint8_t b) {
  if (link_.state() != LinkState::Connected) return;      // spec: keys typed while disconnected are dropped
  if (!term_.frozen()) term_.scrollToLive();
  link_.send(&b, 1);
}

void Ui::sendText(const char* s) {
  if (link_.state() != LinkState::Connected) return;
  if (!term_.frozen()) term_.scrollToLive();
  link_.send((const uint8_t*)s, strlen(s));
}

// ---- events --------------------------------------------------------------------------------

void Ui::handle(const GestureEvent& e) {
  switch (e.type) {
    case GestureType::Tap: onTap(e.x, e.y); break;
    case GestureType::LongPress: onLongPress(e.x, e.y); break;
    case GestureType::Scroll: {
      int bottom = (kbOpen_ || popupBtn_ >= 0) ? OVERLAY_TOP : BTN_Y;
      if (e.y >= TERM_Y && e.y < bottom) term_.scrollBy(e.lines);
      break;
    }
    default: break;
  }
}

void Ui::onTap(int x, int y) {
  if (popupBtn_ >= 0) {
    int it = popupItemAt(popupBtn_, x, y);
    if (it >= 0) {
      const CmdItem& ci = CMD_BUTTONS[popupBtn_].items[it];
      sendText(ci.text);
      if (ci.enter) sendText(ENTER_STR);
    }
    closeOverlay();
    return;
  }
  if (y < STATUS_H) { togglePause(); return; }
  if (kbOpen_) { doKey(kb_.hit(x, y)); return; }
  int b = buttonAt(x, y);
  if (b < 0) return;
  const CmdButton& cb = CMD_BUTTONS[b];
  switch (cb.kind) {
    case BtnKind::Command:
      sendText(cb.items[0].text);
      if (cb.items[0].enter) sendText(ENTER_STR);
      break;
    case BtnKind::Enter: sendText(ENTER_STR); break;
    case BtnKind::CtrlC: sendByte(0x03); break;
    case BtnKind::Keyboard: openKeyboard(); break;
  }
}

void Ui::onLongPress(int x, int y) {
  if (popupBtn_ >= 0 || kbOpen_) return;
  int b = buttonAt(x, y);
  if (b >= 0) {
    if (CMD_BUTTONS[b].kind == BtnKind::Command && CMD_BUTTONS[b].nItems >= 2) openPopup(b);
    return;
  }
  if (y >= TERM_Y && y < BTN_Y) { term_.clearScreen(); rend_.invalidate(); }
}

void Ui::doKey(Key k) {
  switch (k.type) {
    case KeyType::Char: case KeyType::Space: sendByte((uint8_t)k.ch); break;
    case KeyType::Backspace: sendByte(BACKSPACE_BYTE); break;
    case KeyType::Enter: sendText(ENTER_STR); break;
    case KeyType::CtrlC: sendByte(0x03); break;
    case KeyType::Page: kb_.setPage(1 - kb_.page()); drawKeyboard(); break;
    case KeyType::Hide: closeOverlay(); break;
    default: break;
  }
}

void Ui::togglePause() {
  term_.setFrozen(!term_.frozen());
  drawStatus(false);
}

// ---- overlays ------------------------------------------------------------------------------

void Ui::openKeyboard() {
  kbOpen_ = true;
  rend_.setVisibleRows(OVERLAY_ROWS);
  drawKeyboard();
}

void Ui::openPopup(int btn) {
  popupBtn_ = btn;
  rend_.setVisibleRows(OVERLAY_ROWS);
  drawPopup();
}

void Ui::closeOverlay() {
  kbOpen_ = false;
  popupBtn_ = -1;
  tft_.fillRect(0, OVERLAY_TOP, SCREEN_W, BTN_Y - OVERLAY_TOP, C_BG);
  drawButtons();
  rend_.setVisibleRows(ROWS);                              // also invalidates, so the terminal repaints
}

// ---- drawing -------------------------------------------------------------------------------

void Ui::drawButtons() {
  tft_.setTextDatum(MC_DATUM);
  for (int i = 0; i < BTN_COUNT; i++) {
    Rect r = buttonRect(i);
    tft_.fillRect(r.x, r.y, r.w, r.h, C_BG);
    tft_.fillRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 4, C_BTN);
    tft_.setTextColor(C_TEXT, C_BTN);
    tft_.drawString(CMD_BUTTONS[i].label, r.x + r.w / 2, r.y + r.h / 2, 2);
  }
}

void Ui::drawKeyboard() {
  tft_.fillRect(0, OVERLAY_TOP, SCREEN_W, SCREEN_H - OVERLAY_TOP, C_BG);
  tft_.setTextDatum(MC_DATUM);
  for (int i = 0; i < kb_.keyCount(); i++) {
    const KeyRect& k = kb_.key(i);
    tft_.fillRoundRect(k.x + 1, k.y + 1, k.w - 2, k.h - 2, 3, C_KEY);
    tft_.setTextColor(C_TEXT, C_KEY);
    tft_.drawString(k.label, k.x + k.w / 2, k.y + k.h / 2, 2);
  }
}

void Ui::drawPopup() {
  tft_.fillRect(0, OVERLAY_TOP, SCREEN_W, BTN_Y - OVERLAY_TOP, C_BG);
  tft_.setTextDatum(ML_DATUM);
  const CmdButton& cb = CMD_BUTTONS[popupBtn_];
  for (int i = 0; i < cb.nItems; i++) {
    Rect r = popupItemRect(popupBtn_, i);
    tft_.fillRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, C_POP);
    tft_.setTextColor(C_TEXT, C_POP);
    tft_.drawString(cb.items[i].text, r.x + 6, r.y + r.h / 2, 2);
  }
}

void Ui::drawStatus(bool force) {
  const char* link; uint16_t lc;
  switch (link_.state()) {
    case LinkState::Connected: link = "ONLINE"; lc = C_OK; break;
    case LinkState::Connecting: link = "LINKING"; lc = C_WARN; break;
    case LinkState::Lost: link = "LOST"; lc = C_BAD; break;
    default: link = "WIFI..."; lc = C_WARN; break;
  }
  int linkCode = (int)link_.state();
  int rightCode = term_.frozen() ? 2 : (term_.viewOffset() > 0 ? 1 : 0);
  const char* mode = mode_[0] ? mode_ : "-";
  if (!force && linkCode == shownLink_ && rightCode == shownRight_ && strcmp(mode, shownMode_) == 0) return;
  shownLink_ = linkCode; shownRight_ = rightCode;
  strncpy(shownMode_, mode, sizeof shownMode_ - 1);

  tft_.fillRect(0, 0, SCREEN_W, STATUS_H, C_BAR);
  tft_.setTextDatum(ML_DATUM); tft_.setTextColor(lc, C_BAR);     tft_.drawString(link, 4, STATUS_H / 2, 1);
  tft_.setTextDatum(MC_DATUM); tft_.setTextColor(C_TEXT, C_BAR); tft_.drawString(mode, SCREEN_W / 2, STATUS_H / 2, 1);
  tft_.setTextDatum(MR_DATUM); tft_.setTextColor(C_WARN, C_BAR);
  tft_.drawString(rightCode == 2 ? "PAUSED" : (rightCode == 1 ? "SCROLL" : ""), SCREEN_W - 4, STATUS_H / 2, 1);
}

void Ui::update(uint32_t now) {
  char line[COLS + 1], m[16];
  term_.currentLineText(line, sizeof line);
  if (parsePromptMode(line, m, sizeof m)) strncpy(mode_, m, sizeof mode_ - 1);   // keep last mode when no prompt shows
  drawStatus(false);
  if (now - lastDraw_ >= 33) {
    rend_.draw(term_, (now / 500) % 2 == 0 && term_.cursorVisible());
    lastDraw_ = now;
  }
}
```

- [ ] **Step 3: Replace the sketch with the full replay build**

```cpp
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <string.h>
#include "src/ansi.h"
#include "src/bytering.h"
#include "src/capture_data.h"
#include "src/config.h"
#include "src/gesture.h"
#include "src/input.h"
#include "src/render.h"
#include "src/replaylink.h"
#include "src/termbuf.h"
#include "src/ui.h"

static TFT_eSPI tft;
static TermBuf term;
static AnsiParser ansi(term);
static ByteRing rx(RX_RING_BYTES);
static ReplayLink s3link(rx, CAPTURE, CAPTURE_LEN, false);
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
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  touch.begin();
  pinMode(0, INPUT_PULLUP);                                   // hold BOOT at power-up to recalibrate
  if (!touch.hasCalibration() || digitalRead(0) == LOW) touch.runCalibration(tft);
  ui.begin();
  s3link.begin();
  xTaskCreatePinnedToCore(linkTask, "link", 4096, nullptr, 1, nullptr, 0);
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
```

- [ ] **Step 4: Build and flash**

```bash
tools/build.sh && PORT=/dev/ttyUSB0 tools/flash.sh
```
Expected: builds and flashes.

- [ ] **Step 5: Verify on the device (replay link: typed keys are echoed locally)**

Expected, in this order:
1. Status bar: green `ONLINE`, centre shows `HIZ` once the prompt has played, right side empty.
2. Tap `help`: the text `help` appears after the prompt and a newline follows. Tap `Enter`: a newline.
3. Tap `KB`: the keyboard slides up over the buttons and lower terminal; the prompt line stays visible just above it. Type `abc`; the letters appear at the prompt. `<-` removes one. `123` swaps to digits/symbols, `abc` swaps back. `KB` (inside the keyboard) hides it; buttons and the terminal repaint with no leftover keyboard pixels.
4. Long-press `sniff`: a two-item popup (`sniff`, `sniff raw`) appears above the buttons; tap `sniff raw` and it types `sniff raw` + newline; tapping elsewhere just closes it.
5. Drag the terminal down: older text appears and the status bar shows `SCROLL`; typing jumps back to live.
6. Tap the status bar: `PAUSED`; the text view stops; tap again to resume.
7. Long-press the terminal area: it clears.
Anything that fails here is fixed in this task before moving on.

- [ ] **Step 6: Commit**

```bash
tools/commit.sh "feat: UI with status bar, command buttons, keyboard, popup and gestures"
```

---

### Task 13: Capture the web CLI protocol from the S3 (gate for Task 14)

**Files:**
- Modify: `docs/protocol.md`
- Create: `src/secrets.h.example`

**Interfaces:**
- Produces: in `docs/protocol.md`, the facts Task 14 needs: SSID, password, host, port, path, transport (WebSocket or other), frame type (text/binary), whether the S3 echoes typed characters, and whether its output uses `\r\n`.

This task changes the S3's Wi-Fi state and temporarily moves a computer onto the S3's network. Tell the user before step 4.

- [ ] **Step 1: Read the Wi-Fi help on the S3 (USB)**

```bash
python3 -I - <<'E'
import serial, time, re
s = serial.Serial("/dev/ttyACM0", 115200, timeout=0.3)
def tx(c, t=2.0):
    s.reset_input_buffer(); s.write(c.encode() + b"\r"); b = b""; e = time.time() + t
    while time.time() < e: b += s.read(4096)
    return re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", b.decode("utf-8", "replace"))
tx("\x03"); print(tx("mode wifi", 3))
print(tx("help", 3)); print(tx("14", 3))     # WIFI entry in the help menu
E
```
Expected: the Wi-Fi mode's command list. Find the command that starts the S3's own access point (hotspot). If the menu index for WIFI is not 14, pick the entry labelled WIFI from the printed menu. If no access-point command exists, stop: use the "join an existing network" commands instead, and amend the spec's hotspot decision with the user.

- [ ] **Step 2: Start the hotspot and note its credentials**

Run the access-point command found in step 1 (the wiki says `connect` is used for joining a network; the AP command appears in the help list). Write the SSID, password (if any) and IP (expected `192.168.4.1`) into `docs/protocol.md`.

- [ ] **Step 3: Check how this computer connects to the network**

```bash
nmcli -t -f DEVICE,TYPE,STATE device
```
If the only connected device is Wi-Fi, ask the user before step 4 (it will drop their normal connection while testing). If an Ethernet device is connected, continue.

- [ ] **Step 4: Join the S3 hotspot and fetch the page**

```bash
nmcli device wifi connect "<SSID>" password "<password>"      # omit password if open
curl -sv --max-time 5 http://192.168.4.1/ -o /tmp/claude-1000/-home-kratos/f6f56247-bc9e-4989-8bac-e7f22501c638/scratchpad/s3_index.html 2>&1 | head -20
grep -noE "WebSocket\([^)]*\)|ws://[^\"' ]+|fetch\([^)]*\)|EventSource\([^)]*\)" /tmp/claude-1000/-home-kratos/f6f56247-bc9e-4989-8bac-e7f22501c638/scratchpad/s3_index.html | head
```
Expected: HTTP 200, and a `WebSocket(...)` or `ws://...` line giving the path (for example `/ws`). Record host, port and path in `docs/protocol.md`. If the page uses `fetch`/`EventSource` instead of a WebSocket, **stop**: the `WsLink` design in Task 14 does not apply; amend spec §3 `link` and this plan with the user.

- [ ] **Step 5: Talk to the WebSocket from this computer**

```bash
python3 -m venv /tmp/claude-1000/-home-kratos/f6f56247-bc9e-4989-8bac-e7f22501c638/scratchpad/wsenv
/tmp/claude-1000/-home-kratos/f6f56247-bc9e-4989-8bac-e7f22501c638/scratchpad/wsenv/bin/pip install -q websocket-client
/tmp/claude-1000/-home-kratos/f6f56247-bc9e-4989-8bac-e7f22501c638/scratchpad/wsenv/bin/python - <<'E'
import websocket, time
ws = websocket.create_connection("ws://192.168.4.1:80/ws", timeout=3)    # use the host/port/path from step 4
def rd(t=2.5):
    end = time.time() + t; out = []
    ws.settimeout(0.5)
    while time.time() < end:
        try: out.append(ws.recv())
        except Exception: pass
    return out
print("on connect:", rd())
ws.send("\r");           print("after CR:", rd())
ws.send("ab");           print("echo of 'ab':", rd(1.5))
ws.send("\x7f");         print("after DEL:", rd(1.5))
ws.send("\x03");         rd(1)
E
```
Expected: frames printed as `str` (text frames) or `bytes` (binary). Record in `docs/protocol.md`: frame type (`str` -> text, `bytes` -> binary); whether `ab` is echoed back (yes/no); whether the prompt text uses `\r\n`; whether DEL (0x7F) edits the line (redraw shows `a`).

- [ ] **Step 6: Decide the gate**

- All of these must hold: transport is a WebSocket; typed characters are echoed by the S3; Enter `\r` works; DEL or `\b` edits the line. Then continue.
- If the S3 does **not** echo typed characters: stop. Add a `LOCAL_ECHO` option to `Ui::sendByte/sendText` (feed typed bytes to the parser) as a new task and amend the spec before Task 14.
- If the Backspace byte differs from `BACKSPACE_BYTE`, fix `src/config.h` now.

- [ ] **Step 7: Return this computer to its normal network, then record the secrets template**

```bash
nmcli connection up "$(nmcli -t -f NAME,TYPE connection show | grep -m1 wifi | cut -d: -f1)"   # or reconnect the usual network in the GUI
```
Create `src/secrets.h.example` (commit this) and `src/secrets.h` (gitignored, real values):
```cpp
#pragma once
#define WS_SSID "<S3 hotspot SSID>"
#define WS_PASS "<password or empty string>"
#define WS_HOST "192.168.4.1"
#define WS_PORT 80
#define WS_PATH "/ws"
#define WS_USE_TEXT 1        // 1 = text frames, 0 = binary frames (from step 5)
```
Use the measured values in `secrets.h`; in `secrets.h.example` keep placeholder-style values.

- [ ] **Step 8: Commit**

```bash
tools/commit.sh "docs: web CLI protocol findings from the S3 hotspot"
```

---

### Task 14: WsLink and final integration

**Files:**
- Create: `src/wslink.h`, `src/wslink.cpp`
- Modify: `cyd-bitpirate-monitor.ino`, `tools/build.sh`

**Interfaces:**
- Consumes: `Link`, `ByteRing`, `secrets.h` values, WebSockets library.
- Produces: `class WsLink : public Link { explicit WsLink(ByteRing& rx); }`. Default build uses `WsLink`; `-DREPLAY_MODE=1` still builds the replay variant.

- [ ] **Step 1: Create `src/wslink.h`**

```cpp
#pragma once
#include <WebSocketsClient.h>
#include <atomic>
#include "bytering.h"
#include "config.h"
#include "link.h"

class WsLink : public Link {
public:
  explicit WsLink(ByteRing& rx) : rx_(rx), tx_(TX_RING_BYTES) {}
  void begin() override;
  void loop() override;
  LinkState state() const override { return (LinkState)state_.load(); }
  bool send(const uint8_t* d, size_t n) override;
private:
  ByteRing& rx_;
  ByteRing tx_;
  WebSocketsClient ws_;
  std::atomic<uint8_t> state_{(uint8_t)LinkState::Searching};
  bool wsStarted_ = false, wasConnected_ = false;
  uint32_t lastWifiTry_ = 0;
};
```

- [ ] **Step 2: Create `src/wslink.cpp`**

```cpp
#include "wslink.h"
#include <Arduino.h>
#include <WiFi.h>
#include "secrets.h"

void WsLink::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);             // loop() owns reconnects so the reported state stays accurate
  ws_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
      case WStype_CONNECTED:
        wasConnected_ = true;
        state_ = (uint8_t)LinkState::Connected;
        break;
      case WStype_DISCONNECTED:
        if (state_.load() == (uint8_t)LinkState::Connected) state_ = (uint8_t)LinkState::Lost;
        break;
      case WStype_TEXT:
      case WStype_BIN:
        rx_.push(payload, length);
        break;
      default: break;
    }
  });
  ws_.setReconnectInterval(2000);
  ws_.enableHeartbeat(15000, 3000, 2);      // ping every 15 s; 2 missed pongs = dead link
  lastWifiTry_ = millis() - 5000;           // try immediately
}

void WsLink::loop() {
  if (WiFi.status() != WL_CONNECTED) {
    state_ = (uint8_t)(wasConnected_ ? LinkState::Lost : LinkState::Searching);
    if (wsStarted_) { ws_.disconnect(); wsStarted_ = false; }
    uint8_t junk[32]; while (tx_.pop(junk, sizeof junk) > 0) {}
    if (millis() - lastWifiTry_ >= 5000) {
      WiFi.disconnect();
      WiFi.begin(WS_SSID, WS_PASS);
      lastWifiTry_ = millis();
    }
    return;
  }
  if (!wsStarted_) {
    ws_.begin(WS_HOST, WS_PORT, WS_PATH);
    wsStarted_ = true;
    state_ = (uint8_t)LinkState::Connecting;
  }
  ws_.loop();

  uint8_t b[64]; size_t n;
  if (state_.load() == (uint8_t)LinkState::Connected) {
    while ((n = tx_.pop(b, sizeof b)) > 0) {
      if (WS_USE_TEXT) ws_.sendTXT(b, n); else ws_.sendBIN(b, n);
    }
  } else {
    while (tx_.pop(b, sizeof b) > 0) {}     // never deliver stale keystrokes after a reconnect
  }
}

bool WsLink::send(const uint8_t* d, size_t n) {
  if (state() != LinkState::Connected) return false;
  return tx_.push(d, n) == n;
}
```

- [ ] **Step 3: Edit the sketch** (`cyd-bitpirate-monitor.ino`)

Replace
```cpp
#include "src/capture_data.h"
```
with
```cpp
#ifdef REPLAY_MODE
#include "src/capture_data.h"
#include "src/replaylink.h"
#else
#include "src/wslink.h"
#endif
```
Remove the line `#include "src/replaylink.h"` (now inside the `#ifdef`). Replace
```cpp
static ReplayLink s3link(rx, CAPTURE, CAPTURE_LEN, false);
```
with
```cpp
#ifdef REPLAY_MODE
#ifdef REPLAY_LOOP
static ReplayLink s3link(rx, CAPTURE, CAPTURE_LEN, true);
#else
static ReplayLink s3link(rx, CAPTURE, CAPTURE_LEN, false);
#endif
#else
static WsLink s3link(rx);
#endif
```
Replace the task stack size `4096` in `xTaskCreatePinnedToCore(linkTask, "link", 4096, ...)` with `8192` (Wi-Fi and the WebSocket client need more stack).

- [ ] **Step 4: Build both variants**

```bash
tools/build.sh                                  # real link
EXTRA_FLAGS="-DREPLAY_MODE=1" tools/build.sh    # replay variant still builds
tools/build.sh                                  # leave build/ holding the real-link image
```
Expected: both compile. If the real build fails to find `secrets.h`, Task 13 step 7 was skipped.

- [ ] **Step 5: Flash and connect**

Make sure the S3 hotspot is running (Task 13). Then:
```bash
PORT=/dev/ttyUSB0 tools/flash.sh
```
Expected on the device: status `WIFI...` (yellow) for a few seconds, then `LINKING`, then `ONLINE` (green); a `[link up]` line appears in the terminal; the S3's banner/prompt follows and the status bar centre shows `HIZ`.

- [ ] **Step 6: Smoke test against the real S3**

Tap `help` then `Enter` (or type it on the keyboard): the S3's Help Shell menu appears; tap `KB`, type `1`, `Enter`: the full command list appears. Expected: text identical in content to the USB session, no stair-stepping, emoji menu items show `?`.

- [ ] **Step 7: Commit**

```bash
tools/commit.sh "feat: WebSocket link to the Bit Pirate hotspot, real-link build"
```

---

### Task 15: Hardware checklist, soak, finish

**Files:**
- Modify: `docs/superpowers/specs/2026-10-08-cyd-bitpirate-terminal-design.md` (only if a result contradicts it), `docs/protocol.md` (prompt notes)

**Interfaces:**
- Consumes: the real-link build from Task 14 and the replay build.

- [ ] **Step 1: Run the spec's hardware checklist on the real link**

Check each and write PASS/FAIL (with a short note) into a new section "Hardware checklist results" at the end of `docs/protocol.md`:
1. Joins the S3 hotspot (status reaches `ONLINE`).
2. Banner and `HIZ>` prompt render correctly.
3. Typing `help` on the on-screen keyboard works.
4. Each command button sends the right text (`mode`, `help`, `scan`, `read`, `write `, `sniff`, `config`), plus `^C` and `Enter`.
5. Scroll (drag), pause (status-bar tap) and clear (long-press terminal) behave.
6. Power-cycle the S3 (unplug and replug its USB power): the status goes `LOST`, a `[link lost]` marker appears, and it returns to `ONLINE` by itself with `[link up]`, without touching the CYD.
7. Mode tracking: send `mode` then pick `i2c` (or any other mode) on the keyboard. Note what the prompt looks like and whether the status bar shows it. If the real prompt is not `NAME>` (for example it contains extra text), extend `parsePromptMode` with a test case for the observed prompt and re-run `tools/run-tests.sh`.

- [ ] **Step 2: Replay soak (10 minutes at full speed)**

```bash
EXTRA_FLAGS="-DREPLAY_MODE=1 -DREPLAY_LOOP=1" tools/build.sh && PORT=/dev/ttyUSB0 tools/flash.sh
python3 -I -c "
import serial,time
s=serial.Serial('/dev/ttyUSB0',115200,timeout=1)
end=time.time()+600
while time.time()<end:
    l=s.readline().decode(errors='replace').strip()
    if l: print(l)
"
```
Expected: the screen keeps cycling the captured session with no corruption, no reboot (`rst:` or `Guru Meditation` never appears), and the `heap free=... min=...` lines stay flat (min free heap must not decrease steadily; a drop of more than 5 KB over 10 minutes is a leak to fix).

- [ ] **Step 3: Real-link soak (10 minutes)**

Rebuild the real image (`tools/build.sh`, flash). Leave the CYD connected to the S3 for 10 minutes, tapping `help` + `Enter` (and `1` + `Enter` for the full list) about once a minute. Expected: status stays `ONLINE`, no reboot, the heap line stays flat.

- [ ] **Step 4: Final host tests and cleanup**

```bash
tools/run-tests.sh
git status --short
```
Expected: `0 failures`; `git status` shows only intended files (no `build/`, no `src/secrets.h`).

- [ ] **Step 5: Update the spec if any result contradicted it, then commit**

If the checklist or soak changed a design fact (for example a different prompt format, a changed scrollback size, or a different touch rotation), edit the spec to match. Then:
```bash
tools/commit.sh "docs: hardware checklist and soak results"
```

- [ ] **Step 6: Report**

State plainly which checklist items passed and which did not, the measured minimum free heap, and anything deferred to later projects (hex view, SD logging, decoders, radio view).
