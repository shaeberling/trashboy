# Trashboy firmware — architecture notes

ESP-IDF v5.5 firmware for an **ESP32-S3** that emulates a **TRS-80 Model III/IV** and accepts input from a **Bluetooth HID keyboard**. Renders to a 640×480 RGB LCD via LVGL v9.

## Tree

```
firmware/
├── CMakeLists.txt                 # IDF project shell
├── partitions.csv
├── sdkconfig / sdkconfig.defaults.esp32s3
├── main/
│   ├── main.cpp                   # app_main, tasks, BT pairing UI glue
│   ├── I2C_Driver/                # I2C master init (GPIO 7 SCL / 15 SDA)
│   ├── EXIO/                      # TCA9554PWR I/O expander @ 0x20
│   ├── LCD_Driver/                # ST7701S init via SPI + RGB panel
│   ├── LVGL_Driver/               # LVGL v9 display driver, 2 ms tick
│   └── Buzzer/
├── components/
│   ├── ptrs/                      # TRS-80 emulator core (Z80 + memory + screen + I/O)
│   ├── bt_keyboard/               # BTKeyboard wrapper over esp-hid (BT Classic + BLE)
│   ├── trs-io/                    # TRS-IO subsystem (network, FS, etc.)
│   └── trs-lib/                   # On-screen UI primitives (windows, menus, header)
└── managed_components/
    ├── lvgl__lvgl/ (9.1.0)
    └── espressif__mdns/
```

## Boot sequence — `main/main.cpp`

`app_main()` (line 178):
1. `I2C_Init()` — I2C0 @ 400 kHz, SCL=7, SDA=15
2. `EXIO_Init()` — TCA9554PWR (LCD CS, buzzer, backlight enable, etc.)
3. `LCD_Init()` — ST7701S over SPI (MOSI=1, SCLK=2, CS via EXIO), then RGB panel
4. `LVGL_Init()` — Two full-frame RGB565 buffers in PSRAM, 2 ms tick timer
5. `xTaskCreatePinnedToCore(keyb_task, …, core 0)`
6. `z80_task(NULL)` — runs on the calling task (core 1 by default for app_main? actually app_main runs on PRO_CPU; the loop is just on whatever core it inherits)

### Tasks

| Task        | Stack | Prio | Core | Role |
|-------------|-------|------|------|------|
| `keyb_task` | 6000  | 5    | 0    | BT init, pairing, blocking key event loop |
| `z80_task`  | —     | 1    | —    | Initializes everything else, then `while(1) z80_run()` |
| `ui_task`   | 6000  | 5    | 1    | `trs_screen.render()` + `lv_timer_handler()` every 5 ms |

`z80_task` initializes `settings`, `storage`, `events`, `trs_io`, `trs_fs_posix`, `wifi`, `trs_lib`, `trs_screen`, pushes a `MODE_TEXT_64x16` ScreenBuffer, calls `mem_init()` + `z80_reset()`, then spawns `ui_task` on core 1.

## TRS-80 emulator — `components/ptrs/`

- **CPU**: libz80 (`z80.h/.cpp`). Single-instruction stepping inside `z80_run()` (`trs.cpp:206`).
- **Timing**: `cycles_per_timer = CLOCK_MHZ * 1e6 / TIMER_HZ` (Model 3: ~2.028 MHz, 30 Hz; Model 4: ~4.055 MHz, 60 Hz). When the T-state budget is consumed, `sync_time_with_host()` sleeps to maintain real-time, then raises `int_req` for the maskable timer interrupt.
- **Memory** (`trs_memory.cpp`): two 64K banks in PSRAM (`EXT_RAM_ATTR`), separate 2 K video buffer, embedded ROMs (`rom/model3-frehd.cpp-inc`, xrom).
- **Screen** (`trs_screen.h/.cpp`): stack of `ScreenBuffer`s so UI overlays (e.g. pairing screen) can be pushed/popped over the running emulator. Modes: `MODE_TEXT_64x16` (M3), `MODE_TEXT_80x24` (M4), GRAFYX. Glyph fonts are 8×12 (M3) / 8×10 (M4) in `font/font_m3` / `font_m4`.
- **Canvas → LCD**: `TRSCanvas::blit_glyph_to_canvas()` expands TRS-80 glyphs into the LVGL RGB565 buffer with **90° CCW rotation** (so a portrait-oriented panel looks landscape), with per-cell dirty tracking driven from `trs_screen.render()`.
- **I/O ports** (`io.cpp`): 0x84–0x87 memory map / video mode, 0xEC–0xEF cassette / screen mode, 0xF8+ floppy / serial / TRS-IO / network.
- **Pause/resume**: `z80_pause()` / `z80_resume()` (`trs.cpp:222`) — used by the pairing handler and the F5 settings UI.
- **Bundled CMD**: `COSMIC.CMD` is embedded as a binary blob (commit 925f37f integration), reachable from a CMD loader.

## Bluetooth keyboard — `components/bt_keyboard/`

