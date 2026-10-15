# CYD SD Logging, Viewer and Replay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add manual session recording to the CYD's microSD card (timestamped, both directions), an on-device LOGS browser, a read-only viewer and a timed replay, plus a PC converter.

**Architecture:** Pure, host-tested units (`cbpformat`, `recorder`, `logindex`, `replayer`, `viewerfeed`, layout) sit behind two small interfaces, `FileIO` and `LogStore`, so everything except the SD driver and the screens runs under g++ with an in-memory fake. Hardware units (`SdStore`, `LogsScreen`, UI additions) are verified on the CYD after a hardware gate proves the SD slot works next to the touch panel and Wi-Fi. The recorder drains a queue on its own task so SD latency never blocks touch or rendering.

**Tech Stack:** Arduino framework via arduino-cli (core esp32:esp32 3.3.11, bundled SD/FS/SPI libraries), TFT_eSPI, g++ for host tests, Python 3 for the PC converter.

**Spec:** `docs/superpowers/specs/2026-10-14-cyd-sd-logging-design.md` (builds on project 1, `2026-10-08-cyd-bitpirate-terminal-design.md`)

## Global Constraints

- Project 1 behaviour is unchanged: link, terminal, gestures, calibration, keyboard, popups keep working exactly as shipped.
- Screen is landscape 320x240: status bar 16 px, terminal 176 px (53 cols x 22 rows), button area 48 px (two rows of 24 px).
- Session file: header `CBP1`, then records `u32 ms (little endian, since REC)`, `u8 dir` (0 rx, 1 tx, 2 marker), `u8 len` (1..255), `len` bytes. Files are `/logs/NNNN.cbp`, next free four-digit number.
- Timestamps are milliseconds since REC started; there is no wall-clock time.
- Recording is manual (REC button). Both directions are recorded. Link loss and reconnect are written as markers; recording continues across a link drop.
- All SD writes happen on the recorder task, never in the loop task. A full recorder queue drops bytes and writes a `[rec overflow]` marker; it never blocks.
- Replay is display-only: nothing on the replay path may hold a `Link` or call `send`.
- Viewer shows the last 400 lines of a session (its own `TermBuf`, allocated on LOGS open, freed on close). Older lines of a longer session are not pageable in this project.
- Free heap while recording with the viewer closed must stay at or above 100000 bytes (project 1 target). Flash is 85% used before this project; measure after each hardware task.
- `cbpformat`, `recorder`, `logindex`, `replayer`, `viewerfeed`, `layout` must not include any Arduino header, so `tools/run-tests.sh` compiles them with plain g++.
- Every commit uses `tools/commit.sh "<message>"`.
- If the hardware gate (Task 1) shows the SD slot does not work at all, stop and ask the user.
- Out of scope: on-device text export, wall-clock time, file management beyond delete, Wi-Fi upload, auto-record, hex view, decoders.

## Review Focus

Failure modes the spec implies but a happy-path test would not exercise, most likely first. Each has a test or gate in the task named.

1. **Card pulled or power lost mid-write:** a truncated final record must be ignored and every earlier record still shown, in the index, the viewer and the PC converter (Tasks 2, 4, 10).
2. **Recorder queue overflow:** must never block the producer or split a frame; bytes are dropped, counted, and marked `[rec overflow]` in order (Task 3).
3. **No card, card removed mid-recording, card full:** REC shows an error and the terminal keeps working; recording can be started again (Tasks 3 and 8).
4. **SD writes while touch is polled and Wi-Fi is connected** share the VSPI host and CPU: no corrupt file, no dead touch (Task 1 gate, then Task 11 soak).
5. **Replay edge cases:** a huge idle gap must not stall the replay, pause must not make time jump on resume, out-of-order timestamps in a corrupt file must not stall it (Task 5).
6. **Viewer allocation fails** (low heap): LOGS refuses to open with a message and the live terminal is unaffected (Task 9).

## File Structure

```
src/fileio.h            FileIO interface (sequential read/write/flush/size)       host-tested via MemFile
src/cbpformat.{h,cpp}   header + record writer, CbpReader                          host-tested
src/logstore.h          LogStore interface + LogInfo                               host-tested via MemStore
src/recorder.{h,cpp}    REC state machine, frame queue, all-or-nothing push        host-tested
src/logindex.{h,cpp}    LogEntry list (newest first), fmtElapsed/fmtDuration/fmtSize host-tested
src/replayer.{h,cpp}    timed replay with speed, gap cap, pause                     host-tested
src/viewerfeed.{h,cpp}  RecordSink that renders records into a TermBuf, per-line ms host-tested
src/termbuf.{h,cpp}     + lineAtRow()                                               host-tested
src/layout.{h,cpp}      button grid 6x2 with REC and LOGS                           host-tested
src/sdstore.{h,cpp}     LogStore on the SD card (hardware)
src/logsscreen.{h,cpp}  LOGS list + viewer + replay screen (hardware)
src/ui.{h,cpp}          REC/LOGS buttons, status-bar REC indicator, TX logging (hardware)
cyd-bitpirate-monitor.ino  recorder task, wiring, loop routing (hardware)
tools/sdgate/sdgate.ino SD + touch + Wi-Fi gate sketch (diagnostic)
tools/cbp2txt.py        PC converter; tools/test_cbp2txt.py its tests
test/memfile.h memstore.h  in-memory fakes; test/test_*.cpp
```

---

### Task 1: Spec amendments and the SD hardware gate

**Files:**
- Modify: `docs/superpowers/specs/2026-10-14-cyd-sd-logging-design.md`
- Create: `tools/sdgate/sdgate.ino`
- Modify: `docs/hardware.md`, `src/config.h`

**Interfaces:**
- Produces: `SD_CS`, `SD_SCK`, `SD_MISO`, `SD_MOSI`, `SD_HZ`, `REC_QUEUE_BYTES` constants in `src/config.h`, used by Tasks 8 and 9.

This task needs the user: a FAT32 microSD card in the CYD, tapping the screen, and the S3 hotspot on.

- [ ] **Step 1: Amend the spec (three decisions made while planning)**

Run:

```bash
python3 - <<'EOF'
p = 'docs/superpowers/specs/2026-10-14-cyd-sd-logging-design.md'
s = open(p).read()
def rep(old, new):
    global s
    assert old in s, old
    s = s.replace(old, new, 1)
rep("- LOGS browser (list, open, delete), read-only viewer with timestamp margin",
    "- LOGS browser (list of the newest 20 files, open, delete), read-only viewer; tap a terminal line to see its timestamp")
rep("- LOGS opens a full-screen list: name, size, duration, newest first, drag to scroll.",
    "- LOGS opens a full-screen list of the newest 20 files: name, size, duration, newest first, drag to scroll.")
rep("- Tapping a file opens the viewer: a read-only terminal with a header bar (name, timestamp\n  margin toggle, play, speed, stop, back, delete).",
    "- Tapping a file opens the viewer: a read-only terminal with a header bar (file name, state, time)\n  and a control row (play/pause, speed, stop, delete, back). Tapping a terminal line shows that\n  line's timestamp in the header (a 53-column line leaves no room for a timestamp margin).\n- The main button row becomes 12 buttons in a 6x2 grid; REC and LOGS are the two new ones at\n  the end. Button width is 53 px (the last column is 55 px).")
open(p, 'w').write(s)
EOF
git diff --stat
```

Expected: one file changed, three replacements, no assertion error.

- [ ] **Step 2: Write the gate sketch**

Create `tools/sdgate/sdgate.ino`:

```cpp
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
```

- [ ] **Step 3: Build and flash the gate**

Run: `arduino-cli compile --fqbn esp32:esp32:esp32 --output-dir build/sdgate tools/sdgate && fuser -v /dev/ttyUSB0; arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 --input-dir build/sdgate`
Expected: compile succeeds, upload ends with "Hard resetting via RTS pin". If `fuser` lists a process holding the port, stop it first. If the compiler cannot find `../../src/secrets.h`, `src/secrets.h` must exist (see `src/secrets.h.example`).

- [ ] **Step 4: Ask the user to insert the card and run the gate**

Tell the user: insert a FAT32 microSD card into the CYD, keep the S3 hotspot running, and tap the screen a few times during the next two minutes. Then capture:

(`$SCRATCH` is your scratchpad directory.)

```bash
stty -F /dev/ttyUSB0 115200 raw -hupcl -echo
timeout 125 cat /dev/ttyUSB0 > "$SCRATCH/sdgate.log"; tail -4 "$SCRATCH/sdgate.log"; head -3 "$SCRATCH/sdgate.log"
```

Expected (gate PASS): first line `sd mounted=1 hz=<n> ...`; last lines show `passes` of 5 or more, `errors=0`, `maxWriteMs` under 500, `touches` above 0 with `last=(x,y)` coordinates in 200..3900, and `wifi=3` (connected).

- [ ] **Step 5: Judge the gate and record the result**

- Mounted but `errors>0` or touch dead or reboots: STOP and report the log to the user. Do not improvise a bus workaround (the options are a shared-bus mutex with pin re-attach on every access, or dropping the SD slot); the user decides.
- No card slot response (`mounted=0` at every speed): STOP and ask the user (wrong pins or bad card).
- PASS: continue.

Append to `docs/hardware.md`:

```markdown
## microSD (verified by tools/sdgate)

Pins: CS 5, SCK 18, MISO 19, MOSI 23 on its own `SPIClass(VSPI)`, alongside the touch panel's
`SPIClass(VSPI)` on CLK 25 / MISO 39 / MOSI 32 / CS 33. Works with Wi-Fi joined and the touch
panel polled at 100 Hz. Highest working clock: <HZ> Hz. 256 KB write+verify passes: <N>, errors 0,
worst single 4 KB write: <MS> ms.
```

Fill `<HZ>`, `<N>`, `<MS>` with the real numbers from the log.

- [ ] **Step 6: Add the constants to `src/config.h`**

Append (use the real working clock for `SD_HZ`, and change the pins only if the gate needed different ones):

```cpp

// microSD (verified by tools/sdgate, see docs/hardware.md)
constexpr int SD_CS = 5, SD_SCK = 18, SD_MISO = 19, SD_MOSI = 23;
constexpr uint32_t SD_HZ = 20000000;
constexpr int REC_QUEUE_BYTES = 8192;       // frames waiting for the recorder task
```

- [ ] **Step 7: Restore the normal build and commit**

Run: `tools/build.sh 2>&1 | tail -2 && PORT=/dev/ttyUSB0 tools/flash.sh 2>&1 | tail -1 && tools/run-tests.sh 2>&1 | tail -1 && tools/commit.sh "feat: SD hardware gate passes; amend spec (tap-for-timestamp, 6x2 buttons, 20-file list)"`
Expected: build prints sketch size, tests print `1508 checks, 0 failures`, commit succeeds.

---

### Task 2: File format (`FileIO`, `cbpformat`) and `ByteRing::capacity`

**Files:**
- Create: `src/fileio.h`, `src/cbpformat.h`, `src/cbpformat.cpp`, `test/memfile.h`, `test/test_cbpformat.cpp`
- Modify: `src/bytering.h`, `test/test_bytering.cpp`, `tools/run-tests.sh`

**Interfaces:**
- Produces: `FileIO` (sequential `read/write/flush/size`); `CbpDir {CBP_RX, CBP_TX, CBP_MARK}`; `CbpRecord {uint32_t ms; uint8_t dir, len; uint8_t data[255];}`; `bool cbpWriteHeader(FileIO&)`; `bool cbpWriteRecord(FileIO&, uint32_t ms, uint8_t dir, const uint8_t* d, size_t n)` (splits at 255); `CbpReader` with `open(FileIO&)`, `headerOk()`, `next(CbpRecord&)`; `ByteRing::capacity()`; test helper `MemFile` (in `test/memfile.h`).

- [ ] **Step 1: Write the failing tests**

Create `test/memfile.h`:

```cpp
#pragma once
#include <string.h>
#include <vector>
#include "fileio.h"

// In-memory FileIO for host tests. failAfter: total bytes the file will accept before writes fail.
class MemFile : public FileIO {
public:
  std::vector<uint8_t> bytes;
  size_t pos = 0;
  size_t failAfter = (size_t)-1;
  int flushes = 0;
  size_t read(uint8_t* out, size_t n) override {
    size_t avail = bytes.size() - pos, k = n < avail ? n : avail;
    memcpy(out, bytes.data() + pos, k);
    pos += k;
    return k;
  }
  size_t write(const uint8_t* d, size_t n) override {
    size_t room = failAfter > bytes.size() ? failAfter - bytes.size() : 0;
    size_t k = n < room ? n : room;
    bytes.insert(bytes.end(), d, d + k);
    return k;
  }
  void flush() override { flushes++; }
  size_t size() override { return bytes.size(); }
};
```

Create `test/test_cbpformat.cpp`:

