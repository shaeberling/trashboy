// Offline game cache on the internal-flash "games" FAT partition.
//
// Layout on /games:
//   catalog.bin   header + fixed-size records (one per game, sync order)
//   gNNNN.cmd     the COMMAND media image for catalog index NNNN (8.3 names,
//                 so this works without FATFS long-filename support)
//
// "Sync Games" (Settings) rebuilds the whole cache from RetroStore; the
// Games menu then works entirely offline from this cache.

#pragma once

#include <stddef.h>
#include <stdint.h>

#define GAMES_CACHE_ID_LEN      64
#define GAMES_CACHE_NAME_LEN    64
#define GAMES_CACHE_AUTHOR_LEN  64
#define GAMES_CACHE_VERSION_LEN 16
#define GAMES_CACHE_DESC_LEN    512

typedef struct {
  char     id[GAMES_CACHE_ID_LEN];          // RetroStore app id
  char     name[GAMES_CACHE_NAME_LEN];
  char     author[GAMES_CACHE_AUTHOR_LEN];
  char     version[GAMES_CACHE_VERSION_LEN];
  uint16_t release_year;
  uint8_t  model;                           // RsTrs80Model value
  uint8_t  has_cmd;                         // 1 = gNNNN.cmd exists
  char     description[GAMES_CACHE_DESC_LEN];
} cached_game_t;

// Mount the FAT partition (formats it on first use) and load catalog.bin
// into PSRAM. Call once at boot; idempotent. Returns false if the
// partition is missing/unmountable.
bool games_cache_mount();

// Number of games in the loaded catalog (0 if never synced).
int games_cache_count();

// Record for catalog index i, or nullptr if out of range.
const cached_game_t *games_cache_get(int index);

// Full re-sync from RetroStore. Fetches the complete catalog (paged), each
// app's description, and its COMMAND media image; writes everything to
// /games and reloads the in-memory catalog. Requires Wi-Fi. Returns the
// new game count, or -1 on failure.
//
// Downloads are STAGED in PSRAM and committed to flash in as few bursts as
// possible: flash erases stall the shared flash/PSRAM bus and make the RGB
// panel's picture drift, so the long network phase must be write-quiet.
//
// `progress` (may be null): per game, done-so-far / total / current name.
// `flash_phase` (may be null): called with `true` right before a flash
// write burst begins and `false` right after it ends — the UI uses this to
// show a "writing" notice and to resync the LCD panel afterwards.
typedef void (*games_sync_progress_cb)(int done, int total, const char *name);
typedef void (*games_sync_flash_cb)(bool writing);
int games_cache_sync(games_sync_progress_cb progress,
                     games_sync_flash_cb flash_phase);

// Read gNNNN.cmd for catalog index i into `buf` (capacity `max`).
// Returns true and sets *out_size on success.
bool games_cache_load_cmd(int index, uint8_t *buf, size_t max, size_t *out_size);
