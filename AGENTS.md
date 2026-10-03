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
- The firmware builds at **-O2** (`CONFIG_COMPILER_OPTIMIZATION_PERF`, in
  the defaults file). Defaults only seed a *new* sdkconfig: an existing
  local `sdkconfig` still on `-Og` must be switched by hand (menuconfig ->
  Compiler options). -O2 turns more warnings into errors than -Og, e.g.
  `strncpy` truncation — use `strlcpy`.
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
  Also carries the board's battery-voltage divider: Settings -> Battery
  time-shares the pin (`sound_release_pin()` -> one-shot ADC ->
  `sound_reclaim_pin()`), on request only, never during a game.
- **GPIO 33-37 are consumed by octal PSRAM** despite being on the header —
  never use. GPIO 42 = SD D0 (SD unused). GPIO 43/44 = UART console.
  There is effectively **no free exposed GPIO** — hence buttons are polled
  over I2C, not interrupt-driven.
- Buttons (MCP23017, internal pull-ups, pressed = LOW, polled every 10 ms
  in one 2-byte read): D-pad double-assigned A1/B1=Up A2/B2=Right
  A3/B3=Down A4/B4=Left; A5=CLEAR (HID Home) A6=Space A7=**menu/home**
  (HID PageUp, kills a running game) B5=Enter B6=Esc B7=**on-screen
  keyboard toggle** (HID PageDown, in-game only). A0/B0 are unmapped.
  The pins have names in `main/buttons.h` (`BTN_L_DPAD_UP`,
  `BTN_R_ACTION_UPPER`, ...) — use those, not the numbers. Which keys each
  game reads, and a suggested per-game button mapping, are in
  `GAME_KEYS.md`.

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

**Picture shifted and wrapped** (landscape view: everything moved down a
few rows, the bottom rows — e.g. the status bar — reappear at the top) is
the panel's pixel stream out of step with its frame timing. It happened
whenever the DMA could not fetch from the PSRAM frame buffer in time (flash
reads/writes on the shared bus, e.g. loading a game; heavy load), and it
never healed: without bounce buffers the driver restarts the DMA only on
request, and the ESP32-S3 has no LCD underrun interrupt.

**Fixed 2026-10-02 by bounce buffers** (`bounce_buffer_size_px` in
`ST7701S.c`; it had sat behind `CONFIG_EXAMPLE_USE_BOUNCE_BUFFER`, an
example option this project never defined, so it was silently off). The
DMA now sends from two small internal-RAM buffers that the LCD interrupt
refills from PSRAM; the driver counts the refills and restarts the stream
on the next VSYNC by itself if one was late. Cost: ~3 percentage points of
Z80 idle headroom. Keep it on.

**Do NOT request restarts at screen transitions.** Tried the same day,
before bounce buffers: `esp_lcd_rgb_panel_restart()` on every UI mode
switch and every menu. The first menu opened after that left the panel
permanently black — the restarted stream died, as in rule 2, because the
restart coincided with LVGL redrawing the whole screen. A freshly
restarted stream is as fragile as a freshly booted one.

### Emulator screen: direct to the panel, not through LVGL

- In GAME mode `TRSScreen::render()` draws changed glyphs **straight into
  the RGB panel's frame buffer** (`esp_lcd_rgb_panel_get_frame_buffer`),
  then calls `esp_lcd_panel_draw_bitmap()` with the frame buffer itself —
  the driver then skips its copy and only syncs the touched rows from the
  CPU cache to PSRAM. Cost ~0.25 ms per update. Going through LVGL
  (`lv_obj_invalidate(canvas)`) cost ~127 ms per update *regardless of how
  little changed* and capped games at ~8 screen updates/s.
- Every glyph is also drawn into the LVGL canvas buffer, which stays the
  complete picture. Whenever LVGL does repaint (overlay shown/hidden, canvas
  re-shown after the menu) it reproduces what is on the panel, so the two
  paths can be mixed freely.
- Anything LVGL draws **on top of** the emulator canvas must call
  `trs_screen.setOverlayActive(true)` while visible (the OSK does):
  render() then goes through LVGL, invalidating only the changed area, so
  the overlay is composited instead of being painted over.
- Entering GAME mode does one `trs_screen.render()` + `lv_refr_now()` so the
  panel shows the canvas before direct writes start.
- In GAME mode `display_task` runs **once per panel frame, starting at the
  VSYNC** (`LCD_SetVsyncNotifyTask()` in `ST7701S.c` wakes it), so
  render() writes changed glyphs during the vertical blanking, before the
  scan reaches them. Writing at arbitrary moments tore moving objects at
  the scan line (the panel takes ~45 ms per frame at ~22 Hz). Still tears:
  updates longer than the blanking (a full-screen redraw takes 30-40 ms);
  fixing that needs a second frame buffer. Outside games the loop keeps
  `vTaskDelay(pdMS_TO_TICKS(5))` — **0 ticks at 100 Hz**, a yield — which
  the menu animations were tuned against.

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

