
#include "z80.h"
#include "trs_screen.h"
#include "trs_memory.h"
#include "trs.h"
#include "io.h"
#include <freertos/task.h>
#include <esp_timer.h>
#include <esp_log.h>
#include "sdkconfig.h"
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>


#define CYCLES_PER_TIMER_M3 ((unsigned int) (CLOCK_MHZ_M3 * 1000000 / TIMER_HZ_M3))
#define CYCLES_PER_TIMER_M4 ((unsigned int) (CLOCK_MHZ_M4 * 1000000 / TIMER_HZ_M4))

// Pacing: the Z80 runs flat out for one "pace slice" of emulated time, then
// sleeps until real time has caught up. The slice used to be the timer
// interrupt period (1/30 s): with the emulator ~3x faster than real
// hardware, that squeezed 33 ms of game activity into ~12 ms followed by
// ~21 ms of nothing — on-screen motion came in clumps (at most 30 screen
// updates/s) and a key press could wait 21 ms to be seen. Pacing is now
// separate from the timer interrupt and as fine as the scheduler allows:
// one slice per FreeRTOS tick. Going finer needs a faster tick.
#define PACE_HZ 100
#define PACE_US (1000000 / PACE_HZ)
static_assert(PACE_HZ <= configTICK_RATE_HZ,
              "a pace slice can't be shorter than one FreeRTOS tick");

#define CYCLES_PER_PACE_M3 ((unsigned int) (CLOCK_MHZ_M3 * 1000000 / PACE_HZ))
#define CYCLES_PER_PACE_M4 ((unsigned int) (CLOCK_MHZ_M4 * 1000000 / PACE_HZ))

static unsigned int cycles_per_timer = CYCLES_PER_TIMER_M3;
static unsigned int cycles_per_pace = CYCLES_PER_PACE_M3;


int trs_model = 3;

static volatile bool z80_paused = false;
static Z80Context z80ctx;

void trs_timer_speed(int fast)
{
  if (trs_model == 3) fast = 0;
  cycles_per_timer = fast ? CYCLES_PER_TIMER_M4 : CYCLES_PER_TIMER_M3;
  cycles_per_pace = fast ? CYCLES_PER_PACE_M4 : CYCLES_PER_PACE_M3;
}

void poke_mem(uint16_t address, uint8_t data)
{
  mem_write(address, data);
}

uint8_t peek_mem(uint16_t address)
{
  return mem_read(address);
}

//------------------------------------------------------------------

static tstate_t total_tstate_count = 0;

static byte z80_mem_read(int param, ushort address)
{
  return peek_mem(address);
}

void z80_mem_write(int param, ushort address, byte data)
{
  poke_mem(address, data);
}

static byte z80_io_read(int param, ushort address)
{
  return z80_in(address & 0xff, total_tstate_count);
}

static void z80_io_write(int param, ushort address, byte data)
{
  z80_out(address & 0xff, data, total_tstate_count);
}

static int get_ticks()
{
  static struct timeval start_tv, now;
  static int init = 0;

  if (!init) {
    gettimeofday(&start_tv, NULL);
    init = 1;
  }

  gettimeofday(&now, NULL);
  return (now.tv_sec - start_tv.tv_sec) * 1000 +
                 (now.tv_usec - start_tv.tv_usec) / 1000;
}

static int64_t get_time_us()
{
  return esp_timer_get_time();
}

#if CONFIG_TRASHBOY_PERF_DIAG
// Called once per pace slice (PACE_US of emulated time) with the time the
// Z80 task just slept in the pacing delay. Logs once per second:
//   speed = emulated time / wall time (100% = real hardware speed)
//   idle  = share of wall time spent sleeping, i.e. the headroom
// speed < 100% with idle ~0% means the emulator can't keep up.
static void perf_z80_slice(int64_t slept_us)
{
  static int64_t win_start_us = 0, last_slice_us = 0, win_slept_us = 0;
  static int slices = 0;

  const int64_t now = get_time_us();
  // First slice, or the Z80 was paused (menu): start a fresh window.
  if (win_start_us == 0 || now - last_slice_us > 200000) {
    win_start_us = now;
    win_slept_us = 0;
    slices = 0;
  } else {
    slices++;
    win_slept_us += slept_us;
  }
  last_slice_us = now;

  const int64_t wall_us = now - win_start_us;
  if (wall_us >= 1000000) {
    const int64_t emu_us = (int64_t) slices * PACE_US;
    ESP_LOGI("perf", "z80: speed=%d%% idle=%d%% (%d of %u slices)",
             (int) (emu_us * 100 / wall_us),
             (int) (win_slept_us * 100 / wall_us),
             slices, (unsigned) (wall_us / PACE_US));
    win_start_us = now;
    win_slept_us = 0;
    slices = 0;
  }
}
#endif