```cpp
#include "check.h"
#include <string.h>
#include "cbpformat.h"
#include "memfile.h"

static const uint8_t* U(const char* s) { return (const uint8_t*)s; }

SUITE(cbpformat_tests) {
  {  // round trip of all three directions, timestamps use all four bytes
    MemFile f;
    CHECK(cbpWriteHeader(f));
    CHECK(cbpWriteRecord(f, 0, CBP_MARK, U("[rec start]"), 11));
    CHECK(cbpWriteRecord(f, 1500, CBP_RX, U("HIZ> "), 5));
    CHECK(cbpWriteRecord(f, 0xFFFFFFFEu, CBP_TX, U("help\r"), 5));
    CHECK(memcmp(f.bytes.data(), "CBP1", 4) == 0);
    f.pos = 0;
    CbpReader r(f);
    CHECK(r.headerOk());
    CbpRecord rec;
    CHECK(r.next(rec)); CHECK_EQ(rec.ms, 0); CHECK_EQ(rec.dir, CBP_MARK); CHECK_EQ(rec.len, 11);
    CHECK(memcmp(rec.data, "[rec start]", 11) == 0);
    CHECK(r.next(rec)); CHECK_EQ(rec.ms, 1500); CHECK_EQ(rec.dir, CBP_RX); CHECK(memcmp(rec.data, "HIZ> ", 5) == 0);
    CHECK(r.next(rec)); CHECK_EQ(rec.ms, 0xFFFFFFFEu); CHECK_EQ(rec.dir, CBP_TX); CHECK_EQ(rec.len, 5);
    CHECK(!r.next(rec));
  }
  {  // 600 bytes become 255 + 255 + 90 with the same timestamp, bytes in order
    MemFile f;
    uint8_t in[600];
    for (int i = 0; i < 600; i++) in[i] = (uint8_t)(i * 3);
    cbpWriteHeader(f);
    CHECK(cbpWriteRecord(f, 42, CBP_RX, in, 600));
    f.pos = 0;
    CbpReader r(f);
    CbpRecord rec;
    int total = 0, recs = 0;
    while (r.next(rec)) {
      CHECK_EQ(rec.ms, 42);
      for (int i = 0; i < rec.len; i++) CHECK_EQ(rec.data[i], in[total + i]);
      total += rec.len; recs++;
    }
    CHECK_EQ(total, 600); CHECK_EQ(recs, 3);
  }
  {  // a truncated final record is ignored, earlier records survive (card pulled mid-write)
    MemFile f;
    cbpWriteHeader(f);
    cbpWriteRecord(f, 10, CBP_RX, U("one"), 3);
    cbpWriteRecord(f, 20, CBP_RX, U("two"), 3);
    f.bytes.resize(f.bytes.size() - 2);               // cut inside the payload of the last record
    f.pos = 0;
    CbpReader r(f);
    CbpRecord rec;
    CHECK(r.next(rec)); CHECK_EQ(rec.ms, 10);
    CHECK(!r.next(rec));
    f.bytes.resize(4 + 9 + 3);                        // cut inside the next record's 6-byte head
    f.pos = 0;
    CbpReader r2(f);
    CHECK(r2.next(rec)); CHECK(!r2.next(rec));
  }
  {  // wrong magic, empty file, header only
    MemFile bad; bad.bytes = {'X', 'B', 'P', '1', 1, 2, 3};
    CbpReader rb(bad); CbpRecord rec;
    CHECK(!rb.headerOk()); CHECK(!rb.next(rec));
    MemFile empty; CbpReader re(empty);
    CHECK(!re.headerOk()); CHECK(!re.next(rec));
    MemFile hdr; cbpWriteHeader(hdr); hdr.pos = 0; CbpReader rh(hdr);
    CHECK(rh.headerOk()); CHECK(!rh.next(rec));
  }
  {  // an invalid direction or a zero length stops the reader instead of misreading
    MemFile f;
    cbpWriteHeader(f);
    cbpWriteRecord(f, 1, CBP_RX, U("ok"), 2);
    const uint8_t junk[8] = {5, 0, 0, 0, 9, 2, 'x', 'y'};   // dir 9
    f.write(junk, 8);
    f.pos = 0;
    CbpReader r(f); CbpRecord rec;
    CHECK(r.next(rec)); CHECK(!r.next(rec));
  }
  {  // a rejected write is reported to the caller
    MemFile f; f.failAfter = 10;
    CHECK(cbpWriteHeader(f));
    CHECK(!cbpWriteRecord(f, 0, CBP_RX, U("0123456789"), 10));
  }
}
```

In `test/test_bytering.cpp`, add inside `SUITE(bytering_tests) {` before the closing brace:

```cpp
  {  // capacity is the constructor size and free space is capacity - size
    ByteRing r(8);
    CHECK_EQ(r.capacity(), 8);
    const uint8_t in[3] = {1, 2, 3};
    r.push(in, 3);
    CHECK_EQ(r.capacity() - r.size(), 5);
  }
```

Add `cbpformat` to `UNITS` in `tools/run-tests.sh` (the list becomes `ansi termbuf prompt calib keyboard layout gesture bytering cbpformat`).

- [ ] **Step 2: Run to verify it fails**

Run: `tools/run-tests.sh 2>&1 | tail -5`
Expected: compile errors (`cbpformat.h: No such file` or `'capacity' is not a member of 'ByteRing'`). That is the failing state.

- [ ] **Step 3: Implement**

In `src/bytering.h`, add after `size()`:

```cpp
  size_t capacity() const { return cap_; }
```

Create `src/fileio.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

// Minimal sequential file interface so the log format code runs on the host (in memory) and on the SD card.
class FileIO {
public:
  virtual ~FileIO() {}
  virtual size_t read(uint8_t* out, size_t n) = 0;          // bytes read; 0 at end of file
  virtual size_t write(const uint8_t* d, size_t n) = 0;     // bytes written; fewer than n means failure
  virtual void flush() = 0;
  virtual size_t size() = 0;
};
```

Create `src/cbpformat.h`:

```cpp
#pragma once
#include "fileio.h"

// Session file: "CBP1", then records of  u32 ms (LE) | u8 dir | u8 len (1..255) | len bytes.
constexpr size_t CBP_MAX_PAYLOAD = 255;
enum CbpDir : uint8_t { CBP_RX = 0, CBP_TX = 1, CBP_MARK = 2 };

struct CbpRecord { uint32_t ms; uint8_t dir; uint8_t len; uint8_t data[CBP_MAX_PAYLOAD]; };

bool cbpWriteHeader(FileIO& f);
// Writes n payload bytes as one or more records of at most 255 bytes. False if the file rejected a write.
bool cbpWriteRecord(FileIO& f, uint32_t ms, uint8_t dir, const uint8_t* d, size_t n);

class CbpReader {
public:
  CbpReader() {}
  explicit CbpReader(FileIO& f) { open(f); }
  bool open(FileIO& f);                 // reads the 4-byte header; false if it is not "CBP1"
  bool headerOk() const { return ok_; }
  bool next(CbpRecord& r);              // false at end of file, at a truncated record, or at an invalid one
private:
  FileIO* f_ = nullptr;
  bool ok_ = false;
};
```

Create `src/cbpformat.cpp`:

```cpp
#include "cbpformat.h"
#include <string.h>

bool cbpWriteHeader(FileIO& f) { return f.write((const uint8_t*)"CBP1", 4) == 4; }

bool cbpWriteRecord(FileIO& f, uint32_t ms, uint8_t dir, const uint8_t* d, size_t n) {
  while (n > 0) {
    size_t k = n < CBP_MAX_PAYLOAD ? n : CBP_MAX_PAYLOAD;
    uint8_t head[6] = {(uint8_t)ms, (uint8_t)(ms >> 8), (uint8_t)(ms >> 16), (uint8_t)(ms >> 24), dir, (uint8_t)k};
    if (f.write(head, 6) != 6 || f.write(d, k) != k) return false;
    d += k;
    n -= k;
  }
  return true;
}

bool CbpReader::open(FileIO& f) {
  f_ = &f;
  uint8_t m[4];
  ok_ = f.read(m, 4) == 4 && memcmp(m, "CBP1", 4) == 0;
  return ok_;
}

bool CbpReader::next(CbpRecord& r) {
  if (!ok_ || !f_) return false;
  uint8_t head[6];
  if (f_->read(head, 6) != 6) return false;
  r.ms = head[0] | (uint32_t)head[1] << 8 | (uint32_t)head[2] << 16 | (uint32_t)head[3] << 24;
  r.dir = head[4];
  r.len = head[5];
  if (r.dir > CBP_MARK || r.len == 0) return false;
  return f_->read(r.data, r.len) == r.len;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `tools/run-tests.sh 2>&1 | tail -3`
Expected: `... checks, 0 failures` (more than 1508 checks, no `FAIL` lines).

- [ ] **Step 5: Commit**

Run: `tools/commit.sh "feat: session file format (CBP1 records) with truncated-tail tolerance"`

---

### Task 3: `LogStore` interface and the `Recorder`

**Files:**
- Create: `src/logstore.h`, `src/recorder.h`, `src/recorder.cpp`, `test/memstore.h`, `test/test_recorder.cpp`
- Modify: `tools/run-tests.sh`

**Interfaces:**
- Consumes: `FileIO`, `cbpWriteHeader`, `cbpWriteRecord`, `CbpReader`, `CbpDir`, `ByteRing::capacity()` from Task 2.
- Produces:
  - `struct LogInfo { char name[13]; uint32_t size; }`; `class LogStore` with `bool available()`, `FileIO* createNew(char* nameOut, size_t cap)`, `FileIO* openRead(const char* name)`, `void closeFile(FileIO*)`, `int list(LogInfo* out, int max)` (newest first), `bool remove(const char* name)`, `virtual void markFailed() {}`.
  - `enum class RecState {Idle, Recording, Error}`, `enum class RecError {None, NoCard, WriteFailed}`.
  - `class Recorder(LogStore&, size_t queueBytes)`: producer side (loop task) `log(nowMs, dir, d, n)`, `mark(nowMs, text)`, `requestStart(nowMs)`, `requestStop()`; queries `state()`, `error()`, `recording()`, `elapsedMs(nowMs)`, `fileName()`, `dropped()`; consumer side (recorder task) `service(nowMs)`.
  - Test fake `MemStore` (in `test/memstore.h`).

- [ ] **Step 1: Write the failing tests**

Create `src/logstore.h` first (the interface the tests include):

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "fileio.h"

struct LogInfo { char name[13]; uint32_t size; };          // name is "0007.cbp"

// Where session files live. The SD card implements it on the CYD; MemStore implements it in host tests.
class LogStore {
public:
  virtual ~LogStore() {}
  virtual bool available() = 0;                                  // a card is mounted (may try to mount)
  virtual FileIO* createNew(char* nameOut, size_t cap) = 0;      // next free NNNN.cbp opened for writing; nullptr on failure
  virtual FileIO* openRead(const char* name) = 0;                // nullptr if missing
  virtual void closeFile(FileIO* f) = 0;
  virtual int list(LogInfo* out, int max) = 0;                   // newest (highest number) first
  virtual bool remove(const char* name) = 0;
  virtual void markFailed() {}                                   // a write or open failed: drop the mount so it is retried
};
```

Create `test/memstore.h`:

```cpp
#pragma once
#include <map>
#include <stdio.h>
#include <string>
#include "logstore.h"
#include "memfile.h"

class MemStore : public LogStore {
public:
  bool cardPresent = true;
  size_t failAfter = (size_t)-1;                 // applied to every file created from now on
  int failedMarks = 0;
  std::map<std::string, MemFile> files;
  bool available() override { return cardPresent; }
  FileIO* createNew(char* nameOut, size_t cap) override {
    if (!cardPresent) return nullptr;
    for (int n = 1; n < 10000; n++) {
      char name[16];
      snprintf(name, sizeof name, "%04d.cbp", n);
      if (!files.count(name)) {
        MemFile& f = files[name];
        f.failAfter = failAfter;
        snprintf(nameOut, cap, "%s", name);
        return &f;
      }
    }
    return nullptr;
  }
  FileIO* openRead(const char* name) override {
    auto it = files.find(name);
    if (!cardPresent || it == files.end()) return nullptr;
    it->second.pos = 0;
    return &it->second;
  }
  void closeFile(FileIO*) override {}
  int list(LogInfo* out, int max) override {
    int n = 0;
    for (auto it = files.rbegin(); it != files.rend() && n < max; ++it, ++n) {   // names sort numerically
      snprintf(out[n].name, sizeof out[n].name, "%s", it->first.c_str());
      out[n].size = (uint32_t)it->second.bytes.size();
    }
    return n;
  }
  bool remove(const char* name) override { return files.erase(name) > 0; }
  void markFailed() override { failedMarks++; }
};
```

Create `test/test_recorder.cpp`:

