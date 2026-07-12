# TrashBoy — agent instructions

Handheld TRS-80 Model III/IV emulator: ESP32-S3 + 2.8" RGB touch LCD +
physical buttons + speaker. `firmware/` is the ESP-IDF project (where nearly
all work happens); `kicad/` is the custom button/amp carrier PCB; `assets/`
is artwork. Deep architecture detail lives in `firmware/ARCHITECTURE.md` —
this file is the working rules + hard-won gotchas.

## Build & flash workflow

- Every shell needs `source ~/.espressif/tools/activate_idf_v6.0.1.sh`
  before `idf.py` (env does not persist across tool calls; chain it).
- Build with `idf.py build` from `firmware/`.
- **Never open `/dev/ttyACM0`** (no `idf.py flash/monitor`, no serial-log
  daemon): Sascha flashes and captures logs himself and pastes them. Build,
  then ask him to flash and tell him what to look for. (WSL2 + usbipd makes
  port sharing flaky; one holder only.)
- `firmware/sdkconfig` contains **local dev secrets** (preset Wi-Fi
  password) and per-session toggles — do not commit it. Durable config
  choices go in `firmware/sdkconfig.defaults.esp32s3` with a comment.
- Commit style: `firmware: <summary>` subject, body explains the why;
  don't commit `sdkconfig`, `.serial.log`, `scripts/serial-log*`.

## Hardware map (Waveshare ESP32-S3-Touch-LCD-2.8B + custom carrier)

- ESP32-S3R8: 8 MB **octal** PSRAM, 16 MB flash, 512 KB SRAM.
- RGB LCD ST7701S 480x640 (no hardware rotation), 16-bit bus on GPIOs
  3,5,8-14,17,18,21,38-41,45-48; backlight LEDC on GPIO 6; panel init SPI
  on GPIO 1/2; panel reset = TCA9554 EXIO1, panel SPI CS = EXIO3.
- I2C bus: SCL=GPIO7, SDA=GPIO15 — shared by TCA9554 expander (0x20),
  GT911 touch (INT on GPIO16), **MCP23017 button expander (0x21)**.
- **GPIO 4 = SDM audio out** (RC filter -> Adafruit PAM8302 -> speaker).
  Also carries the board's battery-voltage divider (audio and battery ADC
  are mutually exclusive; see tasks).
- **GPIO 33-37 are consumed by octal PSRAM** despite being on the header —
  never use. GPIO 42 = SD D0 (SD unused). GPIO 43/44 = UART console.
  There is effectively **no free exposed GPIO** — hence buttons are polled
  over I2C, not interrupt-driven.
- Buttons (MCP23017, internal pull-ups, pressed = LOW, polled every 10 ms
  in one 2-byte read): D-pad double-assigned A1/B1=Up A2/B2=Right
  A3/B3=Down A4/B4=Left; A5=CLEAR (HID Home) A6=Space A7=**menu/home**
  (HID PageUp, kills a running game) B5=Enter B6=Esc B7="1".

## Display: the four hard rules

1. **LVGL rotation**: `lv_display_set_rotation()` only swaps reported W/H;
   pixels must be sw-rotated in the flush callback (PARTIAL render mode
   only). The flush cb is adaptive: ROTATION_270 = menu UI (sw_rotate),
   ROTATION_0 = TRS-80 emulator (straight blit; TRSCanvas pre-rotates).
   Never try widget transform_rotation.
2. **All flash-heavy init happens BEFORE `LCD_Init()`** (NVS init, FAT
   mounts, first-boot formats — see app_main). Once the RGB panel streams,
   a multi-ms flash erase stalls the shared flash/PSRAM bus; during the
   panel's *first frames* this kills the DMA stream outright — and a dead
   stream has no VSYNC events, so `esp_lcd_rgb_panel_restart()` (which runs
   in the VSYNC handler) can never recover it. Symptom: intermittent
   permanently-black screen at boot with perfectly healthy logs, "once it
   runs, it runs". This was found by bisection 2026-07-12 after the game
   cache added a boot-time FAT mount.
3. **Runtime flash writes must be batched + bracketed**: stage downloads in
   PSRAM, write in one burst behind a user-visible notice, then call
   `lcd_resync_after_flash_writes()` (drift during a burst wraps the
   picture; the explicit restart realigns it — works at runtime because
   VSYNCs are still alive).
4. **Never enable `CONFIG_LCD_RGB_RESTART_IN_VSYNC`** — it restarts the
   panel DMA unconditionally on *every* vsync (see driver source) and
   flickers constantly. Also: `CONFIG_SPI_FLASH_AUTO_SUSPEND` does nothing
   useful on this board's "generic" flash chip (tested; currently off).

## Memory placement