- Wraps **esp-hid** with both BT Classic and BLE host enabled (`CONFIG_BT_BLUEDROID_ENABLED`, `CONFIG_BT_BLE_ENABLED`).
- Pairing data persists in NVS. Erase NVS to force re-pairing (commented-out line in `keyb_task`).
- `setup(pairing_handler, connected_handler, lost_handler)` then loop: `auto_connect_bonded_device()` → if not connected after a couple of waits, `devices_scan()` (5 s) and try again.
- Pairing UI (`pairing_handler` in `main.cpp:37`): pause Z80 → push a fresh ScreenBuffer overlay → render pairing prompt with `trs-lib` (`init_window`, `header`, `wnd_print`) → user types code on the keyboard → on connect, pop overlay buffers and resume Z80.
- Event loop pulls `KeyInfo` from a FreeRTOS queue with `wait_for_low_event()`. Special handling:
  - **F5** → pause Z80, `configure_pocket_trs()` (settings UI), resume.
  - **Ctrl+Alt+Del** → set `do_z80_reset` flag, picked up by `z80_task` to call `z80_reset()`.
  - Else → `process_key(inf)` in `components/ptrs/trs-keyboard.cpp` to feed the emulated keyboard matrix.

## Display — `main/LCD_Driver/`, `main/LVGL_Driver/`