```cpp
#include "check.h"
#include <string.h>
#include <vector>
#include "recorder.h"
#include "memstore.h"

static const uint8_t* U(const char* s) { return (const uint8_t*)s; }

static std::vector<CbpRecord> readAll(MemStore& s, const char* name) {
  std::vector<CbpRecord> v;
  FileIO* f = s.openRead(name);
  if (!f) return v;
  CbpReader r(*f);
  CbpRecord rec;
  while (r.next(rec)) v.push_back(rec);
  return v;
}

SUITE(recorder_tests) {
  {  // start, log both directions, stop: header + start marker + records, ms relative to REC
    MemStore st; Recorder r(st, 1024);
    r.requestStart(5000);
    r.service(5000);
    CHECK(r.state() == RecState::Recording); CHECK(r.recording());
    CHECK_STR(r.fileName(), "0001.cbp");
    r.log(5100, CBP_RX, U("hi"), 2);
    r.log(5200, CBP_TX, U("ok"), 2);
    r.service(5200);
    r.requestStop();
    r.service(5300);
    CHECK(r.state() == RecState::Idle); CHECK(!r.recording());
    auto v = readAll(st, "0001.cbp");
    CHECK_EQ(v.size(), 3);
    CHECK_EQ(v[0].dir, CBP_MARK); CHECK_EQ(v[0].ms, 0); CHECK(memcmp(v[0].data, "[rec start]", 11) == 0);
    CHECK_EQ(v[1].dir, CBP_RX); CHECK_EQ(v[1].ms, 100); CHECK(memcmp(v[1].data, "hi", 2) == 0);
    CHECK_EQ(v[2].dir, CBP_TX); CHECK_EQ(v[2].ms, 200);
    CHECK(st.files["0001.cbp"].flushes >= 1);          // stop flushes before closing
  }
  {  // frames queued before stop are all written; logging after stop is ignored
    MemStore st; Recorder r(st, 1024);
    r.requestStart(0); r.service(0);
    r.log(10, CBP_RX, U("a"), 1);
    r.requestStop();
    r.log(20, CBP_RX, U("late"), 4);
    r.service(30);
    CHECK_EQ(readAll(st, "0001.cbp").size(), 2);       // start marker + "a"
  }
  {  // log() before any start does nothing; a second recording gets the next file number
    MemStore st; Recorder r(st, 1024);
    r.log(1, CBP_RX, U("x"), 1);
    r.requestStart(0); r.service(0); r.requestStop(); r.service(1);
    r.requestStart(100); r.service(100);
    CHECK_STR(r.fileName(), "0002.cbp");
    CHECK_EQ(readAll(st, "0001.cbp").size(), 1);       // only the start marker
  }
  {  // requestStart while recording is ignored; requestStart before the stop finishes is ignored
    MemStore st; Recorder r(st, 1024);
    r.requestStart(0); r.service(0);
    r.requestStart(50); r.service(50);
    CHECK_EQ(st.files.size(), 1);
    r.log(60, CBP_RX, U("z"), 1);
    r.requestStop();
    r.requestStart(70);
    r.service(80);
    CHECK_EQ(st.files.size(), 1);
    CHECK(r.state() == RecState::Idle);
  }
  {  // no card: error shown, nothing queued, a later start works once the card is back
    MemStore st; st.cardPresent = false;
    Recorder r(st, 1024);
    r.requestStart(0); r.service(0);
    CHECK(r.state() == RecState::Error); CHECK(r.error() == RecError::NoCard); CHECK(!r.recording());
    r.log(10, CBP_RX, U("ignored"), 7);
    st.cardPresent = true;
    r.requestStart(100); r.service(100);
    CHECK(r.state() == RecState::Recording); CHECK(r.error() == RecError::None);
    r.requestStop(); r.service(110);
    CHECK_EQ(readAll(st, "0001.cbp").size(), 1);       // the ignored bytes never reached the file
  }
  {  // queue overflow: excess dropped and counted, order kept, marker placed before the next data
    MemStore st; Recorder r(st, 64);
    r.requestStart(0); r.service(0);
    uint8_t a[40], b[40];
    memset(a, 'a', 40); memset(b, 'b', 40);
    r.log(10, CBP_RX, a, 40);                          // frame of 46 bytes fits in 64
    r.log(20, CBP_RX, b, 40);                          // only 18 free: dropped
    CHECK_EQ(r.dropped(), 40);
    r.service(30);                                     // drains
    r.log(40, CBP_RX, U("c"), 1);                      // marker, then data
    r.service(50);
    auto v = readAll(st, "0001.cbp");
    CHECK_EQ(v.size(), 4);
    CHECK_EQ(v[1].dir, CBP_RX); CHECK_EQ(v[1].len, 40); CHECK_EQ(v[1].data[0], 'a');
    CHECK_EQ(v[2].dir, CBP_MARK); CHECK(memcmp(v[2].data, "[rec overflow]", 14) == 0); CHECK_EQ(v[2].ms, 40);
    CHECK_EQ(v[3].dir, CBP_RX); CHECK_EQ(v[3].data[0], 'c');
  }
  {  // a payload over 255 bytes is queued in pieces and written as 255+255+90
    MemStore st; Recorder r(st, 2048);
    r.requestStart(0); r.service(0);
    uint8_t big[600];
    for (int i = 0; i < 600; i++) big[i] = (uint8_t)i;
    r.log(5, CBP_RX, big, 600);
    r.service(6);
    auto v = readAll(st, "0001.cbp");
    CHECK_EQ(v.size(), 4);
    CHECK_EQ(v[1].len, 255); CHECK_EQ(v[2].len, 255); CHECK_EQ(v[3].len, 90);
    CHECK_EQ(v[3].data[89], (uint8_t)599);
  }
  {  // a failing write ends the recording with an error, the store is told, and logging stops
    MemStore st; st.failAfter = 40;
    Recorder r(st, 1024);
    r.requestStart(0); r.service(0);                   // header 4 + marker 17 = 21 bytes: fits
    CHECK(r.state() == RecState::Recording);
    uint8_t big[30];
    memset(big, 'q', 30);
    r.log(10, CBP_RX, big, 30);                        // 36-byte record: does not fit
    r.service(20);
    CHECK(r.state() == RecState::Error); CHECK(r.error() == RecError::WriteFailed); CHECK(!r.recording());
    CHECK_EQ(st.failedMarks, 1);
    r.log(30, CBP_RX, U("more"), 4);                   // ignored, no crash
    r.service(40);
    CHECK(r.state() == RecState::Error);
  }
  {  // flush about once per second, not on every service call
    MemStore st; Recorder r(st, 1024);
    r.requestStart(0); r.service(0);
    r.log(100, CBP_RX, U("x"), 1);
    r.service(500);
    CHECK_EQ(st.files["0001.cbp"].flushes, 0);
    r.service(1000);
    CHECK_EQ(st.files["0001.cbp"].flushes, 1);
    r.service(1500);
    CHECK_EQ(st.files["0001.cbp"].flushes, 1);
    r.service(2000);
    CHECK_EQ(st.files["0001.cbp"].flushes, 2);
  }
  {  // elapsed time counts from REC only while recording
    MemStore st; Recorder r(st, 1024);
    CHECK_EQ(r.elapsedMs(9000), 0);
    r.requestStart(1000);
    CHECK_EQ(r.elapsedMs(4500), 3500);
    r.service(1000); r.requestStop(); r.service(1001);
    CHECK_EQ(r.elapsedMs(9000), 0);
  }
}
```

Add `recorder` to `UNITS` in `tools/run-tests.sh`.

- [ ] **Step 2: Run to verify it fails**

Run: `tools/run-tests.sh 2>&1 | tail -4`
Expected: compile error `recorder.h: No such file or directory`.

- [ ] **Step 3: Implement**

Create `src/recorder.h`:

```cpp
#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include "bytering.h"
#include "cbpformat.h"
#include "logstore.h"

enum class RecState : uint8_t { Idle, Recording, Error };
enum class RecError : uint8_t { None, NoCard, WriteFailed };

// Records sessions to a LogStore. The loop task is the only producer (log, mark, requestStart, requestStop);
// the recorder task is the only consumer (service) and the only one that touches the files.
// Each queued frame is  dir | len | ms (LE32) | payload  and is pushed in a single ByteRing push, so the
// consumer never sees half a frame.
class Recorder {
public:
  Recorder(LogStore& store, size_t queueBytes) : store_(store), q_(queueBytes) {}

  void log(uint32_t nowMs, uint8_t dir, const uint8_t* d, size_t n);   // no-op unless recording
  void mark(uint32_t nowMs, const char* text);
  void requestStart(uint32_t nowMs);                                   // ignored while recording or stopping
  void requestStop();

  RecState state() const { return (RecState)state_.load(); }
  RecError error() const { return (RecError)error_.load(); }
  bool recording() const { return active_.load(); }
  uint32_t elapsedMs(uint32_t nowMs) const { return active_.load() ? nowMs - startMs_.load() : 0; }
  const char* fileName() const { return name_; }
  size_t dropped() const { return dropped_; }

  void service(uint32_t nowMs);                                        // recorder task, every few ms

private:
  bool pushFrame(uint32_t ms, uint8_t dir, const uint8_t* d, size_t n);   // all or nothing
  void finish();
  void fail(RecError e);

  LogStore& store_;
  ByteRing q_;
  FileIO* file_ = nullptr;
  char name_[13] = {0};
  std::atomic<bool> active_{false}, startReq_{false}, stopReq_{false};
  std::atomic<uint8_t> state_{(uint8_t)RecState::Idle}, error_{(uint8_t)RecError::None};
  std::atomic<uint32_t> startMs_{0};
  bool overflowed_ = false;            // producer side only
  size_t dropped_ = 0;                 // producer side only
  uint32_t lastFlush_ = 0;             // consumer side only
};
```

Create `src/recorder.cpp`:

```cpp
#include "recorder.h"
#include <string.h>

static const char* const START_MARK = "[rec start]";
static const char* const OVERFLOW_MARK = "[rec overflow]";

bool Recorder::pushFrame(uint32_t ms, uint8_t dir, const uint8_t* d, size_t n) {
  if (q_.capacity() - q_.size() < 6 + n) return false;
  uint8_t frame[6 + CBP_MAX_PAYLOAD];
  frame[0] = dir;
  frame[1] = (uint8_t)n;
  frame[2] = (uint8_t)ms; frame[3] = (uint8_t)(ms >> 8); frame[4] = (uint8_t)(ms >> 16); frame[5] = (uint8_t)(ms >> 24);
  memcpy(frame + 6, d, n);
  q_.push(frame, 6 + n);
  return true;
}

void Recorder::log(uint32_t nowMs, uint8_t dir, const uint8_t* d, size_t n) {
  if (!active_.load()) return;
  while (n > 0) {
    size_t k = n < CBP_MAX_PAYLOAD ? n : CBP_MAX_PAYLOAD;
    if (overflowed_ && pushFrame(nowMs, CBP_MARK, (const uint8_t*)OVERFLOW_MARK, strlen(OVERFLOW_MARK))) overflowed_ = false;
    if (overflowed_ || !pushFrame(nowMs, dir, d, k)) {      // data may not jump ahead of a marker that did not fit
      overflowed_ = true;
      dropped_ += k;
    }
    d += k;
    n -= k;
  }
}

void Recorder::mark(uint32_t nowMs, const char* text) { log(nowMs, CBP_MARK, (const uint8_t*)text, strlen(text)); }

void Recorder::requestStart(uint32_t nowMs) {
  if (active_.load() || stopReq_.load()) return;
  startMs_ = nowMs;
  error_ = (uint8_t)RecError::None;
  overflowed_ = false;
  active_ = true;
  startReq_ = true;
}

void Recorder::requestStop() {
  if (!active_.load()) return;
  active_ = false;                     // the producer stops here, so nothing new is queued after this
  stopReq_ = true;
}

void Recorder::finish() {
  if (!file_) return;
  file_->flush();
  store_.closeFile(file_);
  file_ = nullptr;
}

void Recorder::fail(RecError e) {
  active_ = false;
  stopReq_ = false;
  startReq_ = false;
  if (file_) { store_.closeFile(file_); file_ = nullptr; }
  store_.markFailed();
  uint8_t junk[64];
  while (q_.pop(junk, sizeof junk) > 0) {}
  error_ = (uint8_t)e;
  state_ = (uint8_t)RecState::Error;
}

void Recorder::service(uint32_t nowMs) {
  if (startReq_.exchange(false)) {
    FileIO* f = store_.available() ? store_.createNew(name_, sizeof name_) : nullptr;
    if (!f) { fail(RecError::NoCard); return; }
    file_ = f;
    if (!cbpWriteHeader(*file_) ||
        !cbpWriteRecord(*file_, 0, CBP_MARK, (const uint8_t*)START_MARK, strlen(START_MARK))) { fail(RecError::WriteFailed); return; }
    state_ = (uint8_t)RecState::Recording;
    lastFlush_ = nowMs;
  }
  if (!file_) return;

  uint8_t head[6], buf[CBP_MAX_PAYLOAD];
  while (q_.size() >= 6) {
    q_.pop(head, 6);                   // frames are published whole, so the payload is already there
    size_t len = head[1];
    q_.pop(buf, len);
    uint32_t ms = (head[2] | (uint32_t)head[3] << 8 | (uint32_t)head[4] << 16 | (uint32_t)head[5] << 24) - startMs_.load();
    if (!cbpWriteRecord(*file_, ms, head[0], buf, len)) { fail(RecError::WriteFailed); return; }
  }
  if (stopReq_.load() && q_.size() == 0) {
    stopReq_ = false;
    finish();
    state_ = (uint8_t)RecState::Idle;
    return;
  }
  if (nowMs - lastFlush_ >= 1000) {
    file_->flush();
    lastFlush_ = nowMs;
  }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `tools/run-tests.sh 2>&1 | tail -3`
Expected: `... checks, 0 failures`, no `FAIL` lines. If a flush-count check fails, re-read the order inside `service()` (flush check is last, stop check before it).

- [ ] **Step 5: Commit**

Run: `tools/commit.sh "feat: Recorder with all-or-nothing frame queue, overflow markers and error states"`

---

### Task 4: Time and size formatting, and the log index

**Files:**
- Create: `src/logindex.h`, `src/logindex.cpp`, `test/test_logindex.cpp`
- Modify: `tools/run-tests.sh`

**Interfaces:**
- Consumes: `LogStore`, `LogInfo`, `CbpReader`, `FileIO` from Tasks 2 and 3; `MemStore` test fake.
- Produces: `void fmtElapsed(uint32_t ms, char* out, size_t cap)` ("MM:SS.mmm", minutes unbounded); `void fmtDuration(uint32_t ms, char* out, size_t cap)` ("MM:SS"); `void fmtSize(uint32_t bytes, char* out, size_t cap)` ("812 B", "1.5 KB", "2.0 MB"); `struct LogEntry { char name[13]; uint32_t size; uint32_t durationMs; bool valid; }`; `int buildLogIndex(LogStore&, LogEntry* out, int max)` (newest first; reads each file for its duration).

- [ ] **Step 1: Write the failing tests**

Create `test/test_logindex.cpp`:

```cpp
#include "check.h"
#include <string.h>
#include "logindex.h"
#include "memstore.h"
#include "cbpformat.h"

static void put(MemStore& s, const char* name, std::initializer_list<uint32_t> times) {
  MemFile& f = s.files[name];
  cbpWriteHeader(f);
  for (uint32_t t : times) cbpWriteRecord(f, t, CBP_RX, (const uint8_t*)"x", 1);
}

