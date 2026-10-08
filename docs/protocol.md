# Bit Pirate CLI protocol notes

## USB capture notes (Task 2)
- Capture file: test/captures/s3_session.bin (13957 bytes: prompt + `help` menu + full command list)
- Bare LF count: 2 (TermBuf treats LF as CR+LF, so these cannot stair-step)
- Escape sequences seen: `ESC[K` only (line redraw: `\rHIZ> ab` + `ESC[K`)
- Non-ASCII byte count: 0 in this capture (emoji appear in the `system` and `man` menus, not in `help`)
- Backspace byte that edits the line: both 0x7F and 0x08 work (redraw ends `HIZ> a`); config keeps 0x7F
- Enter sent as: \r

## Web CLI over Wi-Fi (Task 13)
(filled in by Task 13)
