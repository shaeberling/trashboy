// See games_cache.h.

#include "games_cache.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <vector>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "retrostore.h"
#include "wear_levelling.h"

static const char *TAG = "games_cache";

#define MOUNT_POINT   "/games"
#define CATALOG_PATH  MOUNT_POINT "/catalog.bin"
#define GAMES_MAX     512    // catalog RAM cap: 512 * ~730 B ≈ 370 KB PSRAM
#define CMD_MAX_BYTES (64 * 1024)

// catalog.bin: header followed by `count` cached_game_t records.
struct catalog_header_t {
  uint32_t magic;     // 'TBGC'
  uint32_t version;   // bump when cached_game_t changes
  uint32_t count;
};
#define CATALOG_MAGIC   0x43474254u  // "TBGC" little-endian
#define CATALOG_VERSION 1u

static wl_handle_t    s_wl = WL_INVALID_HANDLE;
static bool           s_mounted = false;
static cached_game_t *s_games = nullptr;  // PSRAM, GAMES_MAX capacity
static int            s_count = 0;

static bool ensure_catalog_buf() {
  if (s_games == nullptr) {
    s_games = (cached_game_t *) heap_caps_malloc(
        sizeof(cached_game_t) * GAMES_MAX, MALLOC_CAP_SPIRAM);
    if (s_games == nullptr) {
      ESP_LOGE(TAG, "no PSRAM for catalog (%u bytes)",
               (unsigned) (sizeof(cached_game_t) * GAMES_MAX));
    }
  }
  return s_games != nullptr;
}

static void load_catalog() {
  s_count = 0;
  if (!ensure_catalog_buf()) return;

  FILE *f = fopen(CATALOG_PATH, "rb");
  if (f == nullptr) {
    ESP_LOGI(TAG, "no catalog yet (never synced)");
    return;
  }
  catalog_header_t hdr = {};
  if (fread(&hdr, sizeof(hdr), 1, f) != 1 ||
      hdr.magic != CATALOG_MAGIC || hdr.version != CATALOG_VERSION ||
      hdr.count > GAMES_MAX) {
    ESP_LOGW(TAG, "catalog header invalid; ignoring (re-sync to rebuild)");
    fclose(f);
    return;
  }
  size_t got = fread(s_games, sizeof(cached_game_t), hdr.count, f);
  fclose(f);
  if (got != hdr.count) {
    ESP_LOGW(TAG, "catalog truncated (%u/%u records); ignoring",
             (unsigned) got, (unsigned) hdr.count);
    return;
  }
  s_count = (int) hdr.count;
  ESP_LOGI(TAG, "catalog loaded: %d games", s_count);
}

bool games_cache_mount() {
  if (s_mounted) return true;

  const esp_vfs_fat_mount_config_t cfg = {
    .format_if_mount_failed = true,   // first boot: blank partition
    .max_files = 4,
    .allocation_unit_size = 4096,
    .disk_status_check_enable = false,
    .use_one_fat = false,
  };
  esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_POINT, "games",
                                                   &cfg, &s_wl);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
    return false;
  }
  s_mounted = true;
  load_catalog();
  return true;
}

int games_cache_count() {
  return s_count;
}

const cached_game_t *games_cache_get(int index) {
  if (index < 0 || index >= s_count) return nullptr;
  return &s_games[index];
}

static void cmd_path(char *out, size_t out_len, int index) {
  snprintf(out, out_len, MOUNT_POINT "/g%04d.cmd", index);
}

// Copy a std::string into a fixed char array, always NUL-terminated.
static void copy_str(char *dst, size_t dst_len, const std::string &src) {
  snprintf(dst, dst_len, "%s", src.c_str());
}

// PSRAM staging for downloaded CMD images, so the network phase performs
// zero flash writes (flash erases stall the RGB panel — see header).
struct staged_cmd_t {
  uint8_t *data;
  size_t   size;
};
#define STAGE_BUDGET_BYTES (2 * 1024 * 1024)

// Write staged entries [from, to) to flash and free their buffers.
// has_cmd is only set once the file is safely on flash.
static void flush_staged(staged_cmd_t *staged, int from, int to,
                         games_sync_flash_cb flash_phase) {
  bool any = false;
  for (int i = from; i < to && !any; i++) any = staged[i].data != nullptr;
  if (!any) return;

  if (flash_phase) flash_phase(true);
  for (int i = from; i < to; i++) {
    if (staged[i].data == nullptr) continue;
    char path[48];
    cmd_path(path, sizeof(path), i);
    FILE *f = fopen(path, "wb");
    if (f != nullptr) {
      size_t wrote = fwrite(staged[i].data, 1, staged[i].size, f);
      fclose(f);
      if (wrote == staged[i].size) {
        s_games[i].has_cmd = 1;
      } else {
        ESP_LOGE(TAG, "short write for %s (%u/%u)", path,
                 (unsigned) wrote, (unsigned) staged[i].size);
        remove(path);
      }
    } else {
      ESP_LOGE(TAG, "fopen(%s) failed", path);
    }
    heap_caps_free(staged[i].data);
    staged[i].data = nullptr;
    staged[i].size = 0;
  }
  if (flash_phase) flash_phase(false);
}