SUITE(logindex_tests) {
  {  // formatting
    char b[24];
    fmtElapsed(0, b, sizeof b);        CHECK_STR(b, "00:00.000");
    fmtElapsed(65432, b, sizeof b);    CHECK_STR(b, "01:05.432");
    fmtElapsed(3600001, b, sizeof b);  CHECK_STR(b, "60:00.001");
    fmtDuration(0, b, sizeof b);       CHECK_STR(b, "00:00");
    fmtDuration(65432, b, sizeof b);   CHECK_STR(b, "01:05");
    fmtDuration(3599999, b, sizeof b); CHECK_STR(b, "59:59");
    fmtSize(0, b, sizeof b);           CHECK_STR(b, "0 B");
    fmtSize(1023, b, sizeof b);        CHECK_STR(b, "1023 B");
    fmtSize(1536, b, sizeof b);        CHECK_STR(b, "1.5 KB");
    fmtSize(12345, b, sizeof b);       CHECK_STR(b, "12.0 KB");
    fmtSize(1048576, b, sizeof b);     CHECK_STR(b, "1.0 MB");
    fmtElapsed(5, b, 4);               CHECK_EQ(strlen(b), 3);          // truncates, never overruns
  }
  {  // newest first, duration = last valid record, size from the store
    MemStore st;
    put(st, "0001.cbp", {0, 900, 2500});
    put(st, "0002.cbp", {0, 9000});
    put(st, "0003.cbp", {0});
    LogEntry e[8];
    int n = buildLogIndex(st, e, 8);
    CHECK_EQ(n, 3);
    CHECK_STR(e[0].name, "0003.cbp"); CHECK_STR(e[1].name, "0002.cbp"); CHECK_STR(e[2].name, "0001.cbp");
    CHECK(e[0].valid && e[1].valid && e[2].valid);
    CHECK_EQ(e[0].durationMs, 0); CHECK_EQ(e[1].durationMs, 9000); CHECK_EQ(e[2].durationMs, 2500);
    CHECK_EQ(e[2].size, st.files["0001.cbp"].bytes.size());
  }
  {  // a truncated tail still gives the duration of the last complete record
    MemStore st;
    put(st, "0001.cbp", {10, 4000, 8000});
    st.files["0001.cbp"].bytes.resize(st.files["0001.cbp"].bytes.size() - 1);
    LogEntry e[2];
    CHECK_EQ(buildLogIndex(st, e, 2), 1);
    CHECK(e[0].valid); CHECK_EQ(e[0].durationMs, 4000);
  }
  {  // a file that is not a session file is listed as invalid
    MemStore st;
    st.files["0001.cbp"].bytes = {'N', 'O', 'P', 'E', 1, 2, 3};
    LogEntry e[2];
    CHECK_EQ(buildLogIndex(st, e, 2), 1);
    CHECK(!e[0].valid); CHECK_EQ(e[0].durationMs, 0);
  }
  {  // max limits the list to the newest files; empty or missing card gives zero
    MemStore st;
    put(st, "0001.cbp", {1}); put(st, "0002.cbp", {1}); put(st, "0003.cbp", {1});
    LogEntry e[2];
    CHECK_EQ(buildLogIndex(st, e, 2), 2);
    CHECK_STR(e[0].name, "0003.cbp"); CHECK_STR(e[1].name, "0002.cbp");
    MemStore empty; CHECK_EQ(buildLogIndex(empty, e, 2), 0);
    MemStore nocard; nocard.cardPresent = false; put(nocard, "0001.cbp", {1});
    CHECK_EQ(buildLogIndex(nocard, e, 2), 0);
  }
}
```

Add `logindex` to `UNITS` in `tools/run-tests.sh`.

- [ ] **Step 2: Run to verify it fails**

Run: `tools/run-tests.sh 2>&1 | tail -4`
Expected: compile error `logindex.h: No such file or directory`.

- [ ] **Step 3: Implement**

Create `src/logindex.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "logstore.h"

void fmtElapsed(uint32_t ms, char* out, size_t cap);     // "MM:SS.mmm" (minutes are not capped at 59)
void fmtDuration(uint32_t ms, char* out, size_t cap);    // "MM:SS"
void fmtSize(uint32_t bytes, char* out, size_t cap);     // "812 B", "1.5 KB", "2.0 MB"

struct LogEntry { char name[13]; uint32_t size; uint32_t durationMs; bool valid; };

// Lists up to max files, newest first, reading each one to find its duration.
int buildLogIndex(LogStore& store, LogEntry* out, int max);
```

Create `src/logindex.cpp`:

```cpp
#include "logindex.h"
#include <stdio.h>
#include "cbpformat.h"

void fmtElapsed(uint32_t ms, char* out, size_t cap) {
  snprintf(out, cap, "%02u:%02u.%03u", (unsigned)(ms / 60000), (unsigned)(ms / 1000 % 60), (unsigned)(ms % 1000));
}

void fmtDuration(uint32_t ms, char* out, size_t cap) {
  snprintf(out, cap, "%02u:%02u", (unsigned)(ms / 60000), (unsigned)(ms / 1000 % 60));
}

void fmtSize(uint32_t bytes, char* out, size_t cap) {
  if (bytes < 1024) snprintf(out, cap, "%u B", (unsigned)bytes);
  else if (bytes < 1048576) { unsigned t = (unsigned)((uint64_t)bytes * 10 / 1024); snprintf(out, cap, "%u.%u KB", t / 10, t % 10); }
  else { unsigned t = (unsigned)((uint64_t)bytes * 10 / 1048576); snprintf(out, cap, "%u.%u MB", t / 10, t % 10); }
}