## Bluetooth keyboard

- BLE HID only (the ESP32-S3 has no Classic BT). One keyboard at a time,
  managed from **Settings -> Bluetooth Keyboard**: scan lists the keyboards
  in pairing mode, the user picks one to pair; Connect / Disconnect /
  Unpair for the paired one. **Nothing pairs automatically** unless
  `TRASHBOY_BT_AUTO_PAIR` is set (default off; for boards that can't drive
  the menus): then, while nothing is paired, `bt_task` scans and pairs the
  first keyboard it finds in pairing mode.
- `bt_task` only keeps the *paired* keyboard connected (retry loop). No
  pairing -> no radio activity. `g_bt_reconnect` is cleared by "Disconnect"
  so it stays disconnected; `g_bt_menu_open` pauses the loop while the
  Settings screen is open.
- `BTKeyboard::scan_keyboards / connect / connect_paired / disconnect /
  unpair_all` all block and are serialized by one mutex. A BLE connection
  attempt **cannot be cancelled** and blocks up to
  `CONFIG_BT_BLE_ESTAB_LINK_CONN_TOUT` (set to 10 s) — that bounds both
  "Connect" on a switched-off keyboard and how long a user action can wait
  behind a background attempt (`is_busy()` / `wait_idle()`).
- The stack stores a bond as keys + address, not a name: the paired
  keyboard's name lives in our own NVS namespace `bt_kbd`.
- Passkey pairing shows the code on the menu status line
  (`pairing_handler`). BT callbacks run on stack tasks: only log, set flags,
  `splash_set_status()`.
- On a lost connection we post an all-released BT report to the input hub.
  Without it a key held when the link drops (e.g. the ENTER that chose
  "Disconnect") stays down forever and auto-repeats in the menus.

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
- Settings -> Input Test (touch fireworks + MCP23017 button grid + beep) is
  a third UI mode, INPUT_TEST: an overlay on the menu (rot 270) built/torn
  down by `input_test_show()/hide()` on `display_task`. Every button is
  under test, so exit is **holding** A7 or ESC for 1 s. (It used to be a
  boot-time Kconfig mode, `TRASHBOY_INPUT_TEST_MODE` — removed.)
- Settings -> Restart reboots the device (`esp_restart()`), no confirmation.
- Emulator pacing (`ptrs/trs.cpp`): the Z80 runs one 10 ms slice of
  emulated time per FreeRTOS tick, then sleeps (`PACE_HZ`, must not exceed
  the tick rate). This is separate from the machine's 30/60 Hz timer
  interrupt, which stays a pure function of emulated time. Games that time
  themselves off that interrupt still update 30x/s, as on real hardware.

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

## FreHD hard disk in flash (read-only)