static void sync_time_with_host()
{
  int64_t curtime_us;
  int64_t nexttime_us;
  const int64_t deltatime_us = PACE_US;
  static int64_t lasttime_us = 0;

  curtime_us = get_time_us();
  if (lasttime_us == 0) {
    lasttime_us = curtime_us;
  }

  nexttime_us = lasttime_us + deltatime_us;
  if (nexttime_us > curtime_us) {
    // vTaskDelay() can only sleep to a tick boundary, so the wait is
    // rounded to the NEAREST whole tick: a slice that finished 6.5 ms early
    // sleeps to the next tick, which is where its slot ends once the loop
    // has locked onto the tick. (Rounding down — the old pdMS_TO_TICKS —
    // would make that 0 ticks: no sleep, and slices would bunch up in
    // pairs.) Any error is carried in lasttime_us and corrected by the
    // following slices, so emulated time still tracks real time exactly.
    const int64_t tick_us = (int64_t) portTICK_PERIOD_MS * 1000;
    const int64_t wait_us = nexttime_us - curtime_us;
    const TickType_t wait_ticks = (TickType_t) ((wait_us + tick_us / 2) / tick_us);
    if (wait_ticks > 0) {
      vTaskDelay(wait_ticks);
    }
  }

#if CONFIG_TRASHBOY_PERF_DIAG
  perf_z80_slice(get_time_us() - curtime_us);
#endif
  curtime_us = get_time_us();

  lasttime_us = nexttime_us;
  if ((lasttime_us + deltatime_us) < curtime_us) {
    lasttime_us = curtime_us;
  }
}

void z80_reset(ushort entryAddr)
{
  mem_init();
  memset(&z80ctx, 0, sizeof(Z80Context));
  Z80RESET(&z80ctx);
  z80ctx.PC = entryAddr;
  z80ctx.memRead = z80_mem_read;
  z80ctx.memWrite = z80_mem_write;
  z80ctx.ioRead = z80_io_read;
  z80ctx.ioWrite = z80_io_write;
}

// ------- TRS-80 CMD file loader --------------------------------------------
//
// Block layout (Disk BASIC / LDOS CMD format):
//   0x01 <size> <addr_lo> <addr_hi> <bytes...>   data block (raw size
//                                                 encoding: 0->254, 1->255,
//                                                 2->256, else size-2)
//   0x02 <unused> <entry_lo> <entry_hi>          transfer (entry) address
//   0x00                                         optional EOF marker
//
// Other block types are skipped.
uint16_t trs_load_cmd(const uint8_t *cmd, size_t size)
{
  size_t off = 0;
  uint16_t entry = 0;
  while (off < size) {
    uint8_t type = cmd[off++];
    if (type == 0x00) break;
    if (off >= size) break;

    if (type == 0x01) {
      uint8_t raw = cmd[off++];
      uint16_t block_size;
      switch (raw) {
        case 0: block_size = 254; break;
        case 1: block_size = 255; break;
        case 2: block_size = 256; break;
        default: block_size = (uint16_t)(raw - 2); break;
      }
      if (off + 2 > size) break;
      uint16_t load_addr = cmd[off] | ((uint16_t)cmd[off + 1] << 8);
      off += 2;
      for (uint16_t i = 0; i < block_size && off < size; i++) {
        poke_mem((uint16_t)(load_addr + i), cmd[off++]);
      }
    } else if (type == 0x02) {
      if (off + 3 > size) break;
      off += 1; // size byte (typically 0x02, ignored)
      entry = cmd[off] | ((uint16_t)cmd[off + 1] << 8);
      off += 2;
      break;
    } else {
      // Unknown block: skip its size+payload.
      if (off >= size) break;
      uint8_t skip = cmd[off++];
      off += skip ? skip : 256;
    }
  }
  return entry;
}

void z80_set_pc(uint16_t pc)
{
  z80ctx.PC = pc;
}

void z80_reset()
{
  Z80RESET(&z80ctx);
  mem_init();
  trs_screen.setMode(MODE_TEXT_64x16);
  trs_screen.setInverse(false);
  trs_screen.refresh();
  z80ctx.memRead = z80_mem_read;
  z80ctx.memWrite = z80_mem_write;
  z80ctx.ioRead = z80_io_read;
  z80ctx.ioWrite = z80_io_write;
}

void z80_run()
{
  if (z80_paused) {
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
  }
  static unsigned pace_tstates = 0;

  unsigned last_tstate_count = z80ctx.tstates;
  Z80Execute(&z80ctx);
  const unsigned executed = z80ctx.tstates - last_tstate_count;
  total_tstate_count += executed;

  // The machine's timer interrupt: purely a matter of emulated time.
  while (z80ctx.tstates >= cycles_per_timer) {
    z80ctx.tstates -=  cycles_per_timer;
    z80ctx.int_req = 1;
  }

  // Pacing against real time, in slices much shorter than the timer period.
  pace_tstates += executed;
  while (pace_tstates >= cycles_per_pace) {
    sync_time_with_host();
    pace_tstates -= cycles_per_pace;
  }
}

void z80_pause()
{
  z80_paused = true;
  // Let the Z80 finish the current instruction
  vTaskDelay(pdMS_TO_TICKS(50));
}

void z80_resume()
{
  z80_paused = false;
}