int buildLogIndex(LogStore& store, LogEntry* out, int max) {
  if (!store.available() || max <= 0) return 0;
  LogInfo info[32];
  int n = store.list(info, max < 32 ? max : 32);
  int count = 0;
  for (int i = 0; i < n; i++) {
    LogEntry& e = out[count++];
    snprintf(e.name, sizeof e.name, "%s", info[i].name);
    e.size = info[i].size;
    e.durationMs = 0;
    e.valid = false;
    FileIO* f = store.openRead(info[i].name);
    if (!f) continue;
    CbpReader rd(*f);
    e.valid = rd.headerOk();
    CbpRecord rec;
    while (rd.next(rec)) e.durationMs = rec.ms;
    store.closeFile(f);
  }
  return count;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `tools/run-tests.sh 2>&1 | tail -3`
Expected: `... checks, 0 failures`.

- [ ] **Step 5: Commit**

Run: `tools/commit.sh "feat: log index with durations and time/size formatting"`

---

### Task 5: `Replayer`

**Files:**
- Create: `src/replayer.h`, `src/replayer.cpp`, `test/test_replayer.cpp`
- Modify: `tools/run-tests.sh`

**Interfaces:**
- Consumes: `CbpReader`, `CbpRecord`, `FileIO`; `MemFile`.
- Produces: `struct RecordSink { virtual void onRecord(const CbpRecord&) = 0; }`; `class Replayer` with `bool begin(FileIO&)`, `void play(uint32_t nowMs)`, `void pause()`, `void stop()`, `void setSpeed(int x)` (1, 4 or 16), `int speed()`, `void tick(uint32_t nowMs, RecordSink&)`, `bool playing()`, `bool paused()`, `bool finished()`, `uint32_t positionMs()`. `Replayer::MAX_GAP_MS = 2000`.

- [ ] **Step 1: Write the failing tests**

Create `test/test_replayer.cpp`:

```cpp
#include "check.h"
#include <vector>
#include "replayer.h"
#include "memfile.h"
#include "cbpformat.h"

namespace {
struct Collect : RecordSink {
  std::vector<uint32_t> ms;
  void onRecord(const CbpRecord& r) override { ms.push_back(r.ms); }
};
void fill(MemFile& f, std::initializer_list<uint32_t> times) {
  cbpWriteHeader(f);
  for (uint32_t t : times) cbpWriteRecord(f, t, CBP_RX, (const uint8_t*)"x", 1);
  f.pos = 0;
}
}

SUITE(replayer_tests) {
  {  // 1x: each record is delivered when its time arrives, and the end is detected
    MemFile f; fill(f, {0, 100, 1000});
    Replayer rp; Collect c;
    CHECK(rp.begin(f));
    rp.play(0);
    rp.tick(0, c);    CHECK_EQ(c.ms.size(), 1);
    rp.tick(99, c);   CHECK_EQ(c.ms.size(), 1);
    rp.tick(100, c);  CHECK_EQ(c.ms.size(), 2);
    rp.tick(999, c);  CHECK_EQ(c.ms.size(), 2);
    rp.tick(1000, c); CHECK_EQ(c.ms.size(), 3);
    CHECK(!rp.finished());
    rp.tick(1001, c); CHECK(rp.finished()); CHECK(!rp.playing());
    CHECK_EQ(c.ms.size(), 3);
  }
  {  // 4x and 16x run time faster
    MemFile f; fill(f, {0, 400});
    Replayer rp; Collect c;
    rp.begin(f); rp.setSpeed(4); rp.play(0);
    rp.tick(0, c); rp.tick(99, c);  CHECK_EQ(c.ms.size(), 1);
    rp.tick(100, c);                CHECK_EQ(c.ms.size(), 2);
    MemFile g; fill(g, {0, 1600});
    Replayer rq; Collect d;
    rq.begin(g); rq.setSpeed(16); rq.play(0);
    rq.tick(0, d); rq.tick(99, d);  CHECK_EQ(d.ms.size(), 1);
    rq.tick(100, d);                CHECK_EQ(d.ms.size(), 2);
    rq.setSpeed(7); CHECK_EQ(rq.speed(), 1);        // only 1, 4, 16 are valid
  }
  {  // a long silence is cut to 2 s; the cut accumulates
    MemFile f; fill(f, {0, 10000, 10100, 40000});
    Replayer rp; Collect c;
    rp.begin(f); rp.play(0);
    rp.tick(0, c);     CHECK_EQ(c.ms.size(), 1);
    rp.tick(1999, c);  CHECK_EQ(c.ms.size(), 1);
    rp.tick(2000, c);  CHECK_EQ(c.ms.size(), 2);        // 10000 ms gap played as 2000
    rp.tick(2100, c);  CHECK_EQ(c.ms.size(), 3);        // 100 ms later as recorded
    rp.tick(4099, c);  CHECK_EQ(c.ms.size(), 3);
    rp.tick(4100, c);  CHECK_EQ(c.ms.size(), 4);        // 29900 ms gap played as 2000
  }
  {  // pause freezes time and resume does not jump ahead
    MemFile f; fill(f, {0, 100});
    Replayer rp; Collect c;
    rp.begin(f); rp.play(0);
    rp.tick(0, c); rp.tick(50, c);
    rp.pause(); CHECK(rp.paused());
    rp.tick(10000, c);  CHECK_EQ(c.ms.size(), 1);
    rp.play(10000);
    rp.tick(10049, c);  CHECK_EQ(c.ms.size(), 1);
    rp.tick(10050, c);  CHECK_EQ(c.ms.size(), 2);
  }
  {  // stop ends delivery; a bad file never starts; positionMs follows the playhead
    MemFile f; fill(f, {0, 100});
    Replayer rp; Collect c;
    rp.begin(f); rp.play(0);
    rp.tick(0, c); rp.tick(30, c);
    CHECK_EQ(rp.positionMs(), 30);
    rp.stop();
    rp.tick(5000, c);   CHECK_EQ(c.ms.size(), 1);
    MemFile bad; bad.bytes = {'N', 'O', 'P', 'E'};
    Replayer rb; Collect d;
    CHECK(!rb.begin(bad));
    rb.play(0); rb.tick(1000, d);
    CHECK(!rb.playing()); CHECK_EQ(d.ms.size(), 0);
  }
  {  // records with out-of-order timestamps (corrupt file) do not stall the replay
    MemFile f; fill(f, {500, 100, 200});
    Replayer rp; Collect c;
    rp.begin(f); rp.play(0);
    rp.tick(500, c);
    CHECK_EQ(c.ms.size(), 3);
  }
}
```

Add `replayer` to `UNITS` in `tools/run-tests.sh`.

- [ ] **Step 2: Run to verify it fails**

Run: `tools/run-tests.sh 2>&1 | tail -4`
Expected: compile error `replayer.h: No such file or directory`.

- [ ] **Step 3: Implement**

Create `src/replayer.h`:

```cpp
#pragma once
#include <stdint.h>
#include "cbpformat.h"

struct RecordSink {
  virtual ~RecordSink() {}
  virtual void onRecord(const CbpRecord& r) = 0;
};

// Plays a session file back at its original timing (1x, 4x or 16x), with silences longer than MAX_GAP_MS
// shortened to MAX_GAP_MS. Display only: it delivers records to a sink and has no way to send anything.
class Replayer {
public:
  static constexpr uint32_t MAX_GAP_MS = 2000;
  bool begin(FileIO& f);                    // false if the file is not a session file; resets all state
  void play(uint32_t nowMs);                // start, or resume after pause
  void pause();
  void stop();
  void setSpeed(int x) { speed_ = (x == 4 || x == 16) ? x : 1; }
  int speed() const { return speed_; }
  void tick(uint32_t nowMs, RecordSink& sink);
  bool playing() const { return st_ == St::Playing; }
  bool paused() const { return st_ == St::Paused; }
  bool finished() const { return st_ == St::Finished; }
  uint32_t positionMs() const { return vt_; }
private:
  enum class St : uint8_t { Stopped, Playing, Paused, Finished };
  CbpReader rd_;
  CbpRecord pending_;
  bool havePending_ = false, ok_ = false;
  St st_ = St::Stopped;
  int speed_ = 1;
  uint32_t vt_ = 0, cut_ = 0, prevMs_ = 0, lastNow_ = 0;
};
```

Create `src/replayer.cpp`:

```cpp
#include "replayer.h"

bool Replayer::begin(FileIO& f) {
  ok_ = rd_.open(f);
  havePending_ = false;
  st_ = St::Stopped;
  vt_ = cut_ = prevMs_ = lastNow_ = 0;
  return ok_;
}

void Replayer::play(uint32_t nowMs) {
  if (!ok_ || st_ == St::Playing || st_ == St::Finished) return;
  lastNow_ = nowMs;
  st_ = St::Playing;
}

void Replayer::pause() { if (st_ == St::Playing) st_ = St::Paused; }
void Replayer::stop() { if (st_ != St::Finished) st_ = St::Stopped; }

void Replayer::tick(uint32_t nowMs, RecordSink& sink) {
  if (st_ != St::Playing) return;
  vt_ += (nowMs - lastNow_) * (uint32_t)speed_;
  lastNow_ = nowMs;
  for (;;) {
    if (!havePending_) {
      if (!rd_.next(pending_)) { st_ = St::Finished; return; }
      if (pending_.ms > prevMs_ && pending_.ms - prevMs_ > MAX_GAP_MS) cut_ += pending_.ms - prevMs_ - MAX_GAP_MS;
      havePending_ = true;
    }
    uint32_t eff = pending_.ms > cut_ ? pending_.ms - cut_ : 0;
    if (eff > vt_) return;
    sink.onRecord(pending_);
    prevMs_ = pending_.ms;
    havePending_ = false;
  }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `tools/run-tests.sh 2>&1 | tail -3`
Expected: `... checks, 0 failures`.

- [ ] **Step 5: Commit**

Run: `tools/commit.sh "feat: Replayer with speed, gap cap and pause"`

---

### Task 6: `TermBuf::lineAtRow`, `ViewerFeed`

**Files:**
- Modify: `src/termbuf.h`, `test/test_termbuf.cpp`, `tools/run-tests.sh`
- Create: `src/viewerfeed.h`, `src/viewerfeed.cpp`, `test/test_viewerfeed.cpp`

**Interfaces:**
- Consumes: `TermBuf`, `AnsiParser`, `RecordSink`, `CbpRecord`, `SCROLLBACK_LINES`.
- Produces: `long TermBuf::lineAtRow(int row) const` (absolute line number shown in terminal row `row` of the current view); `class ViewerFeed : public RecordSink` with `explicit ViewerFeed(TermBuf&)`, `void onRecord(const CbpRecord&)`, `uint32_t msOfLine(long absLine) const`, `void reset()`.

Rendering rules: received bytes go through the ANSI parser as they are; sent bytes are wrapped in cyan (`ESC[36m ... ESC[0m`, colour index 6); a marker is printed on its own line in yellow (colour index 3). Every line is stamped with the `ms` of the record in which its first byte arrived.

- [ ] **Step 1: Write the failing tests**

In `test/test_termbuf.cpp`, add inside the existing suite (before its final `}`; check the suite name at the top of the file) this block:

```cpp
  {  // lineAtRow maps a screen row to its absolute line, live and scrolled
    TermBuf tb; AnsiParser p(tb);
    for (int i = 0; i < 40; i++) { char l[8]; l[0] = 'A' + (i % 26); l[1] = '\n'; p.feed((const uint8_t*)l, 2); }
    for (int r = 0; r < ROWS; r++)
      if (r != tb.cursorRow()) CHECK_EQ(tb.cellAt(r, 0).ch, 'A' + (tb.lineAtRow(r) % 26));   // the cursor line is empty
    CHECK_EQ(tb.lineAtRow(tb.cursorRow()), tb.totalLines() - 1);
    tb.scrollBy(5);
    for (int r = 0; r < ROWS; r++) CHECK_EQ(tb.cellAt(r, 0).ch, 'A' + (tb.lineAtRow(r) % 26));   // cursor row is off screen now
    CHECK_EQ(tb.lineAtRow(tb.cursorRow()), tb.totalLines() - 1);
  }
```

Create `test/test_viewerfeed.cpp`:

```cpp
#include "check.h"
#include <string.h>
#include <string>
#include "viewerfeed.h"
#include "termbuf.h"

namespace {
CbpRecord rec(uint32_t ms, uint8_t dir, const char* text) {
  CbpRecord r;
  r.ms = ms; r.dir = dir; r.len = (uint8_t)strlen(text);
  memcpy(r.data, text, r.len);
  return r;
}
std::string rowText(const TermBuf& tb, int row) {
  std::string s;
  for (int c = 0; c < COLS; c++) s += tb.cellAt(row, c).ch;
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
}
}

SUITE(viewerfeed_tests) {
  {  // received bytes render like the live terminal; sent bytes are cyan and reset afterwards
    TermBuf tb; ViewerFeed vf(tb);
    vf.onRecord(rec(0, CBP_RX, "HIZ> "));
    vf.onRecord(rec(10, CBP_TX, "ls"));
    vf.onRecord(rec(20, CBP_RX, "ok"));
    CHECK_STR(rowText(tb, 0), "HIZ> lsok");
    CHECK_EQ(tb.cellAt(0, 5).ch, 'l'); CHECK_EQ(tb.cellAt(0, 5).color, 6);
    CHECK_EQ(tb.cellAt(0, 6).ch, 's'); CHECK_EQ(tb.cellAt(0, 6).color, 6);
    CHECK_EQ(tb.cellAt(0, 7).ch, 'o'); CHECK_EQ(tb.cellAt(0, 7).color, 7);
  }
  {  // a marker gets its own yellow line
    TermBuf tb; ViewerFeed vf(tb);
    vf.onRecord(rec(0, CBP_RX, "abc"));
    vf.onRecord(rec(50, CBP_MARK, "[link lost]"));
    vf.onRecord(rec(60, CBP_RX, "back"));
    CHECK_STR(rowText(tb, 0), "abc");
    CHECK_STR(rowText(tb, 1), "[link lost]");
    CHECK_EQ(tb.cellAt(1, 0).color, 3);
    CHECK_STR(rowText(tb, 2), "back");
    CHECK_EQ(tb.cellAt(2, 0).color, 7);
  }
  {  // each line is stamped with the record in which its first byte arrived
    TermBuf tb; ViewerFeed vf(tb);
    vf.onRecord(rec(100, CBP_RX, "a\nb"));
    vf.onRecord(rec(250, CBP_RX, "c\n"));
    vf.onRecord(rec(400, CBP_RX, "d"));
    CHECK_EQ(vf.msOfLine(0), 100);
    CHECK_EQ(vf.msOfLine(1), 100);       // "bc" started in the first record
    CHECK_EQ(vf.msOfLine(2), 250);       // created by the LF in the second record
    CHECK_EQ(vf.msOfLine(tb.lineAtRow(0)), 100);
    CHECK_EQ(vf.msOfLine(9999), 0);      // unknown line
  }
  {  // line stamps survive the ring: only the last 400 lines are kept, with their own times
    TermBuf tb; ViewerFeed vf(tb);
    for (int i = 0; i < 1000; i++) {
      char l[16]; snprintf(l, sizeof l, "L%d\n", i);
      vf.onRecord(rec((uint32_t)i * 10, CBP_RX, l));
    }
    long last = tb.totalLines() - 1;
    CHECK_EQ(vf.msOfLine(last - 5), (uint32_t)((last - 5) * 10 - 10));   // line n was created by record n-1's LF
  }
  {  // reset clears the buffer, the stamps and any colour left active
    TermBuf tb; ViewerFeed vf(tb);
    vf.onRecord(rec(500, CBP_RX, "\x1b[31mred\nmore"));
    vf.reset();
    CHECK_STR(rowText(tb, 0), "");
    CHECK_EQ(vf.msOfLine(0), 0);
    vf.onRecord(rec(5, CBP_RX, "x"));
    CHECK_EQ(tb.cellAt(0, 0).color, 7);
    CHECK_EQ(vf.msOfLine(0), 5);
  }
  {  // garbage and unterminated escapes in a record do not corrupt later records
    TermBuf tb; ViewerFeed vf(tb);
    vf.onRecord(rec(0, CBP_RX, "\x1b[3"));
    vf.onRecord(rec(1, CBP_RX, "1mhi"));
    CHECK_STR(rowText(tb, 0), "hi");
  }
}
```

Add `viewerfeed` to `UNITS` in `tools/run-tests.sh`.

Note: in the ring test, record `i` prints `L<i>` then LF, so the LF of record `i` creates line `i+1`, stamped `i*10`. Line `n` is therefore stamped `(n-1)*10`, which is `n*10 - 10`.

- [ ] **Step 2: Run to verify it fails**

Run: `tools/run-tests.sh 2>&1 | tail -4`
Expected: compile error (`lineAtRow` not a member, `viewerfeed.h` missing).

- [ ] **Step 3: Implement**

In `src/termbuf.h`, add to the public section after `int totalLines() const { return (int)(lineNo_ + 1); }`:

```cpp
  long lineAtRow(int row) const { return topLine() + row; }       // absolute line shown in terminal row `row`
```

(`topLine()` is private but a member, so the inline accessor can call it.)

Create `src/viewerfeed.h`:

```cpp
#pragma once
#include <stdint.h>
#include "ansi.h"
#include "config.h"
#include "replayer.h"
#include "termbuf.h"

// Renders session records into a TermBuf (the viewer's own buffer) and remembers when each line began.
class ViewerFeed : public RecordSink {
public:
  explicit ViewerFeed(TermBuf& tb) : tb_(tb), ansi_(tb) { resetStamps(); }
  void onRecord(const CbpRecord& r) override;
  uint32_t msOfLine(long absLine) const;      // arrival time of the line's first byte; 0 if unknown
  void reset();                               // empty buffer, stamps and colour state
private:
  void resetStamps();
  void feedStr(const char* s);
  TermBuf& tb_;
  AnsiParser ansi_;
  uint32_t lineMs_[SCROLLBACK_LINES];
};
```

Create `src/viewerfeed.cpp`:

```cpp
#include "viewerfeed.h"
#include <string.h>

static const uint32_t UNSET = 0xFFFFFFFFu;

void ViewerFeed::resetStamps() { for (int i = 0; i < SCROLLBACK_LINES; i++) lineMs_[i] = UNSET; }

void ViewerFeed::feedStr(const char* s) { ansi_.feed((const uint8_t*)s, strlen(s)); }

void ViewerFeed::reset() {
  tb_.clearScreen();
  resetStamps();
  feedStr("\x1b[0m");
}

uint32_t ViewerFeed::msOfLine(long absLine) const {
  long total = tb_.totalLines();
  if (absLine < 0 || absLine >= total || absLine < total - SCROLLBACK_LINES) return 0;
  uint32_t v = lineMs_[absLine % SCROLLBACK_LINES];
  return v == UNSET ? 0 : v;
}

void ViewerFeed::onRecord(const CbpRecord& r) {
  long before = tb_.totalLines();
  uint32_t& cur = lineMs_[(before - 1) % SCROLLBACK_LINES];
  if (cur == UNSET) cur = r.ms;                               // the line being written starts with this record
  switch (r.dir) {
    case CBP_TX:
      feedStr("\x1b[36m");
      ansi_.feed(r.data, r.len);
      feedStr("\x1b[0m");
      break;
    case CBP_MARK:
      feedStr("\r\n\x1b[33m");
      ansi_.feed(r.data, r.len);
      feedStr("\x1b[0m\r\n");
      break;
    default:
      ansi_.feed(r.data, r.len);
      break;
  }
  for (long n = before; n < tb_.totalLines(); n++) lineMs_[n % SCROLLBACK_LINES] = r.ms;   // lines created in this record
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `tools/run-tests.sh 2>&1 | tail -3`
Expected: `... checks, 0 failures`. If the ring-stamp check fails, remember `before` is `totalLines()` before the record, so the first new line has index `before`.

- [ ] **Step 5: Commit**

Run: `tools/commit.sh "feat: ViewerFeed renders session records with per-line timestamps; TermBuf::lineAtRow"`

---

### Task 7: Button grid 6x2 with REC and LOGS

**Files:**
- Modify: `src/layout.h`, `src/layout.cpp`, `test/test_layout.cpp`

**Interfaces:**
- Produces: `BTN_COLS = 6`, `BTN_COUNT = 12`, `BTN_W = 53`; `BtnKind::Rec`, `BtnKind::Logs`; `CMD_BUTTONS[10]` = REC, `CMD_BUTTONS[11]` = LOGS. `buttonRect(5)` and `buttonRect(11)` are 55 px wide so the row ends at 320.

- [ ] **Step 1: Write the failing tests**

In `test/test_layout.cpp`, replace these existing lines:

```cpp
  CHECK_EQ(buttonAt(0, BTN_Y + 24), 5);
  CHECK_EQ(buttonAt(319, 239), 9);
```

with:

```cpp
  CHECK_EQ(buttonAt(52, BTN_Y), 0);
  CHECK_EQ(buttonAt(53, BTN_Y), 1);
  CHECK_EQ(buttonAt(0, BTN_Y + 24), 6);
  CHECK_EQ(buttonAt(319, 239), 11);
  CHECK_EQ(BTN_COUNT, 12);
  CHECK(CMD_BUTTONS[10].kind == BtnKind::Rec);
  CHECK(CMD_BUTTONS[11].kind == BtnKind::Logs);
  CHECK_EQ(buttonRect(5).x + buttonRect(5).w, SCREEN_W);
  CHECK_EQ(buttonRect(11).x + buttonRect(11).w, SCREEN_W);
```

and replace the last line of the suite

```cpp
  CHECK(popupItemRect(4, 0).x + POPUP_W <= SCREEN_W);   // rightmost button's popup is clamped
```

with

```cpp
  CHECK(popupItemRect(5, 0).x + POPUP_W <= SCREEN_W);   // rightmost command button's popup is clamped
```

- [ ] **Step 2: Run to verify it fails**

Run: `tools/run-tests.sh 2>&1 | tail -4`
Expected: compile error `'Rec' is not a member of 'BtnKind'`.

- [ ] **Step 3: Implement**

In `src/layout.h`: change the enum to `enum class BtnKind : uint8_t { Command, Enter, CtrlC, Keyboard, Rec, Logs };` and the constants line to:

```cpp
constexpr int BTN_COLS = 6, BTN_COUNT = 12, BTN_W = 53, BTN_ROW_H = 24;   // last column is wider: 320 = 5 x 53 + 55
```

In `src/layout.cpp`, append two entries to `CMD_BUTTONS` after the `Enter` entry:

```cpp
  {"REC",    BtnKind::Rec,     0, {}},
  {"LOGS",   BtnKind::Logs,    0, {}},
```

and replace `buttonRect` and `buttonAt`:

```cpp
Rect buttonRect(int idx) {
  int col = idx % BTN_COLS;
  int x = col * BTN_W;
  return Rect{x, BTN_Y + (idx / BTN_COLS) * BTN_ROW_H, col == BTN_COLS - 1 ? SCREEN_W - x : BTN_W, BTN_ROW_H};
}

int buttonAt(int x, int y) {
  if (x < 0 || x >= SCREEN_W || y < BTN_Y || y >= SCREEN_H) return -1;
  int col = x / BTN_W;
  if (col >= BTN_COLS) col = BTN_COLS - 1;
  return ((y - BTN_Y) / BTN_ROW_H) * BTN_COLS + col;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `tools/run-tests.sh 2>&1 | tail -3`
Expected: `... checks, 0 failures`. (The firmware build is checked in Task 8, where `Ui` handles the new kinds.)

- [ ] **Step 5: Commit**

Run: `tools/commit.sh "feat: 6x2 button grid with REC and LOGS buttons"`

---

### Task 8: SD driver, REC button, status-bar indicator, recorder wiring (hardware)

**Files:**
- Create: `src/sdstore.h`, `src/sdstore.cpp`
- Modify: `src/ui.h`, `src/ui.cpp`, `cyd-bitpirate-monitor.ino`

**Interfaces:**
- Consumes: `LogStore`/`FileIO` (Tasks 2-3), `Recorder`, `CbpDir` (Task 3), `BtnKind::Rec/Logs` (Task 7), `SD_*`/`REC_QUEUE_BYTES` constants (Task 1).
- Produces: `SdStore : LogStore`; `Ui::setRecorder(Recorder*)`, `bool Ui::takeLogsRequest()` (true once after the LOGS button was tapped); a running recorder task; link events, received bytes and sent bytes are logged while recording.

Hardware task: the host-tested pieces are already green, so the checks here are the build and the on-device checklist in Step 7.

- [ ] **Step 1: SD store**

Create `src/sdstore.h`:

```cpp
#pragma once
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include "logstore.h"

// FileIO over an Arduino File.
class SdFileIO : public FileIO {
public:
  File f;
  size_t read(uint8_t* out, size_t n) override { return f ? f.read(out, n) : 0; }
  size_t write(const uint8_t* d, size_t n) override { return f ? f.write(d, n) : 0; }
  void flush() override { if (f) f.flush(); }
  size_t size() override { return f ? f.size() : 0; }
};

// Session files in /logs on the CYD's microSD card. One writer (the recorder task) and one reader (the LOGS screen).
class SdStore : public LogStore {
public:
  bool available() override;
  FileIO* createNew(char* nameOut, size_t cap) override;
  FileIO* openRead(const char* name) override;
  void closeFile(FileIO* f) override;
  int list(LogInfo* out, int max) override;
  bool remove(const char* name) override;
  void markFailed() override;
private:
  bool mount();
  static bool parseName(const char* name, int* num);       // "0007.cbp" -> 7
  SPIClass spi_{VSPI};
  SdFileIO wr_, rd_;
  volatile bool mounted_ = false;
  uint32_t lastTry_ = 0;
};
```

Create `src/sdstore.cpp`:

```cpp
#include "sdstore.h"
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

bool SdStore::mount() {
  spi_.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, spi_, SD_HZ)) { SD.end(); return false; }
  if (!SD.exists("/logs") && !SD.mkdir("/logs")) { SD.end(); return false; }
  return true;
}

bool SdStore::available() {
  if (mounted_) return true;
  if (lastTry_ != 0 && millis() - lastTry_ < 2000) return false;     // do not hammer an empty slot
  lastTry_ = millis();
  mounted_ = mount();
  return mounted_;
}

void SdStore::markFailed() {
  SD.end();
  mounted_ = false;
  lastTry_ = 0;                                                      // remount on the next request
}

bool SdStore::parseName(const char* n, int* num) {
  if (strlen(n) != 8 || strcmp(n + 4, ".cbp") != 0) return false;
  for (int i = 0; i < 4; i++) if (n[i] < '0' || n[i] > '9') return false;
  if (num) *num = atoi(n);
  return true;
}

FileIO* SdStore::createNew(char* nameOut, size_t cap) {
  if (!available()) return nullptr;
  int hi = 0;
  File dir = SD.open("/logs");
  if (!dir) { markFailed(); return nullptr; }
  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    int n;
    if (parseName(e.name(), &n) && n > hi) hi = n;
  }
  dir.close();
  if (hi >= 9999) return nullptr;
  char name[16], path[32];
  snprintf(name, sizeof name, "%04d.cbp", hi + 1);
  snprintf(path, sizeof path, "/logs/%s", name);
  wr_.f = SD.open(path, FILE_WRITE);
  if (!wr_.f) { markFailed(); return nullptr; }
  snprintf(nameOut, cap, "%s", name);
  return &wr_;
}

