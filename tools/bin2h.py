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