int games_cache_sync(games_sync_progress_cb progress,
                     games_sync_flash_cb flash_phase) {
  if (!s_mounted && !games_cache_mount()) return -1;
  if (!ensure_catalog_buf()) return -1;

  retrostore::RetroStore rs;

  // 1) Page through the full catalog (network only).
  std::vector<retrostore::RsAppNano> all;
  const int PAGE = 16;
  while ((int) all.size() < GAMES_MAX) {
    std::vector<retrostore::RsAppNano> page;
    if (!rs.FetchAppsNano((int) all.size(), PAGE, &page)) {
      // A failed page fetch on a non-empty catalog means "end of list" for
      // some server versions; only treat page 0 failing as a hard error.
      if (all.empty()) {
        ESP_LOGE(TAG, "catalog fetch failed");
        return -1;
      }
      break;
    }
    if (page.empty()) break;
    for (auto &a : page) {
      if ((int) all.size() >= GAMES_MAX) break;
      all.push_back(a);
    }
    if ((int) page.size() < PAGE) break;
  }
  const int total = (int) all.size();
  ESP_LOGI(TAG, "sync: %d games in catalog", total);
  if (total == 0) return -1;

  staged_cmd_t *staged = (staged_cmd_t *) heap_caps_calloc(
      GAMES_MAX, sizeof(staged_cmd_t), MALLOC_CAP_SPIRAM);
  if (staged == nullptr) return -1;
  size_t staged_bytes = 0;
  int flush_from = 0;

  // 2) Per game: description + COMMAND image, staged into PSRAM. Flush a
  //    batch to flash only when the staging budget fills up.
  for (int i = 0; i < total; i++) {
    const auto &nano = all[i];
    cached_game_t *g = &s_games[i];
    memset(g, 0, sizeof(*g));
    copy_str(g->id, sizeof(g->id), nano.id);
    copy_str(g->name, sizeof(g->name), nano.name);
    copy_str(g->author, sizeof(g->author), nano.author);
    copy_str(g->version, sizeof(g->version), nano.version);
    g->release_year = (uint16_t) nano.release_year;
    g->model = (uint8_t) nano.model;

    if (progress) progress(i, total, g->name);

    retrostore::RsApp app;
    if (rs.FetchApp(nano.id, &app)) {
      copy_str(g->description, sizeof(g->description), app.description);
    } else {
      ESP_LOGW(TAG, "FetchApp(%s) failed; caching without description",
               g->name);
    }

    std::vector<retrostore::RsMediaImage> images;
    std::vector<retrostore::RsMediaType> types =
        { retrostore::RsMediaType_COMMAND };
    if (rs.FetchMediaImages(nano.id, types, &images) && !images.empty() &&
        images[0].data_size > 0 &&
        (size_t) images[0].data_size <= CMD_MAX_BYTES) {
      const size_t sz = (size_t) images[0].data_size;
      uint8_t *copy = (uint8_t *) heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
      if (copy == nullptr) {
        // PSRAM tight: free staging space with an early flush, then retry.
        flush_staged(staged, flush_from, i, flash_phase);
        flush_from = i;
        staged_bytes = 0;
        copy = (uint8_t *) heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
      }
      if (copy != nullptr) {
        memcpy(copy, images[0].data.get(), sz);
        staged[i].data = copy;
        staged[i].size = sz;
        staged_bytes += sz;
      } else {
        ESP_LOGE(TAG, "no PSRAM to stage '%s' (%u bytes); skipping",
                 g->name, (unsigned) sz);
      }
    } else {
      ESP_LOGI(TAG, "no runnable CMD image for '%s'", g->name);
    }

    if (staged_bytes >= STAGE_BUDGET_BYTES) {
      flush_staged(staged, flush_from, i + 1, flash_phase);
      flush_from = i + 1;
      staged_bytes = 0;
    }
  }

  // 3) Final flash burst: remaining staged images + the catalog.
  if (flash_phase) flash_phase(true);
  for (int i = flush_from; i < total; i++) {
    if (staged[i].data == nullptr) continue;
    char path[48];
    cmd_path(path, sizeof(path), i);
    FILE *f = fopen(path, "wb");
    if (f != nullptr) {
      size_t wrote = fwrite(staged[i].data, 1, staged[i].size, f);
      fclose(f);
      if (wrote == staged[i].size) {
        s_games[i].has_cmd = 1;
      } else {
        ESP_LOGE(TAG, "short write for %s", path);
        remove(path);
      }
    }
    heap_caps_free(staged[i].data);
    staged[i].data = nullptr;
  }

  bool ok = false;
  FILE *f = fopen(CATALOG_PATH, "wb");
  if (f != nullptr) {
    catalog_header_t hdr = { CATALOG_MAGIC, CATALOG_VERSION, (uint32_t) total };
    ok = fwrite(&hdr, sizeof(hdr), 1, f) == 1 &&
         fwrite(s_games, sizeof(cached_game_t), total, f) == (size_t) total;
    fclose(f);
  }
  if (flash_phase) flash_phase(false);

  heap_caps_free(staged);

  if (!ok) {
    ESP_LOGE(TAG, "catalog write failed");
    return -1;
  }

  s_count = total;
  ESP_LOGI(TAG, "sync complete: %d games cached", s_count);
  return s_count;
}

bool games_cache_load_cmd(int index, uint8_t *buf, size_t max,
                          size_t *out_size) {
  const cached_game_t *g = games_cache_get(index);
  if (g == nullptr || !g->has_cmd || buf == nullptr) return false;

  char path[48];
  cmd_path(path, sizeof(path), index);
  FILE *f = fopen(path, "rb");
  if (f == nullptr) {
    ESP_LOGE(TAG, "fopen(%s) failed", path);
    return false;
  }
  size_t got = fread(buf, 1, max, f);
  bool eof = feof(f) != 0;
  fclose(f);
  if (got == 0 || !eof) {
    ESP_LOGE(TAG, "read %s failed (got=%u eof=%d)", path, (unsigned) got, eof);
    return false;
  }
  if (out_size) *out_size = got;
  return true;
}