FileIO* SdStore::openRead(const char* name) {
  if (!parseName(name, nullptr) || !available()) return nullptr;
  char path[32];
  snprintf(path, sizeof path, "/logs/%s", name);
  rd_.f = SD.open(path, FILE_READ);
  return rd_.f ? &rd_ : nullptr;
}

void SdStore::closeFile(FileIO* f) {
  if (f) static_cast<SdFileIO*>(f)->f.close();
}

int SdStore::list(LogInfo* out, int max) {
  if (!available()) return 0;
  if (max > 64) max = 64;
  int nums[64], count = 0;
  uint32_t sizes[64];
  File dir = SD.open("/logs");
  if (!dir) return 0;
  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    int n;
    if (!parseName(e.name(), &n)) continue;
    int pos = count;                                                 // keep the newest `max` numbers, descending
    while (pos > 0 && nums[pos - 1] < n) pos--;
    if (pos >= max) continue;
    for (int i = (count < max ? count : max - 1); i > pos; i--) { nums[i] = nums[i - 1]; sizes[i] = sizes[i - 1]; }
    nums[pos] = n;
    sizes[pos] = (uint32_t)e.size();
    if (count < max) count++;
  }
  dir.close();
  for (int i = 0; i < count; i++) {
    snprintf(out[i].name, sizeof out[i].name, "%04d.cbp", nums[i]);
    out[i].size = sizes[i];
  }
  return count;
}

bool SdStore::remove(const char* name) {
  if (!parseName(name, nullptr) || !available()) return false;
  char path[32];
  snprintf(path, sizeof path, "/logs/%s", name);
  return SD.remove(path);
}
```

- [ ] **Step 2: UI header changes**

In `src/ui.h`:

1. Add `#include "recorder.h"` after `#include "render.h"`.
2. In the `public:` section, after `void update(uint32_t nowMs);` add:

```cpp
  void setRecorder(Recorder* r) { rec_ = r; }
  bool takeLogsRequest() { bool r = logsReq_; logsReq_ = false; return r; }   // LOGS button tapped since the last call
```

3. In the `private:` function list, after `void togglePause();` add:

```cpp
  void toggleRec();
  void logTx(const uint8_t* d, size_t n);
```

4. After `uint32_t lastDraw_ = 0;` add:

```cpp
  Recorder* rec_ = nullptr;
  bool logsReq_ = false;
  bool shownRecOn_ = false;
  int shownRec_ = -2;
```

- [ ] **Step 3: UI source changes**

In `src/ui.cpp`:

1. Add `#include <stdio.h>` after `#include <string.h>`.
2. Replace `sendByte` and `sendText` with:

```cpp
void Ui::logTx(const uint8_t* d, size_t n) { if (rec_) rec_->log(millis(), CBP_TX, d, n); }

void Ui::sendByte(uint8_t b) {
  if (link_.state() != LinkState::Connected) return;      // spec: keys typed while disconnected are dropped
  if (!term_.frozen()) term_.scrollToLive();
  if (link_.send(&b, 1)) logTx(&b, 1);
}

void Ui::sendText(const char* s) {
  if (link_.state() != LinkState::Connected) return;
  if (!term_.frozen()) term_.scrollToLive();
  if (link_.send((const uint8_t*)s, strlen(s))) logTx((const uint8_t*)s, strlen(s));
}

void Ui::toggleRec() {
  if (!rec_) return;
  if (rec_->recording()) rec_->requestStop();
  else rec_->requestStart(millis());
}
```

3. In `onTap`, extend the `switch (cb.kind)` with two cases after `case BtnKind::Keyboard: openKeyboard(); break;`:

```cpp
    case BtnKind::Rec: toggleRec(); break;
    case BtnKind::Logs: logsReq_ = true; break;
```

4. Replace `drawButtons` with:

```cpp
void Ui::drawButtons() {
  tft_.setTextDatum(MC_DATUM);
  bool recOn = rec_ && rec_->recording();
  for (int i = 0; i < BTN_COUNT; i++) {
    Rect r = buttonRect(i);
    const char* label = CMD_BUTTONS[i].label;
    uint16_t bg = C_BTN;
    if (CMD_BUTTONS[i].kind == BtnKind::Rec && recOn) { label = "STOP"; bg = C_BAD; }
    tft_.fillRect(r.x, r.y, r.w, r.h, C_BG);
    tft_.fillRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 4, bg);
    tft_.setTextColor(C_TEXT, bg);
    tft_.drawString(label, r.x + r.w / 2, r.y + r.h / 2, 2);
  }
}
```

5. In `drawStatus`, replace the block from `int rightCode = ...` through the `strncpy(shownMode_, ...)` line with:

```cpp
  int rightCode = term_.frozen() ? 2 : (term_.viewOffset() > 0 ? 1 : 0);
  const char* mode = mode_[0] ? mode_ : "-";
  int recCode = 0;
  char recTxt[16] = "";
  if (rec_) {
    if (rec_->state() == RecState::Error) { recCode = -1; strcpy(recTxt, "REC ERR"); }
    else if (rec_->recording()) {
      unsigned s = rec_->elapsedMs(millis()) / 1000;
      recCode = 1 + (int)s;
      snprintf(recTxt, sizeof recTxt, "REC %02u:%02u", s / 60, s % 60);
    }
  }
  if (!force && linkCode == shownLink_ && rightCode == shownRight_ && recCode == shownRec_ && strcmp(mode, shownMode_) == 0) return;
  shownLink_ = linkCode; shownRight_ = rightCode; shownRec_ = recCode;
  strncpy(shownMode_, mode, sizeof shownMode_ - 1);
```

and after the line that draws the link text (`tft_.drawString(link, 4, STATUS_H / 2, 1);`) add on a new line:

```cpp
  tft_.setTextDatum(ML_DATUM); tft_.setTextColor(C_BAD, C_BAR);  tft_.drawString(recTxt, 60, STATUS_H / 2, 1);
```

6. In `Ui::update`, add at the top of the function body (before `char line[COLS + 1], m[16];`):

```cpp
  bool recOn = rec_ && rec_->recording();
  if (recOn != shownRecOn_) {                           // REC <-> STOP button face
    shownRecOn_ = recOn;
    if (!kbOpen_ && popupBtn_ < 0) drawButtons();
  }
```

- [ ] **Step 4: Sketch changes**

In `cyd-bitpirate-monitor.ino`:

1. After `#include "src/render.h"` add `#include "src/recorder.h"` and `#include "src/sdstore.h"`.
2. After `static Ui ui(tft, term, renderer, s3link);` add:

```cpp
static SdStore sdStore;
static Recorder rec(sdStore, REC_QUEUE_BYTES);
```

3. After the `linkTask` function add:

```cpp
static void recTask(void*) {
  for (;;) { rec.service(millis()); vTaskDelay(pdMS_TO_TICKS(5)); }
}
```

4. In `setup()`, replace `ui.begin();` with:

```cpp
  ui.setRecorder(&rec);
  ui.begin();
```

and after the `xTaskCreatePinnedToCore(linkTask, ...)` line add:

```cpp
  xTaskCreatePinnedToCore(recTask, "rec", 6144, nullptr, 1, nullptr, 1);
```

5. In `loop()`, replace the link-state block and the receive lines (from `LinkState st = s3link.state();` through `if (dropped) feedText("\r\n[overflow]\r\n");`) with:

```cpp
  LinkState st = s3link.state();
  if (st != prev) {
    if (st == LinkState::Lost) { feedText("\r\n[link lost]\r\n"); rec.mark(now, "[link lost]"); }
    else if (st == LinkState::Connected) { feedText("\r\n[link up]\r\n"); rec.mark(now, "[link up]"); }
    prev = st;
  }

  uint8_t buf[256]; size_t n;
  while ((n = rx.pop(buf, sizeof buf)) > 0) { ansi.feed(buf, n); rec.log(now, CBP_RX, buf, n); }
  size_t dropped = rx.takeDropped();
  if (dropped) { feedText("\r\n[overflow]\r\n"); rec.mark(now, "[overflow]"); }
```

6. In the `#ifdef SOAK_HELP` block, replace the send line with:

```cpp
  if (now - lastHelp >= 20000) { lastHelp = now; if (s3link.send((const uint8_t*)"help\r", 5)) rec.log(now, CBP_TX, (const uint8_t*)"help\r", 5); }
```

- [ ] **Step 5: Build, check size and heap budget**

Run: `tools/run-tests.sh 2>&1 | tail -1 && tools/build.sh 2>&1 | tail -3`
Expected: tests `0 failures`; build prints `Sketch uses ... bytes (8x%)` and `Global variables use ...`. Record both numbers. If flash exceeds 95%, stop and report to the user.

- [ ] **Step 6: Flash**

Run: `fuser -v /dev/ttyUSB0; PORT=/dev/ttyUSB0 tools/flash.sh 2>&1 | tail -2`
Expected: `Hard resetting via RTS pin`.

- [ ] **Step 7: On-device checks (ask the user at the CYD, card inserted, S3 hotspot on)**

1. The button row shows 12 buttons: `mode help scan read write sniff / config ^C KB Enter REC LOGS`; the first ten still work (tap `help`, `Enter`).
2. Tap `REC`: the button turns red `STOP` and the status bar shows `REC 00:0x` counting seconds.
3. Tap `help`, `Enter`, wait for the dump, tap `STOP`: red indicator and counter disappear.
4. With no card (remove it, power-cycle), tap `REC`: the status bar shows `REC ERR`, the terminal and buttons keep working. Insert the card, tap `REC` again: recording starts (the card is retried within 2 s).
5. Power down, put the card in the desktop and run: `ls -l /media/$USER/*/logs/ && strings /media/$USER/*/logs/0001.cbp | head`
   Expected: `0001.cbp` of a few KB; `strings` shows `CBP1`, `[rec start]`, `help` and the `HIZ>` menu text.
6. Read the heap lines from serial while recording: `grep heap` shows `min=` above 100000.

If any check fails, use systematic-debugging; do not commit a failing build.

- [ ] **Step 8: Commit**

Run: `tools/commit.sh "feat: REC button, SD store and recorder task wired into the terminal"`

---

### Task 9: LOGS screen: list, viewer, replay, delete (hardware)

**Files:**
- Create: `src/logsscreen.h`, `src/logsscreen.cpp`
- Modify: `cyd-bitpirate-monitor.ino`