- Any buffer > ~16 KB must live in PSRAM
  (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`), never static BSS: internal SRAM
  starvation breaks BT/Wi-Fi coex (association timeouts). Watch
  `heap_diag_task` output — largest free SRAM block < ~20 KB means coex is
  about to break.
- RetroStore SDK buffers, LVGL draw/rot buffers, TRS canvas, game CMD
  staging, games catalog: all PSRAM already.

## Input architecture

- `main/input.{hpp,cpp}` is the single hub: BT keyboard (BTKeyboard is a
  pure producer via report sink) and MCP23017 buttons both post **full
  HID reports**; the hub emits their **union** (a shared FIFO alone would
  clobber held keys, since consumers diff successive reports).
- Consumers: `input_wait_ascii()` (menus, includes key-repeat/caps state)
  and `input_wait_event()` (raw; emulator via `process_key`, F5,
  Ctrl-Alt-Del).
- **When switching input consumers, call `input_flush()`** (or
  `drain_bt_events()`): it clears the queue AND the translator's repeat
  state. Draining only the queue eats the release report and leaves
  key-repeat armed -> phantom ENTER self-selects item 0 on the next menu.

## Audio

- SDM (sigma-delta) on GPIO 4 -> 2-pole RC (1k/10nF x2, ~16 kHz) -> PAM8302.
  **The RC caps must be nF, not uF** — 10 uF shunts the whole audio band to
  ground: symptom is "barely audible, unresponsive to drive level" (burned
  a day on this; a wrong-reel solder mistake).
- SDM density clamped to +/-25 of 127 to protect the tiny speaker.
- `CONFIG_TRASHBOY_SOUND_DIAG` (menuconfig -> Trashboy) = continuous test
  tone + per-second SDM telemetry, for bring-up.
- Speaker wiring: PAM8302 output is bridge-tied — polarity irrelevant,
  never ground either output terminal.

## Boot flow & UI modes

- Boot lands on the **main menu immediately** (never blocks on radios):
  BT (`bt_task`) and Wi-Fi (`wifi_bg_task`, preset/NVS creds) come up in
  background; a white bottom status bar shows live Wi-Fi state.
- `display_task` owns ALL LVGL work incl. the mode transitions
  MENU (rot 270, splash widgets) <-> GAME (rot 0, TRS canvas via
  `trs_screen.setVisible()`); other tasks request a mode via
  `ui_set_mode()` and block. Menu and emulator never coexist — A7 pauses
  the Z80, flushes sound, hides the canvas.
- `flow_task` is the single input consumer / UI state machine; `z80_task`
  idles paused until a game session resumes it (per-launch mem_init +
  z80_reset, so games are relaunchable).
- trs-lib settings UI (Settings -> TRS-80 Config, or F5 in-game) runs on
  the TRS screen with the Z80 kept paused.

## Games cache (offline play)

- 8 MB wear-levelled FAT partition `games` in internal flash (`/games`):
  `catalog.bin` (header + fixed records) + `gNNNN.cmd` (8.3 names). No SD
  card needed/used; CMDs are <= 64 KB (Z80 address space).
- Settings -> Sync Games: pages the FULL RetroStore catalog, fetches
  descriptions + COMMAND images **into PSRAM staging** (network phase does
  zero flash writes -> stable screen), then commits in one bracketed burst.
- Games menu is cache-only ("Games (N)" count, scrolling 10-row window),
  works fully offline; launch reads the CMD from flash into the PSRAM
  launch buffer.

## Dev toggles (menuconfig -> Trashboy)

- `TRASHBOY_BT_SCAN_ENABLED` (off while iterating without BT keyboard)
- `TRASHBOY_WIFI_USE_PRESET` + SSID/password (dev-only; lives in sdkconfig)
- `TRASHBOY_INPUT_TEST_MODE` (boot straight into touch+button+beep test
  screen — also the minimal reproducer for display bring-up issues)
- `TRASHBOY_SOUND_DIAG` (audio test tone + telemetry)

## Misc gotchas

- trs-io's `configure()` form has its OWN Wi-Fi credential store
  (`set_wifi_credentials` reboots the chip!) — unrelated to our
  wifi_manager NVS creds.
- SD-card init (`init_trs_fs_posix`) is intentionally not called (no card,
  noisy failures, GPIO overlap with panel SPI); FreHD file ops are
  null-safe without it.
- The GPIO-4-for-audio decision and its comment in `ptrs/sound.h` are
  Sascha's (verify authorship with git blame before attributing decisions).
- kicad/: MCP23017 carrier with buttons, R network pull-ups (4.7k SIP),
  PAM8302 header (GND,VIN,SD,A-,A+ = pads 1-5, square pad = GND), RC
  reconstruction filter, speaker terminal.
