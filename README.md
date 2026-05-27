<p align="center">
  <img src="assets/logo.png" alt="TRASHBOY" width="520" />
</p>

<p align="center">
  <em>A pocketable TRS-80 Model III/IV. Yes, really.</em>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/MCU-ESP32--S3-E7352C?style=for-the-badge&logo=espressif&logoColor=white" alt="ESP32-S3" />
  <img src="https://img.shields.io/badge/ESP--IDF-v6.0.1-1A5DAE?style=for-the-badge" alt="ESP-IDF v6.0.1" />
  <img src="https://img.shields.io/badge/LVGL-v9.1-AF7AC5?style=for-the-badge" alt="LVGL v9.1" />
  <img src="https://img.shields.io/badge/Z80-libz80-3E8E41?style=for-the-badge" alt="Z80 / libz80" />
  <img src="https://img.shields.io/badge/TRS--80-Model%20III%20%2F%20IV-FFCB05?style=for-the-badge" alt="TRS-80" />
</p>


## What is this?

A handheld **TRS-80 Model III / IV emulator** running on an ESP32-S3 board with a 640×480 RGB LCD.

It runs at original-hardware clock speed — 2.028 MHz for Model III, 4.055 MHz for Model IV — synchronised to wall-clock time so games play at their original speed. The 8 × 12 (M3) and 8 × 10 (M4) glyphs are blitted into an LVGL canvas with 90° rotation, so the portrait panel reads as a landscape Model III screen.

## Hardware

| Part | Role |
|------|------|
| **ESP32-S3** with 8 MB octal PSRAM | Runs the emulator, LVGL UI, BT, Wi-Fi, and a 16-bit RGB LCD bus simultaneously. |
| **ST7701S 480×640 RGB LCD** | Drawn as 640×480 landscape via software rotation in the LVGL flush callback. |
| **GT911 capacitive touch** | I²C; used for the boot-time touch-test screen. |
| **TCA9554PWR I/O expander @ 0x20** | On-board: LCD CS, backlight enable, buzzer reset, etc. |
| **MCP23017 I/O expander @ 0x21** | Physical board buttons. Interrupt-driven via INTA → GPIO4. |
| **Bluetooth HID keyboard** | Any generic BT keyboard. Pairing persists in NVS. |
| **Buzzer** | LEDC PWM on GPIO 6 (shared with the LCD backlight channel). |

## Software stack

- **ESP-IDF** — Official ESP32 software stack
- **LVGL v9.1** in `LV_DISPLAY_RENDER_MODE_PARTIAL` with software rotation done inside the flush callback (the panel has no hardware rotation, and `lv_display_set_rotation` alone only swaps reported dimensions). An adaptive flush callback skips the rotation entirely when the emulator is running, since its TRSCanvas pre-rotates glyphs into an unrotated buffer.
- **libz80** Z80 emulation core in `components/ptrs/`, derived from `apuder/pTRS-80`.
- **RetroStore C SDK** (`components/retrostore-c-sdk/`, git submodule) for fetching downloadable game media. HTTP and decode buffers live in PSRAM to keep internal SRAM available for BT/Wi-Fi coexistence working memory.
- **esp-hid** wrapping BT Classic + BLE for the keyboard host.
- **TRS-IO / TRS-LIB** (`components/trs-io/`, `components/trs-lib/`) for on-screen menus, the F5 settings UI, and TRS-80 system I/O glue.

Want the full picture? See [`firmware/ARCHITECTURE.md`](firmware/ARCHITECTURE.md) — boot sequence, task layout, the rotation handoff between splash and emulator, memory placement rules, and lessons-learned annotations.

## Build & flash

```bash
# Once per shell session:
source ~/.espressif/tools/activate_idf_v6.0.1.sh

cd firmware
idf.py set-target esp32s3        # only the first time
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

On WSL2, the USB device routing is its own little adventure — see [`firmware/wsl.md`](firmware/wsl.md).

## Repo layout

```
.
├── assets/         Artwork (logo PNGs)
└── firmware/       ESP-IDF project — the action lives here
    ├── main/         app_main, LVGL UI, splash, Wi-Fi setup,
    │                 RetroStore browser, MCP23017 buttons, touch test
    ├── components/   Z80 core, TRS-80 emulator, BT keyboard,
    │                 RetroStore SDK, TRS-IO, TRS-LIB
    ├── scripts/      Serial logger, asset rotation helpers
    ├── ARCHITECTURE.md
    └── wsl.md
```

## Status

Active development on the `lvgl9` branch. Recent direction: ESP-IDF v6 migration, MCP23017 board buttons over IRQs, GT911 touch driver, RetroStore game launching with PSRAM-backed buffers, an adaptive flush callback for the LVGL pipeline.

---

<sub>Not affiliated with Tandy or Radio Shack. The TRS-80 trademark belongs to whoever owns it now.</sub>