**Interfaces:**
- Consumes: `LogStore`, `Recorder`, `LogEntry`/`buildLogIndex`/`fmt*` (Task 4), `Replayer`/`RecordSink` (Task 5), `ViewerFeed`, `TermBuf::lineAtRow` (Task 6), `buttonRect`/`buttonAt` (Task 7), `Renderer`, `GestureEvent`, `Ui::takeLogsRequest` (Task 8).
- Produces: `class LogsScreen(TFT_eSPI&, Renderer&, LogStore&, Recorder&)` with `open()`, `active()`, `handle(const GestureEvent&, uint32_t nowMs)`, `update(uint32_t nowMs)`.

Screen layout (both modes): header bar y 0..15 (reuses the status-bar area); content y 16..191; control row y 192..239 using the first five cells of the `buttonRect` grid.

- List: 7 rows of 24 px. Controls: cell 0 = `Back`.
- Viewer: terminal area drawn by the `Renderer` from the viewer's own `TermBuf`. Controls: cell 0 Play/Pause/Resume, cell 1 speed (`1x`/`4x`/`16x`), cell 2 Stop, cell 3 Del (`SURE?` for 3 s after the first tap), cell 4 Back. Tapping a terminal line shows its timestamp in the header. Dragging scrolls the viewer.
- Viewer states: `Loading` (incrementally fast-forwards the file into the buffer, 48 records per `update`), `Static` (fully loaded, the last 400 lines), `Playing`, `Paused`.

- [ ] **Step 1: Header**

Create `src/logsscreen.h`:

```cpp
#pragma once
#include <TFT_eSPI.h>
#include "cbpformat.h"
#include "gesture.h"
#include "layout.h"
#include "logindex.h"
#include "logstore.h"
#include "recorder.h"
#include "render.h"
#include "replayer.h"
#include "termbuf.h"
#include "viewerfeed.h"

class LogsScreen {
public:
  LogsScreen(TFT_eSPI& tft, Renderer& rend, LogStore& store, Recorder& rec)
    : tft_(tft), rend_(rend), store_(store), rec_(rec) {}
  void open();                                        // build the list and take over the screen
  bool active() const { return mode_ != Mode::Closed; }
  void handle(const GestureEvent& e, uint32_t nowMs);
  void update(uint32_t nowMs);

private:
  enum class Mode : uint8_t { Closed, List, View };
  enum class VState : uint8_t { Loading, Static, Playing, Paused };
  static constexpr int LIST_MAX = 20, LIST_Y = STATUS_H, LIST_ROW_H = 24, LIST_ROWS = 7;
  static constexpr uint32_t CONFIRM_MS = 3000;

  void showList();
  void drawList();
  void drawHeader(const char* text, const char* right);
  void drawControls();
  void tapList(int x, int y);
  void tapView(int x, int y, uint32_t now);
  void openView(int idx);
  void startLoad();
  void startReplay(uint32_t now);
  void closeView();
  void closeAll();
  void headerForView(uint32_t now);
  bool allocViewer();
  void freeViewer();
  void closeFile();

  TFT_eSPI& tft_;
  Renderer& rend_;
  LogStore& store_;
  Recorder& rec_;
  Mode mode_ = Mode::Closed;
  VState vs_ = VState::Static;
  LogEntry entries_[LIST_MAX];
  int count_ = 0, listTop_ = 0;
  char msg_[24] = {0};
  int cur_ = -1;                                       // index of the open file in entries_
  FileIO* file_ = nullptr;
  CbpReader rd_;                                       // used while loading
  Replayer rp_;
  TermBuf* vt_ = nullptr;
  ViewerFeed* feed_ = nullptr;
  int speed_ = 1;
  bool confirmDel_ = false;
  uint32_t confirmAt_ = 0, lastDraw_ = 0, lastHdr_ = 0;
  uint32_t stampMs_ = 0;
  bool stampShown_ = false;
};
```

- [ ] **Step 2: Implementation**

Create `src/logsscreen.cpp`:

```cpp
#include "logsscreen.h"
#include <esp_heap_caps.h>
#include <new>
#include <stdio.h>
#include <string.h>

namespace {
constexpr uint16_t C_BG = 0x0000, C_BAR = 0x18E3, C_BTN = 0x2945, C_ROW = 0x10A2;
constexpr uint16_t C_TEXT = 0xFFFF, C_DIM = 0x8410, C_WARN = 0xFFE0, C_BAD = 0xF800, C_OK = 0x07E0;
constexpr size_t MIN_HEAP_AFTER_VIEWER = 30000;       // keep this much free after the viewer buffer is allocated
}

// ---- open / close ---------------------------------------------------------------------------

void LogsScreen::open() {
  tft_.fillScreen(C_BG);
  drawHeader("LOGS", "reading card...");
  count_ = buildLogIndex(store_, entries_, LIST_MAX);
  listTop_ = 0;
  msg_[0] = 0;
  if (!store_.available()) strcpy(msg_, "No SD card");
  else if (count_ == 0) strcpy(msg_, "No recordings yet");
  mode_ = Mode::List;
  showList();
}

void LogsScreen::closeAll() {
  closeFile();
  freeViewer();
  mode_ = Mode::Closed;                                // the caller redraws the live screen (Ui::begin)
}

void LogsScreen::closeFile() {
  if (file_) { store_.closeFile(file_); file_ = nullptr; }
}

bool LogsScreen::allocViewer() {
  if (vt_) return true;
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < sizeof(TermBuf) + sizeof(ViewerFeed) + MIN_HEAP_AFTER_VIEWER) return false;
  vt_ = new (std::nothrow) TermBuf();
  if (!vt_) return false;
  feed_ = new (std::nothrow) ViewerFeed(*vt_);
  if (!feed_) { delete vt_; vt_ = nullptr; return false; }
  return true;
}

void LogsScreen::freeViewer() {
  delete feed_; feed_ = nullptr;
  delete vt_; vt_ = nullptr;
}

// ---- list -----------------------------------------------------------------------------------

void LogsScreen::showList() {
  tft_.fillRect(0, STATUS_H, SCREEN_W, SCREEN_H - STATUS_H, C_BG);
  char h[24];
  snprintf(h, sizeof h, "LOGS (%d)", count_);
  drawHeader(h, "");
  drawList();
  drawControls();
}

void LogsScreen::drawList() {
  tft_.fillRect(0, LIST_Y, SCREEN_W, LIST_ROWS * LIST_ROW_H, C_BG);
  tft_.setTextDatum(ML_DATUM);
  if (msg_[0] && count_ == 0) {
    tft_.setTextColor(C_DIM, C_BG);
    tft_.drawString(msg_, 8, LIST_Y + 20, 2);
    return;
  }
  for (int r = 0; r < LIST_ROWS; r++) {
    int i = listTop_ + r;
    if (i >= count_) break;
    const LogEntry& e = entries_[i];
    int y = LIST_Y + r * LIST_ROW_H;
    tft_.fillRect(2, y + 1, SCREEN_W - 4, LIST_ROW_H - 2, C_ROW);
    char sz[16], du[16], line[48];
    fmtSize(e.size, sz, sizeof sz);
    fmtDuration(e.durationMs, du, sizeof du);
    if (e.valid) snprintf(line, sizeof line, "%s  %s  %s", e.name, sz, du);
    else snprintf(line, sizeof line, "%s  (not a session file)", e.name);
    tft_.setTextColor(e.valid ? C_TEXT : C_DIM, C_ROW);
    tft_.drawString(line, 8, y + LIST_ROW_H / 2, 2);
  }
}

void LogsScreen::tapList(int x, int y) {
  int b = buttonAt(x, y);
  if (b == 0) { closeAll(); return; }
  if (y >= LIST_Y && y < LIST_Y + LIST_ROWS * LIST_ROW_H) {
    int i = listTop_ + (y - LIST_Y) / LIST_ROW_H;
    if (i < count_) openView(i);
  }
}

// ---- viewer ---------------------------------------------------------------------------------

void LogsScreen::openView(int idx) {
  if (!allocViewer()) { strcpy(msg_, "low memory"); drawHeader("LOGS", msg_); return; }
  cur_ = idx;
  confirmDel_ = false; stampShown_ = false; speed_ = 1;
  mode_ = Mode::View;
  rend_.setVisibleRows(ROWS);
  rend_.invalidate();
  tft_.fillRect(0, STATUS_H, SCREEN_W, SCREEN_H - STATUS_H, C_BG);
  startLoad();
  drawControls();
}

void LogsScreen::startLoad() {                         // (re)build the static view by reading the whole file
  closeFile();
  feed_->reset();
  rend_.invalidate();
  file_ = store_.openRead(entries_[cur_].name);
  if (!file_ || !rd_.open(*file_)) {                   // missing, or not a session file: show an empty view
    vs_ = VState::Static;
    if (file_) closeFile();
    strcpy(msg_, file_ || entries_[cur_].valid ? "cannot read" : "not a session file");
    return;
  }
  msg_[0] = 0;
  vs_ = VState::Loading;
}

void LogsScreen::startReplay(uint32_t now) {
  closeFile();
  feed_->reset();
  rend_.invalidate();
  file_ = store_.openRead(entries_[cur_].name);
  if (!file_ || !rp_.begin(*file_)) { vs_ = VState::Static; closeFile(); return; }
  rp_.setSpeed(speed_);
  rp_.play(now);
  vs_ = VState::Playing;
}

void LogsScreen::closeView() {
  closeFile();
  freeViewer();
  mode_ = Mode::List;
  rend_.invalidate();
  showList();
}

void LogsScreen::tapView(int x, int y, uint32_t now) {
  int b = buttonAt(x, y);
  if (b >= 0 && b <= 4) {
    switch (b) {
      case 0:                                          // play / pause / resume
        if (vs_ == VState::Playing) { rp_.pause(); vs_ = VState::Paused; }
        else if (vs_ == VState::Paused) { rp_.play(now); vs_ = VState::Playing; }
        else if (vs_ == VState::Static) startReplay(now);
        break;
      case 1:                                          // speed 1x -> 4x -> 16x
        speed_ = speed_ == 1 ? 4 : (speed_ == 4 ? 16 : 1);
        rp_.setSpeed(speed_);
        break;
      case 2:                                          // stop: back to the fully loaded view
        if (vs_ == VState::Playing || vs_ == VState::Paused) { rp_.stop(); startLoad(); }
        break;
      case 3:                                          // delete, confirmed by a second tap
        if (rec_.recording() && strcmp(entries_[cur_].name, rec_.fileName()) == 0) {
          strcpy(msg_, "recording this file");
        } else if (!confirmDel_) {
          confirmDel_ = true; confirmAt_ = now;
        } else {
          closeFile();
          char name[13];
          strcpy(name, entries_[cur_].name);
          store_.remove(name);
          confirmDel_ = false;
          freeViewer();
          count_ = buildLogIndex(store_, entries_, LIST_MAX);
          listTop_ = 0;
          mode_ = Mode::List;
          msg_[0] = 0;
          rend_.invalidate();
          showList();
          return;
        }
        break;
      case 4: closeView(); return;
    }
    drawControls();
    return;
  }
  if (y >= TERM_Y && y < BTN_Y && vt_) {               // tap a line: show its timestamp
    long line = vt_->lineAtRow((y - TERM_Y) / CHAR_H);
    stampMs_ = feed_->msOfLine(line);
    stampShown_ = true;
  }
}

void LogsScreen::headerForView(uint32_t now) {
  char name[24], right[28];
  snprintf(name, sizeof name, "%s", entries_[cur_].name);
  right[0] = 0;
  if (msg_[0]) snprintf(right, sizeof right, "%s", msg_);
  else if (stampShown_) { char t[16]; fmtElapsed(stampMs_, t, sizeof t); snprintf(right, sizeof right, "line %s", t); }
  else if (vs_ == VState::Loading) snprintf(right, sizeof right, "LOADING");
  else if (vs_ == VState::Playing || vs_ == VState::Paused) {
    char t[16]; fmtElapsed(rp_.positionMs(), t, sizeof t);
    snprintf(right, sizeof right, "%s %s", vs_ == VState::Paused ? "PAUSED" : "PLAY", t);
  } else { char t[16]; fmtDuration(entries_[cur_].durationMs, t, sizeof t); snprintf(right, sizeof right, "%s", t); }
  drawHeader(name, right);
  lastHdr_ = now;
}

// ---- common drawing ---------------------------------------------------------------------------

void LogsScreen::drawHeader(const char* text, const char* right) {
  tft_.fillRect(0, 0, SCREEN_W, STATUS_H, C_BAR);
  tft_.setTextDatum(ML_DATUM); tft_.setTextColor(C_TEXT, C_BAR); tft_.drawString(text, 4, STATUS_H / 2, 1);
  tft_.setTextDatum(MR_DATUM); tft_.setTextColor(C_WARN, C_BAR); tft_.drawString(right, SCREEN_W - 4, STATUS_H / 2, 1);
}

void LogsScreen::drawControls() {
  tft_.fillRect(0, BTN_Y, SCREEN_W, BTN_H, C_BG);
  tft_.setTextDatum(MC_DATUM);
  const char* labels[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
  char sp[8];
  uint16_t bg[5] = {C_BTN, C_BTN, C_BTN, C_BTN, C_BTN};
  if (mode_ == Mode::List) {
    labels[0] = "Back";
  } else {
    labels[0] = vs_ == VState::Playing ? "Pause" : (vs_ == VState::Paused ? "Resume" : "Play");
    snprintf(sp, sizeof sp, "%dx", speed_);
    labels[1] = sp;
    labels[2] = "Stop";
    labels[3] = confirmDel_ ? "SURE?" : "Del";
    if (confirmDel_) bg[3] = C_BAD;
    labels[4] = "Back";
  }
  for (int i = 0; i < 5; i++) {
    if (!labels[i]) continue;
    Rect r = buttonRect(i);
    tft_.fillRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 4, bg[i]);
    tft_.setTextColor(C_TEXT, bg[i]);
    tft_.drawString(labels[i], r.x + r.w / 2, r.y + r.h / 2, 2);
  }
}

// ---- events and frame update ---------------------------------------------------------------------

void LogsScreen::handle(const GestureEvent& e, uint32_t nowMs) {
  switch (e.type) {
    case GestureType::Tap:
      if (mode_ == Mode::List) tapList(e.x, e.y);
      else if (mode_ == Mode::View) tapView(e.x, e.y, nowMs);
      break;
    case GestureType::Scroll:
      if (mode_ == Mode::List) {
        int maxTop = count_ > LIST_ROWS ? count_ - LIST_ROWS : 0;
        listTop_ -= e.lines;                           // lines > 0 means "toward older": dragging down shows earlier rows
        if (listTop_ < 0) listTop_ = 0;
        if (listTop_ > maxTop) listTop_ = maxTop;
        drawList();
      } else if (mode_ == Mode::View && vt_ && e.y >= TERM_Y && e.y < BTN_Y) {
        vt_->scrollBy(e.lines);
      }
      break;
    default: break;
  }
}

void LogsScreen::update(uint32_t now) {
  if (mode_ != Mode::View || !vt_) return;
  if (confirmDel_ && now - confirmAt_ >= CONFIRM_MS) { confirmDel_ = false; drawControls(); }

  if (vs_ == VState::Loading) {
    CbpRecord r;
    for (int i = 0; i < 48; i++) {
      if (!rd_.next(r)) { closeFile(); vs_ = VState::Static; drawControls(); headerForView(now); break; }
      feed_->onRecord(r);
    }
  } else if (vs_ == VState::Playing) {
    rp_.tick(now, *feed_);
    if (rp_.finished()) { closeFile(); vs_ = VState::Static; drawControls(); headerForView(now); }
  }

  if (now - lastHdr_ >= 100) headerForView(now);
  if (now - lastDraw_ >= 33) {
    rend_.draw(*vt_, false);
    lastDraw_ = now;
  }
}
```

