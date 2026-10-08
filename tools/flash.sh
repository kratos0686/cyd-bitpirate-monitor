#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
arduino-cli upload -p "${PORT:-/dev/ttyUSB0}" --fqbn esp32:esp32:esp32 --input-dir build
