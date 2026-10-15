# CYD Bit Pirate Terminal: SD Logging, Viewer and Replay (Project 2)

Date: 2026-10-14
Status: Draft, awaiting review
Builds on: `2026-10-08-cyd-bitpirate-terminal-design.md` (project 1, shipped)

## 1. Purpose and scope

Record sessions with the Bit Pirate to the CYD's microSD card, browse the saved sessions on
the CYD, and view or replay them on screen.

In scope:
- Manual REC button; one binary session file per recording
- Timestamps (milliseconds since REC started), both directions recorded
- LOGS browser (list of the newest 20 files, open, delete), read-only viewer; tap a terminal line to see its timestamp
- Timed replay (1x, 4x, 16x, pause, stop) into the viewer
- Host tool to convert a session file to text on a PC

Out of scope: on-device text export, wall-clock time, file manager beyond delete, upload
over Wi-Fi, auto-record, hex view, decoders (later projects).

User decisions (2026-10-14): logs are browsed and replayed on the CYD; replay is both a
scrollable view and a timed replay; recording is manual.

## 2. Hardware gate

The CYD SD slot is assumed to use its own SPI bus: CS 5, SCK 18, MISO 19, MOSI 23. This is
unverified on the user's board. Task 1 of the plan is a gate: mount, write, read back, and
sustained write while Wi-Fi is joined to the S3 hotspot. If the pins differ, they are
recorded in `docs/hardware.md` and the plan continues with the verified pins. If the slot
does not work at all, the project stops there and asks the user.

## 3. File format (`/logs/NNNN.cbp`)

- Header: 4 bytes `CBP1`.
- Records, repeated: `u32 ms` (little endian, since REC), `u8 dir`, `u8 len` (1..255),
  `len` bytes.
- `dir`: 0 = received from S3, 1 = sent by the CYD, 2 = marker (text such as `[link lost]`,
  `[link up]`, `[rec overflow]`, `[rec start]`).
- A larger payload is split across records. A truncated final record (card pulled, power
  loss) is ignored on read; every earlier record is still valid.
- Duration of a session = `ms` of the last valid record.
- File name: next free four-digit number, `0001.cbp` upward.

The format code (writer, reader) has no Arduino dependency and is tested on the host.

## 4. Architecture

| Unit | Job | Depends on |
|---|---|---|
| `cbpformat` | Encode and decode records, header check, truncated-tail handling. | none |
| `recorder` | REC state; takes `(dir, bytes)` from a queue and appends records through a file interface; counts drops; writes markers. | `cbpformat`, `bytering` |
| `sdfile` | Thin SD-card file interface (open new, append, flush, list, open read, delete). Host tests use an in-memory fake. | SD library |
| `logindex` | Builds the LOGS list: name, size, duration, newest first. | `cbpformat`, `sdfile` |
| `replayer` | Reads records and feeds them to a terminal at the original timing, speed 1x/4x/16x, silences capped at 2 s, pause/stop. Display only; never sends to the S3. | `cbpformat` |
| `viewer` | Read-only terminal screen with its own `TermBuf`; timestamp margin toggle; hosts the replayer. | `termbuf`, `render`, `replayer` |
| `ui` additions | REC and LOGS buttons, status-bar REC indicator, list and viewer screens, delete confirmation. | existing `ui` |

Data flow: the existing link rx loop and the UI's send path both also push `(dir, bytes)`
into a recorder queue. The recorder task drains the queue and does all SD writes on its own
task so a slow write cannot stall touch or rendering. A full queue drops bytes and writes a
`[rec overflow]` marker. Nothing in the live path blocks on the SD card.

## 5. Behavior

Recording:
- REC opens the next file; REC again closes it. Status bar shows `REC` and elapsed time.
- Both directions recorded; link loss and reconnect are written as markers; recording
  continues across a link drop.
- No card, or a write error: REC shows an error, recording stops, the terminal keeps working.
- The file is flushed about once per second and on stop, so a power loss costs at most about
  one second of data.

LOGS browser and viewer:
- LOGS opens a full-screen list of the newest 20 files: name, size, duration, newest first, drag to scroll.
- Tapping a file opens the viewer: a read-only terminal with a header bar (file name, state, time)
  and a control row (play/pause, speed, stop, delete, back). Tapping a terminal line shows that
  line's timestamp in the header (a 53-column line leaves no room for a timestamp margin).
- The main button row becomes 12 buttons in a 6x2 grid; REC and LOGS are the two new ones at
  the end. Button width is 53 px (the last column is 55 px).
- The viewer has its own 400-line buffer, so live data and recording continue in the
  background and the live screen is unchanged when leaving the viewer.
- The viewer shows the last 400 lines of a session. Older lines of a longer session are not
  pageable in this project (deferred); replay streams the whole file from the start.
- Delete needs a confirmation tap. Deleting the file being recorded is refused.

Replay:
- Play feeds records into the viewer buffer at the original timing; speeds 1x, 4x, 16x;
  gaps longer than 2 s are shortened to 2 s; pause and stop.
- Bytes sent by the user are drawn in a different colour from bytes received.
- Replay never transmits.

## 6. Error handling

| Situation | Behavior |
|---|---|
| No SD card at REC | Error shown; terminal unaffected. |
| Card removed mid-recording | Recording stops with an error; file keeps all flushed records. |
| Corrupt or truncated file | Valid records are shown; an invalid header shows "not a session file". |
| Recorder queue full | Bytes dropped; `[rec overflow]` marker in the file. |
| Card full | Recording stops with an error. |
| Viewer RAM unavailable | LOGS refuses to open and says so; the live terminal is unaffected. |

## 7. Testing

- Host unit tests (g++, in-memory fake file): record round trip, splitting of long payloads,
  truncated final record, bad header, session duration, log index ordering, replayer timing
  at each speed, gap capping, pause/stop, replay never calls send.
- Hardware gate (section 2) before any feature work.
- On the CYD: record a `help` dump, view it, replay at 1x and 16x, pull the card and open
  the file on a PC with the host converter, remove the card mid-recording, delete a file.
- Done = checklist passes and a 10-minute soak with recording running shows no crash or
  corruption, a valid file on the card, and measured free heap.

## 8. Build and layout

New files under `src/`: `cbpformat.* recorder.* sdfile.* logindex.* replayer.* viewer.*`;
host tool `tools/cbp2txt.py`; tests under `test/`. New library: the arduino-esp32 SD
library (bundled with the core). Updates to `README.md` and `docs/hardware.md`.

## 9. Risks and open items

1. SD pins on this board are unverified (section 2 gate).
2. RAM: the second 400-line buffer costs about 42 KB; project 1 leaves at least 153 KB free.
   The viewer scrollback is a constant to shrink if the measured margin is too thin.
3. SD and Wi-Fi share the CPU; the gate includes a sustained-write test with Wi-Fi joined.
4. Flash is 85% full; the new code must fit in about 190 KB. Measured at each task.
5. The `.cbp` files are binary and need `tools/cbp2txt.py` to read on a PC.
