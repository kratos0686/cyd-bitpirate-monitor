# Bit Pirate CLI protocol notes

## USB capture notes (Task 2)
- Capture file: test/captures/s3_session.bin (13957 bytes: prompt + `help` menu + full command list)
- Bare LF count: 2 (TermBuf treats LF as CR+LF, so these cannot stair-step)
- Escape sequences seen: `ESC[K` only (line redraw: `\rHIZ> ab` + `ESC[K`)
- Non-ASCII byte count: 0 in this capture (emoji appear in the `system` and `man` menus, not in `help`)
- Backspace byte that edits the line: both 0x7F and 0x08 work (redraw ends `HIZ> a`); config keeps 0x7F
- Enter sent as: \r

## Web CLI over Wi-Fi (Task 13)
Measured live from the CYD (a probe sketch joined the S3 hotspot; this desktop has no Wi-Fi adapter) and cross-checked
against the firmware source (geo-tp/ESP32-Bit-Pirate: `GlobalState.h`, `WebSocketServer.cpp`).

- Hotspot: SSID `ESP32-Bit-Pirate`, WPA password (firmware default; see the Bit Pirate source, kept in the git-ignored src/secrets.h), channel 6. Started from the S3's boot menu
  (reset, then hold the board button ~1 s = "WiFi Hotspot"); it was already running during the test.
- S3 address 192.168.4.1; the CYD is handed 192.168.4.2. HTTP GET `/` returns the web terminal page (200, 4863 bytes).
- CLI transport: WebSocket `ws://192.168.4.1:80/ws`, **text frames** in both directions. One client at a time: a new
  connection closes the previous one.
- Echo: the S3 echoes typed characters by redrawing the whole line on each key: `\rHIZ> a\e[K`, then `\rHIZ> ab\e[K`.
- Enter (`\r`) is answered with `\n` then the prompt `HIZ> `. Output lines end in bare `\n` (no `\r\n`).
- Backspace: DEL (0x7F) edits the line: reply `\rHIZ> a \e[K` then `\e[D`. `BACKSPACE_BYTE` stays 0x7F.
- Escape sequences seen: `ESC[K`, `ESC[D` only. Frames are small (1-40 bytes) and arrive in bursts of ~25 within ~100 ms for `help`.
- Gate (Task 13 step 6): WebSocket yes; S3 echoes yes; Enter works; DEL edits the line. No `LOCAL_ECHO` needed.
- Regression test: `web_cli_stream_tests` in `test/test_capture.cpp` replays these frames through the parser.

