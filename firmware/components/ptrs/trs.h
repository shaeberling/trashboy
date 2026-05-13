
#ifndef __TRS_H__
#define __TRS_H__

#include <stdint.h>
#include <stddef.h>

typedef unsigned long long tstate_t;

// Model III/4 specs
#define TIMER_HZ_M3 30
#define TIMER_HZ_M4 60
#define CLOCK_MHZ_M3 2.02752
#define CLOCK_MHZ_M4 4.05504

extern int trs_model;

void trs_timer_speed(int fast);
void poke_mem(uint16_t address, uint8_t data);
uint8_t peek_mem(uint16_t address);
void z80_reset(uint16_t entryAddr);
void z80_reset();
void z80_run();
void z80_pause();
void z80_resume();

// Parse a TRS-80 CMD file and write its data blocks into Z80 memory via
// poke_mem(). Returns the entry address declared in the file's transfer
// block, or 0 if none was found. Safe to call after z80_reset() — overlay
// the loaded program on top of the freshly-reset memory.
uint16_t trs_load_cmd(const uint8_t *data, size_t size);

// Set the Z80 PC. Used after trs_load_cmd() to jump into the loaded program.
void z80_set_pc(uint16_t pc);

#endif
