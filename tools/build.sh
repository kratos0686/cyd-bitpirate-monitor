#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
FQBN=esp32:esp32:esp32
DRIVER="${DISPLAY_DRIVER:-ILI9341_DRIVER}"
FLAGS="-DUSER_SETUP_LOADED=1 -D${DRIVER}=1 -DTFT_WIDTH=240 -DTFT_HEIGHT=320 \
-DTFT_MISO=12 -DTFT_MOSI=13 -DTFT_SCLK=14 -DTFT_CS=15 -DTFT_DC=2 -DTFT_RST=-1 \
-DTFT_BL=21 -DTFT_BACKLIGHT_ON=HIGH -DUSE_HSPI_PORT=1 -DLOAD_GLCD=1 -DLOAD_FONT2=1 \
-DSPI_FREQUENCY=40000000 ${EXTRA_FLAGS:-}"
arduino-cli compile --fqbn "$FQBN" --output-dir build --build-property "build.extra_flags=${FLAGS}" .
