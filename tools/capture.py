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