- [ ] **Step 3: Wire it into the sketch**

In `cyd-bitpirate-monitor.ino`:

1. After `#include "src/recorder.h"` add `#include "src/logsscreen.h"`.
2. After `static Recorder rec(sdStore, REC_QUEUE_BYTES);` add `static LogsScreen logs(tft, renderer, sdStore, rec);`.
3. In `loop()`, replace the two lines

```cpp
  ui.handle(gestures.update(s.pressed, s.x, s.y, now));
  ui.update(now);
```

with:

```cpp
  GestureEvent ge = gestures.update(s.pressed, s.x, s.y, now);
  static bool logsWas = false;
  if (logs.active()) {
    logs.handle(ge, now);
    logs.update(now);
  } else {
    if (logsWas) ui.begin();                          // LOGS just closed: repaint the live screen
    ui.handle(ge);
    ui.update(now);
    if (ui.takeLogsRequest()) logs.open();
  }
  logsWas = logs.active();
```

- [ ] **Step 4: Build and check size**

Run: `tools/run-tests.sh 2>&1 | tail -1 && tools/build.sh 2>&1 | tail -3`
Expected: `0 failures` and a successful build. Record the flash percentage. If flash is above 95%, stop and report.

- [ ] **Step 5: Flash and check on the CYD (card inserted, with at least one recording from Task 8)**

Run: `fuser -v /dev/ttyUSB0; PORT=/dev/ttyUSB0 tools/flash.sh 2>&1 | tail -2`
Ask the user to check, in this order:
1. Tap `LOGS`: a list with the recording(s) newest first, each row `0001.cbp  4.2 KB  00:31`. With no card it shows `No SD card`; with an empty card `No recordings yet`. `Back` returns to the normal terminal with buttons intact.
2. Tap a file: the old session appears in the terminal area (`HIZ>`, the help menu), header shows the name and total duration. Drag up/down: it scrolls.
3. Tap a terminal line: the header shows `line 00:03.120` style time.
4. `Play`: the screen clears and the session replays; header shows `PLAY 00:xx.xxx`. `1x` button cycles to `4x`, `16x` and the replay speeds up. `Pause` freezes it, `Resume` continues. `Stop` returns to the fully loaded view.
5. Bytes you typed during the recording appear in cyan; `[rec start]`, `[link lost]` markers on their own yellow lines.
6. `Del` turns into red `SURE?`; waiting 3 s reverts it; tapping `Del` twice deletes the file and returns to the list without it.
7. While a recording is running, open LOGS, open the file being recorded and tap `Del` twice: header shows `recording this file` and nothing is deleted. Back out; REC keeps counting.
8. `Back` twice returns to the live terminal; live output that arrived while in LOGS is present in the scrollback.

If a check fails, use systematic-debugging; do not commit a failing build.

- [ ] **Step 6: Commit**

Run: `tools/commit.sh "feat: LOGS screen with list, viewer, timed replay and delete"`

---

### Task 10: PC converter `tools/cbp2txt.py`

**Files:**
- Create: `tools/cbp2txt.py`, `tools/test_cbp2txt.py`
- Modify: `tools/run-tests.sh`

**Interfaces:**
- Consumes: the file format from Task 2 (`CBP1`, records `<IBB` + payload).
- Produces: `records(data) -> iterator of (ms, dir, payload)` (stops quietly at a truncated or invalid record, raises `ValueError("not a session file")` on a bad header); `convert(data) -> str` (one line per non-empty text fragment: `[MM:SS.mmm] <tag> text`, tag `<` received, `>` sent, `*` marker, ANSI escapes and carriage returns removed); command line `tools/cbp2txt.py FILE.cbp [> out.txt]`, exit code 2 with a message on a bad file.

- [ ] **Step 1: Write the failing tests**

Create `tools/test_cbp2txt.py`:

```python
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cbp2txt


def rec(ms, direction, payload):
    return struct.pack("<IBB", ms, direction, len(payload)) + payload


class Cbp2Txt(unittest.TestCase):
    def test_converts_directions_and_strips_ansi(self):
        data = (b"CBP1" + rec(0, 2, b"[rec start]") + rec(1500, 0, b"\x1b[32mHIZ>\x1b[0m \r\n")
                + rec(61234, 1, b"help\r") + rec(61300, 0, b"line one\r\nline two\r\n"))
        self.assertEqual(
            cbp2txt.convert(data),
            "[00:00.000] * [rec start]\n"
            "[00:01.500] < HIZ>\n"
            "[01:01.234] > help\n"
            "[01:01.300] < line one\n"
            "[01:01.300] < line two\n")

    def test_truncated_tail_is_ignored(self):
        data = b"CBP1" + rec(10, 0, b"one") + rec(20, 0, b"two")
        self.assertEqual(cbp2txt.convert(data[:-2]), "[00:00.010] < one\n")

    def test_invalid_record_stops_output_without_error(self):
        data = b"CBP1" + rec(10, 0, b"one") + struct.pack("<IBB", 20, 9, 2) + b"xx" + rec(30, 0, b"three")
        self.assertEqual(cbp2txt.convert(data), "[00:00.010] < one\n")

    def test_bad_header_raises(self):
        with self.assertRaises(ValueError):
            cbp2txt.convert(b"NOPE" + rec(0, 0, b"x"))
        with self.assertRaises(ValueError):
            cbp2txt.convert(b"")

    def test_header_only_gives_empty_text(self):
        self.assertEqual(cbp2txt.convert(b"CBP1"), "")

    def test_non_utf8_bytes_do_not_crash(self):
        out = cbp2txt.convert(b"CBP1" + rec(0, 0, b"ab\xff\xfecd"))
        self.assertIn("ab", out)
        self.assertIn("cd", out)


if __name__ == "__main__":
    unittest.main()
```

Add this line at the end of `tools/run-tests.sh` (after `./build/host-tests`):

```bash
python3 -I tools/test_cbp2txt.py
```

- [ ] **Step 2: Run to verify it fails**

Run: `python3 -I tools/test_cbp2txt.py 2>&1 | tail -3`
Expected: `ModuleNotFoundError: No module named 'cbp2txt'`.

- [ ] **Step 3: Implement**

Create `tools/cbp2txt.py` and make it executable (`chmod +x tools/cbp2txt.py`):

```python
#!/usr/bin/env python3
"""Convert a CYD session file (.cbp) to readable text with timestamps.

Usage: cbp2txt.py FILE.cbp [> out.txt]
Lines look like  [MM:SS.mmm] <tag> text   with tag  <  received,  >  sent,  *  marker.
"""
import re
import struct
import sys

ANSI = re.compile(rb"\x1b(?:\[[0-?]*[ -/]*[@-~]|[@-Z\\-_])")
TAGS = {0: "<", 1: ">", 2: "*"}


def records(data):
    if data[:4] != b"CBP1":
        raise ValueError("not a session file")
    pos = 4
    while pos + 6 <= len(data):
        ms, direction, length = struct.unpack_from("<IBB", data, pos)
        if direction > 2 or length == 0 or pos + 6 + length > len(data):
            return                                   # truncated or corrupt tail: keep everything before it
        yield ms, direction, data[pos + 6:pos + 6 + length]
        pos += 6 + length


def stamp(ms):
    return "%02d:%02d.%03d" % (ms // 60000, ms // 1000 % 60, ms % 1000)


def convert(data):
    out = []
    for ms, direction, payload in records(data):
        text = ANSI.sub(b"", payload).replace(b"\r", b"")
        for part in text.split(b"\n"):
            line = part.decode("utf-8", "replace").rstrip()
            if line:
                out.append("[%s] %s %s" % (stamp(ms), TAGS[direction], line))
    return "\n".join(out) + ("\n" if out else "")


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 1
    try:
        with open(argv[1], "rb") as f:
            sys.stdout.write(convert(f.read()))
    except (OSError, ValueError) as e:
        print("cbp2txt: %s: %s" % (argv[1], e), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
```

- [ ] **Step 4: Run to verify it passes**

Run: `python3 -I tools/test_cbp2txt.py 2>&1 | tail -3 && tools/run-tests.sh 2>&1 | tail -3`
Expected: `OK` from unittest (6 tests), then the host tests report `0 failures`.

- [ ] **Step 5: Commit**

Run: `tools/commit.sh "feat: cbp2txt converts session files to timestamped text on the PC"`

---

### Task 11: Hardware checklist, soak, docs and cleanup

**Files:**
- Modify: `README.md`, `docs/hardware.md` (only if numbers changed)

**Interfaces:**
- Consumes: everything above. Produces: the evidence for "done" in the spec (checklist passes, 10-minute soak, measured heap).

This task needs the user for the hardware steps (card inserted, S3 hotspot on, `/dev/ttyUSB0` free).

- [ ] **Step 1: Full host test run**

Run: `tools/run-tests.sh 2>&1 | tail -3`
Expected: `0 failures` and the unittest `OK`.

- [ ] **Step 2: Normal-build checklist (the user at the CYD; run each, report which passed)**

1. Join and terminal still work exactly as before: `help`, `Enter`, `KB` typing, scroll, pause, long-press clear.
2. Record `help` and `scan` output, stop. LOGS lists it with plausible size and duration.
3. View it, scroll, tap a line for its time, replay at 1x, 4x, 16x, pause, resume, stop.
4. Delete confirmation works; deleting the file being recorded is refused.
5. Power-cycle the S3 while recording: the recording continues, the file contains `[link lost]` and `[link up]` markers.
6. Pull the card while recording: status shows `REC ERR`, the terminal keeps working; insert it and REC works again.
7. Pull the card, open the file on the desktop: `tools/cbp2txt.py /media/$USER/*/logs/0001.cbp | head -30` prints timestamped lines, including your typed commands with `>`.
8. BOOT-window recalibration still works.

State plainly which items passed and which did not.

- [ ] **Step 3: Replay-mode build still builds**

Run: `EXTRA_FLAGS="-DREPLAY_MODE=1 -DREPLAY_LOOP=1" tools/build.sh 2>&1 | tail -2`
Expected: build succeeds (the replay build must not break; it is the project 1 debugging tool).

- [ ] **Step 4: 10-minute soak with recording running**

Flash the soak build and record the serial log while recording is on:

```bash
EXTRA_FLAGS="-DSOAK_HELP=1" tools/build.sh 2>&1 | tail -1 && PORT=/dev/ttyUSB0 tools/flash.sh 2>&1 | tail -1
stty -F /dev/ttyUSB0 115200 raw -hupcl -echo
(timeout 660 cat /dev/ttyUSB0 > "$SCRATCH/soak_rec.log" &)
```

Ask the user to tap `REC` right after the reboot (the status bar must show `REC 00:0x`), leave the CYD alone for 10 minutes, and watch for glitches. Then:

```bash
sleep 640; grep -c heap "$SCRATCH/soak_rec.log"; awk -F'min=' '/heap/ {print $2}' "$SCRATCH/soak_rec.log" | sort -n | head -1; grep -v heap "$SCRATCH/soak_rec.log" | head -3
```

Expected: about 64 heap samples; smallest `min=` at or above 100000; no non-heap lines (no reboot messages). Ask the user to tap `STOP`, pull the card, and run `tools/cbp2txt.py` on the newest file: it should show about 30 `help` commands (`> help`) with the menu text after each, timestamps rising to about 10:00, no garbage lines.

- [ ] **Step 5: Heap with the viewer open**

Flash the normal build (`tools/build.sh && PORT=/dev/ttyUSB0 tools/flash.sh`). Ask the user to open LOGS, open a file, start a replay at 16x, and leave it. Read serial for 30 s: `grep heap` and report the smallest `min=`. Expected: it stays above 60000 and the replay finishes without a reboot. Record this number for the final message.

- [ ] **Step 6: Documentation**

Add to `README.md`, after the Features list:

```markdown
## Recording and replay (project 2)
- `REC` records the session to the microSD card (`/logs/NNNN.cbp`): everything the S3 prints and everything you send,
  with timestamps (time since REC) and `[link lost]` / `[link up]` markers. Tap again (`STOP`) to finish.
- `LOGS` lists the newest 20 recordings. Tap one to view it (last 400 lines; tap a line for its time), `Play` to replay
  it at the original timing (1x / 4x / 16x, pause, stop), `Del` twice to delete it.
- On a PC: `tools/cbp2txt.py /path/to/0001.cbp > session.txt` gives timestamped text.
- Card: FAT32 microSD in the CYD's slot. Pins and the verified bus setup are in `docs/hardware.md`.
```

Also update the "Measured" section's numbers in `README.md` with the Step 4 and Step 5 results.

- [ ] **Step 7: Commit**

Run: `tools/commit.sh "docs: README for recording and replay; soak and heap results"`