- The `trsdisk` partition (2 MB, after `games`) holds FreHD's files:
  FREHD.ROM (FreHD's boot loader opens it by name) + hard-disk images with
  an autoboot entry (NEWDOS3D). `scripts/pack_trs_disk.py` packs them
  (only the 256-byte blocks with data; NEWDOS3D 64 MB -> 760 KB) into
  `firmware/trsdisk.bin` (gitignored: the DOS images aren't ours to
  publish); `idf.py trsdisk-flash` writes it. Plain `idf.py flash` leaves
  it alone.
- `main/flash_disk.cpp` memory-maps it and registers it with TRS-IO as a
  `TRS_FS` backend via `init_trs_fs_local()` (our hook in the trs-io
  submodule, branch `trashboy`): SD card > flash > SMB. While the partition
  holds files, FreHD does not see the SMB share. Writes return
  FR_WRITE_PROTECTED and the packer sets the image's write-protect flag.
- `init_io()` (FreHD state init) was never called before this; z80_task
  now calls it, then `flash_disk_mount()`.

## Second board: Seeed SenseCAP Watcher (experimental)

Round 412x412 SPD2010 LCD on QSPI, a wheel (rotary encoder + push button),
ESP32-S3 with 8 MB octal PSRAM and 32 MB flash. Selected by
`CONFIG_TRASHBOY_BOARD_SENSECAP_WATCHER` (menuconfig -> Trashboy -> Board);
`main/CMakeLists.txt` then compiles `main/watcher/` **instead of** everything
else in `main/`. The components are shared and unchanged.

**Status 2026-10-03: runs on the device** (Sascha: "it works"). The wheel
constants in `watcher_main.cpp` are first guesses that have not been tuned.

- No menus, radios, sound or touch. It boots straight into Breakdown, which
  is embedded in the app image. The game file (`main/watcher/breakdown.cmd`)
  is gitignored: `scripts/watcher.sh build` downloads it from RetroStore when
  it is missing (`scripts/fetch_retrostore_cmd.py`). Wheel left/right = LEFT/RIGHT arrow,
  press = SPACE, hold 2 s = restart the game.
- Build and flash with `scripts/watcher.sh build|backup|flash`. It uses its
  own `build-watcher/` and `build-watcher/sdkconfig`, seeded from
  `sdkconfig.defaults` + `sdkconfig.defaults.watcher`; the Waveshare
  `sdkconfig` is not involved. After editing the defaults, delete
  `build-watcher/sdkconfig` — defaults only seed a new one.
- **`sdkconfig.defaults` alone does not build the project**: the Waveshare
  `sdkconfig` carries values the defaults lack (`COMPILER_DISABLE_DEFAULT_ERRORS`
  for trs-lib, newlib instead of picolibc, task watchdog off, ...). The
  Watcher overlay repeats them.
- **Never flash it with plain esptool / `idf.py flash`.** Its USB bridge
  (CH342, two ports; the S3 is the one ending in `3` on macOS) drops bytes on
  full-size packets: the flash fails after the bootloader has been erased.
  Flash through `scripts/paced_esptool.py` (115200 baud, 64-byte writes); a
  failed flash is recovered by flashing again with it. (All of this is from
  muse-gadget-sdk's notes on the board, where the script comes from.)
- Seeed's per-device factory data sits in flash at 0x9000-0x3B000
  (`nvsfactory`). `partitions_watcher.csv` declares it so nothing lands on
  it, and a flash writes only 0x0-0x9000 and the app at 0x50000. Don't move
  the partition table from 0x8000 or shrink that partition.
- Display (`watcher_board.c`, `watcher_screen.cpp`): no LVGL. The 64x16
  screen is drawn at 6x9 pixels per cell (384x144, the largest 8:3 rectangle
  in the circle) into an internal-RAM frame buffer, and changed text rows are
  sent with `esp_lcd_panel_draw_bitmap`. SPD2010 windows must start and end
  on 4-pixel columns. Set up the panel's SPI bus on the task that draws
  (pinned): the driver's bus lock can strand a sender whose transfer-done
  interrupt runs on the other core.
- Breakdown reads the keys once per ~35 ms game tick and moves the paddle a
  column per tick, so a wheel step is turned into a key held for
  `WHEEL_STEP_HOLD_MS` (see `watcher_main.cpp`). It never uses sound or any
  output port.
- `trs_screen.init()`, `init_settings()`, `init_trs_io()`, `init_sound()` are
  NOT called on this board (LVGL, NVS, SDM on GPIO 4 — a camera pin here).
  A game that needs trs-io, FreHD or sound needs more than a new CMD file.

## Dev toggles (menuconfig -> Trashboy)

- `TRASHBOY_BT_AUTO_PAIR` (pair the first BT keyboard found, no input needed)
- `TRASHBOY_ENABLE_MINI_TRS_MODE` (board without buttons/touch: TRS-IO's
  `init_wifi()` runs Wi-Fi instead of wifi_manager — web server, SMB, NTP,
  printer, its own creds/"TRS-IO" AP; status bar says "TRS-IO: ..." with
  the web address on the right; selects `TRASHBOY_BT_AUTO_PAIR`; boots the
  TRS-80 ROM as soon as the BT keyboard is connected. Internal RAM gets very
  tight in this mode — largest free block measured 3-7 KB)
- `TRASHBOY_WIFI_USE_PRESET` + SSID/password (dev-only; lives in sdkconfig)
- `TRASHBOY_SOUND_DIAG` (audio test tone + telemetry)
- `TRASHBOY_PERF_DIAG` (two `perf` log lines per second during a game:
  `z80: speed=..% idle=..%` = emulated speed vs. real hardware and pacing
  headroom; `disp: updates=.. chars=.. avg=..us` = screen updates and their
  cost). **Measure with this before optimizing** — it is what showed the
  display, not the Z80, was the bottleneck.

## Misc gotchas

- **Games launch on a machine whose ROM never booted**: the launcher resets
  the Z80, loads the CMD over zeroed RAM and jumps to its entry. Reading the
  key matrix directly works; ROM services only work if the program sets up
  the RAM tables itself. We also only have **Model III ROMs**, and some
  programs install Model I ROM addresses: Rear Guard writes the Model I
  keyboard driver (03E3H) into the keyboard DCB, which on the Model III ROM
  returns "no key" forever. `redirect_model1_kbd_driver()` in `ptrs/trs.cpp`
  sends that one case to the Model III driver (3024H). A game that ignores
  keys at one prompt but not another is probably this class of problem —
  check where the Z80 is executing before touching the input path. Booting
  the ROM before loading does NOT help (the game overwrites the DCB) and
  makes every launch seconds slower.
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