- ST7701S, 640×480 RGB565, 8 MHz pixel clock.
- 16-bit RGB pin map (B0–B4, G0–G5, R0–R4): `5,45,48,47,21,14,13,12,11,10,9,46,3,8,18,17`.
- Sync: HSYNC=38, VSYNC=39, DE=40, PCLK=41. Backlight=GPIO 6 (LEDC PWM, 13-bit, 4 kHz).
- Two LVGL frame buffers in PSRAM (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`) → `LV_DISPLAY_RENDER_MODE_FULL` for tear-free output.
- Flush callback hands buffers to `esp_lcd_panel_draw_bitmap()`.

## Orientation & 90° rotation

This is the single most confusing part of the firmware. **Read this before adding any UI.**

### Physical layout

- The ST7701S panel is **480 × 640 native (portrait)**. That's what `EXAMPLE_LCD_H_RES`/`V_RES` and the LVGL display report.
- The device is meant to be **held in landscape with the panel's native top edge on the user's right**. So the user sees a **640 × 480** view.
- Because the device is rotated, native content appears to the user rotated **90° CCW**. To compensate, anything we want to look upright must be rotated **90° CW *as drawn into the framebuffer*** (so the user's eyes "un-rotate" it back to upright).

### User ↔ native coordinate mapping

Pick the user-view coordinates that match what you see, then convert to native to position widgets / blit pixels:

```
native_x = user_y
native_y = (NATIVE_H - 1) - user_x       // NATIVE_H = 640
```

Inverse:

```
user_x = (NATIVE_H - 1) - native_y
user_y = native_x
```

This mapping is the one TRSCanvas uses (see `components/ptrs/trs_screen.h`, `blit_glyph_to_canvas`).

### How the splash UI rotates (LVGL display rotation + sw_rotate)

The splash UI uses LVGL's display rotation feature with software pixel rotation in the flush callback. The combination is exactly what Espressif's `esp_lvgl_port` does when its `.sw_rotate = true` flag is set, and is the pattern documented in [LVGL v9's rotation chapter](https://docs.lvgl.io/9.4/details/main-modules/display/rotation.html).

**Two pieces are required together — neither alone works:**

1. **`lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270)`** — this only swaps the reported width/height so widgets get laid out in landscape (640 × 480) coordinates. It does **not** rotate pixels by itself; if you do this without the second piece you get noise on the panel because LVGL hands the framebuffer pixels in the wrong orientation.
2. **Pixel rotation in the flush callback** — for every dirty rectangle LVGL flushes, we:
    1. translate `area` from user-view (landscape) coords to panel-native (portrait) coords using the user↔native mapping above;
    2. call `lv_draw_sw_rotate(px_map, rot_buf, …, LV_DISPLAY_ROTATION_270, cf)` to rotate the pixel buffer into a pre-allocated rotation-destination buffer (`rot_buf`, full-screen size in PSRAM);
    3. write the rotated buffer to the panel at the translated coords.

Implementation: `main/LVGL_Driver/LVGL_Driver.c:example_lvgl_flush_cb`. Constraints documented in the search results: `sw_rotate` is **incompatible with `LV_DISPLAY_RENDER_MODE_DIRECT` or `FULL`** — must be `PARTIAL`, which is what we use.

### How the TRS-80 emulator rotates

The TRS-80 emulator was built before the LVGL rotation work, and uses an older approach: `TRSCanvas` writes pixels **directly into an unrotated full-screen `lv_canvas` buffer**, doing the rotation by hand per glyph (`(sx, sy) → (sy, fw-1-sx)`). It does not use `lv_display_set_rotation`. This still works because LVGL just blits the canvas widget's buffer to the panel — but it means the emulator and the splash live in two different coordinate worlds.

> ⚠️ **The TRS-80 path still assumes unrotated display.** `trs_screen.init()` calls `lv_display_get_horizontal_resolution()` which now returns the rotated value (640, not 480) thanks to the splash rotation. Before re-enabling the TRS-80 path post-splash, we'd need to either disable LVGL rotation before `trs_screen.init()` or convert TRSCanvas to use landscape coordinates directly. See `trs_screen.cpp:269` (`TRSScreen::init`).

Key file: `components/ptrs/trs_screen.h:119` (`blit_glyph_to_canvas`).

### Strategies for rendering rotated UI

1. **The splash approach (`lv_display_set_rotation` + sw_rotate flush).** Best default for new LVGL UI. Widgets use plain landscape coords, no transforms.
2. **Pre-rotated raster assets** — if you have a static image, rotate offline and store the rotated bytes; place at native coords. `main/logo90c.c` is the original 596 × 182 logo stored as 182 × 596 native, and was used by an earlier splash iteration. Currently unused; `main/logo.c` (the original landscape orientation) is what the splash uses now since LVGL rotation handles the rest.
3. **Manual pixel blit into a canvas (the TRS-80 approach).** Reliable but verbose. Use when you need to share a buffer between subsystems or want to do the rotation yourself.

### What we tried and discarded

- **`lv_obj_set_style_transform_rotation` per widget.** Works visually for a couple of short labels, but in LVGL v9.1 the dirty-area tracking for transformed widgets does not cover the actual rotated render region for long-text labels. Symptoms: stale-pixel bands across the middle of the user view ("black square"), and eventually the render task wedges. Hours of debugging confirmed this — see commit history. **Don't use widget transform_rotation for our rotated UI.**
- **`LV_OBJ_FLAG_OVERFLOW_VISIBLE` on `lv_scr_act()`.** Setting the flag on the *screen object* causes the entire splash to render as black. Setting it on a regular `lv_obj` child is fine.
- **`lv_refr_now()` inside `splash_tick`.** Hangs the LVGL render task indefinitely when many transformed widgets are present. The async render driven by `lv_timer_handler` is fine.
- **Fonts.** Only `lv_font_montserrat_14` is enabled by default. For crisper bigger text enable a specific size in `sdkconfig.defaults.esp32s3` (e.g. `CONFIG_LV_FONT_MONTSERRAT_28=y`) rather than scaling via transforms.

### Splash screen architecture (cross-reference)

- LVGL is set up with `LV_DISPLAY_ROTATION_270` so widgets render in landscape (640 × 480). Pixel rotation happens in `example_lvgl_flush_cb` via `lv_draw_sw_rotate`.
- `splash_root` is a full-screen 640 × 480 black `lv_obj`. The logo is `main/logo.c` (596 × 182 landscape) placed at `(22, 30)` in landscape coords for the BT-pairing screen, and scaled 50% to `(171, 5)` when we transition to the compact (Wi-Fi / RetroStore) layout. No widget transforms.
- Status / list rows / subtext are plain `lv_label`s at `x=20`, `lv_obj_set_width(.., 600)` for wrapping.
- The splash is created in `app_main` before `z80_task`'s `trs_screen.init()`; an `lvgl_pump_task` drives `lv_timer_handler` while it's up, then exits when the user dismisses. After that, `ui_task` takes over and the TRS-80 owns the screen.

Files: `main/splash.{c,h}`, `main/logo.c`, `main/LVGL_Driver/LVGL_Driver.c`, `main/main.cpp` (`app_main`, `lvgl_pump_task`).

## SDK config highlights — `sdkconfig`

- `CONFIG_SPIRAM=y`, octal PSRAM @ 80 MHz, fetch-instructions/rodata/malloc all in PSRAM.
- `CONFIG_BT_ENABLED=y`, Bluedroid + BLE; HID host task stacks 2048 (BT) / 4096 (BLE).
- `CONFIG_SOC_LCD_RGB_SUPPORTED=y` (16-bit data width).

## Misc

- `wsl.md` — USB-IP recipe to expose `/dev/ttyACM0` from Windows for flashing under WSL2.
- `crash1.log` — interrupt watchdog timeout shortly after `LVGL_Init`; documented but not yet rooted.
- ESP-IDF version in use: **v5.5.2-dirty**.

## Quick navigation

- App entry / tasks: `main/main.cpp:178` (`app_main`), `:152` (`z80_task`), `:82` (`keyb_task`), `:142` (`ui_task`).
- Z80 step + timer: `components/ptrs/trs.cpp:206`.
- Screen render: `components/ptrs/trs_screen.cpp` (`render`, `blit_glyph_to_canvas`).
- BT setup / pairing: `components/bt_keyboard/src/bt_keyboard.hpp`.
- LCD pin map: `main/LCD_Driver/ST7701S.h`.
- LVGL init: `main/LVGL_Driver/LVGL_Driver.c`.
