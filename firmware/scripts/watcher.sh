#!/bin/sh
# Build and flash the SenseCAP Watcher firmware (experimental, see
# main/watcher/watcher_main.cpp):
#
#   scripts/watcher.sh build
#   scripts/watcher.sh backup [PORT]     # once, before the first flash
#   scripts/watcher.sh flash [PORT]
#
# The Watcher build has its own build directory and sdkconfig
# (build-watcher/), so it leaves the Waveshare build and its local sdkconfig
# alone. Activate ESP-IDF first (see AGENTS.md).
#
# The firmware image carries the one game this board runs. The game is not
# in the repository: `build` downloads it from RetroStore the first time
# (needs network), to main/watcher/breakdown.cmd.
#
# PORT: the Watcher's bottom USB-C port is a CH342 bridge with two serial
# ports; the ESP32-S3 is the second one (on macOS the /dev/cu.usbmodem* that
# ends in 3; the other is the camera chip). Without PORT, or WATCHER_PORT in
# the environment, that one is used if there is exactly one.
#
# The CH342 drops bytes when esptool sends a whole packet at once, so
# everything that talks to the chip goes through paced_esptool.py at 115200
# baud. Plain `idf.py flash` fails halfway and leaves the board without a
# bootloader (the ROM loader still answers: flash again with this script). A
# flash takes a few minutes; don't interrupt it.
set -eu
cd "$(dirname "$0")/.."

B=build-watcher
BAUD=115200
GAME=main/watcher/breakdown.cmd
GAME_ID=29b20252-680f-11e8-b4a9-1f10b5491ef5   # Breakdown, see GAME_KEYS.md

find_port() {
    if [ -n "${1:-}" ]; then echo "$1"; return; fi
    if [ -n "${WATCHER_PORT:-}" ]; then echo "$WATCHER_PORT"; return; fi
    set -- /dev/cu.usbmodem*3
    if [ $# -ne 1 ] || [ ! -e "$1" ]; then
        echo "can't tell which serial port is the Watcher's; pass it: $0 <command> PORT" >&2
        exit 1
    fi
    echo "$1"
}

esptool() {
    python scripts/paced_esptool.py --chip esp32s3 -p "$PORT" -b $BAUD "$@"
}

case "${1:-}" in
build)
    [ -s $GAME ] || python scripts/fetch_retrostore_cmd.py $GAME_ID $GAME
    python "$IDF_PATH/tools/idf.py" -B $B -DIDF_TARGET=esp32s3 \
        -DSDKCONFIG=$B/sdkconfig \
        -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.watcher" build
    ;;
backup)
    # Everything a flash overwrites below the app, plus Seeed's factory data
    # (0x9000-0x3B000), which a flash does not touch but which can't be had
    # again if anything ever does.
    PORT=$(find_port "${2:-}")
    mkdir -p watcher-backup
    out=watcher-backup/flash-0x0-0x3b000-$(date +%Y%m%d-%H%M%S).bin
    esptool read-flash 0x0 0x3B000 "$out"
    echo "saved $out"
    ;;
flash)
    PORT=$(find_port "${2:-}")
    cd $B
    python ../scripts/paced_esptool.py --chip esp32s3 -p "$PORT" -b $BAUD \
        --before default-reset --after hard-reset write-flash "@flash_args"
    ;;
*)
    sed -n '2,8p' "$0" >&2
    exit 2
    ;;
esac
