// FreHD storage in internal flash: a read-only TRS-IO file system backend
// (TRS_FS) serving the files in the "trsdisk" partition, so the TRS-80 boots
// a hard-disk DOS without an SD card or an SMB share.
//
// FreHD's boot loader opens FREHD.ROM, which lists the directory for
// hard-disk images with an autoboot entry and mounts the one it boots. The
// partition holds those files as written by scripts/pack_trs_disk.py: each
// file is a list of extents of 256-byte blocks that either carry data or
// say "every byte is X"; blocks outside all extents are the file's fill
// byte. A hard disk is mostly formatted-but-unused sectors (E5H), so
// NEWDOS3D (64 MB) takes 760 KB. FreHD still sees every file at full size.
//
// The partition is memory-mapped: reads go through the flash cache like code
// fetches, with no flash operation that could stall the panel. Writes are
// refused (FR_WRITE_PROTECTED); the packer also sets the write-protect flag
// in each hard-disk header. TRS-IO prefers this backend to SMB (see
// set_fs() in trs-fs.cpp), so the SMB share is not visible to FreHD while
// the partition holds files.

#include "flash_disk.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "trs-fs.h"

static const char *TAG = "flash_disk";

#define BLOCK_SIZE 256
#define FILL_FLAG  0x80000000u

struct __attribute__((packed)) pack_header {
  char magic[4];             // "TBFD"
  uint16_t version;          // 1
  uint16_t file_count;
  uint32_t total_len;        // the whole pack, this header included
  uint32_t crc32;            // of bytes [16, total_len)
};

struct __attribute__((packed)) pack_file {
  char name[13];             // 8.3, NUL-terminated
  uint8_t fill;              // byte of blocks no extent covers
  uint16_t reserved;
  uint32_t size;             // the file's size as FreHD sees it
  uint32_t extent_count;
  uint32_t extents;          // offset of the extent table, sorted by block
  uint32_t reserved2;
};

struct __attribute__((packed)) pack_extent {
  uint32_t first_block;
  uint32_t block_count;
  uint32_t data;             // offset of the blocks' data, or FILL_FLAG | byte
};

static_assert(sizeof(pack_header) == 16, "pack_header");
static_assert(sizeof(pack_file) == 32, "pack_file");
static_assert(sizeof(pack_extent) == 12, "pack_extent");

// An open file: FIL::f points at one of these.
struct flash_file {
  const pack_file *file;
  uint32_t pos;
};

class TRS_FS_FLASH : public TRS_FS {
 public:
  TRS_FS_FLASH(const uint8_t *base, const pack_header *hdr)
      : base_(base),
        files_(reinterpret_cast<const pack_file *>(base + sizeof(pack_header))),
        count_(hdr->file_count) {
    err_msg = NULL;
  }

  FS_TYPE type() override { return FS_LOCAL; }

  void f_log(const char *msg) override {}

  FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode) override {
    const pack_file *file = find(path);
    if (file == NULL) {
      return (mode & (FA_CREATE_NEW | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS))
                 ? FR_WRITE_PROTECTED : FR_NO_FILE;
    }
    if (mode & FA_CREATE_NEW) {
      return FR_EXIST;
    }
    if (mode & FA_CREATE_ALWAYS) {
      return FR_WRITE_PROTECTED;  // would truncate it
    }
    // Opening for writing succeeds: FreHD mounts its drives read/write.
    // The writes themselves fail.
    flash_file *f = (flash_file *) malloc(sizeof(flash_file));
    if (f == NULL) {
      return FR_NOT_ENOUGH_CORE;
    }
    f->file = file;
    f->pos = 0;
    fp->f = f;
    return FR_OK;
  }

  // The pack has one directory, the root. dp->dir is the index of the next
  // entry, so there is nothing to free (the API has no closedir).
  FRESULT f_opendir(DIR_ *dp, const TCHAR *path) override {
    while (*path == '/' || (path[0] == '.' && (path[1] == '\0' || path[1] == '/'))) {
      path++;
    }
    if (*path != '\0') {
      return FR_NO_PATH;
    }
    dp->dir = (void *) (uintptr_t) 0;
    return FR_OK;
  }

  FRESULT f_readdir(DIR_ *dp, FILINFO *fno) override {
    uintptr_t i = (uintptr_t) dp->dir;
    if (i >= count_) {
      fno->fname[0] = '\0';  // end of the directory
      return FR_OK;
    }
    fill_info(&files_[i], fno);
    dp->dir = (void *) (i + 1);
    return FR_OK;
  }

  FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw) override {
    *bw = 0;
    return FR_WRITE_PROTECTED;
  }

  FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br) override {
    flash_file *f = (flash_file *) fp->f;
    if (f == NULL) {
      return FR_INVALID_OBJECT;
    }
    const pack_file *file = f->file;
    uint8_t *out = (uint8_t *) buff;
    uint32_t n = 0;
    while (n < btr && f->pos < file->size) {
      uint32_t block = f->pos / BLOCK_SIZE;
      uint32_t off = f->pos % BLOCK_SIZE;
      uint32_t len = BLOCK_SIZE - off;
      if (len > btr - n) {
        len = btr - n;
      }
      if (len > file->size - f->pos) {
        len = file->size - f->pos;
      }
      const pack_extent *e = find_extent(file, block);
      if (e == NULL) {
        memset(out + n, file->fill, len);
      } else if (e->data & FILL_FLAG) {
        memset(out + n, e->data & 0xff, len);
      } else {
        memcpy(out + n,
               base_ + e->data + (block - e->first_block) * BLOCK_SIZE + off, len);
      }
      n += len;
      f->pos += len;
    }
    *br = n;
    return FR_OK;
  }

  FSIZE_t f_tell(FIL *fp) override {
    flash_file *f = (flash_file *) fp->f;
    return (f == NULL) ? 0 : f->pos;
  }

  FRESULT f_sync(FIL *fp) override { return FR_OK; }

  FRESULT f_lseek(FIL *fp, FSIZE_t ofs) override {
    flash_file *f = (flash_file *) fp->f;
    if (f == NULL) {
      return FR_INVALID_OBJECT;
    }
    f->pos = ofs;  // past the end is fine: reads there return no data
    return FR_OK;
  }

  FRESULT f_close(FIL *fp) override {
    free(fp->f);
    fp->f = NULL;
    return FR_OK;
  }

  FRESULT f_unlink(const TCHAR *path) override {
    return (find(path) == NULL) ? FR_NO_FILE : FR_WRITE_PROTECTED;
  }

  FRESULT f_stat(const TCHAR *path, FILINFO *fno) override {
    const pack_file *file = find(path);
    if (file == NULL) {
      return FR_NO_FILE;
    }
    fill_info(file, fno);
    return FR_OK;
  }

 private:
  const pack_file *find(const TCHAR *path) const {
    while (*path == '/') {
      path++;
    }
    for (int i = 0; i < count_; i++) {
      if (strcasecmp(files_[i].name, path) == 0) {
        return &files_[i];
      }
    }
    return NULL;
  }

  // The extent holding `block`, or NULL if the block is the fill byte.
  const pack_extent *find_extent(const pack_file *file, uint32_t block) const {
    const pack_extent *ext = reinterpret_cast<const pack_extent *>(base_ + file->extents);
    uint32_t lo = 0, hi = file->extent_count;
    while (lo < hi) {  // the first extent starting after `block`
      uint32_t mid = (lo + hi) / 2;
      if (ext[mid].first_block <= block) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    if (lo == 0) {
      return NULL;
    }
    const pack_extent *e = &ext[lo - 1];
    return (block - e->first_block < e->block_count) ? e : NULL;
  }

  static void fill_info(const pack_file *file, FILINFO *fno) {
    memset(fno, 0, sizeof(*fno));
    fno->fsize = file->size;
    strlcpy(fno->fname, file->name, sizeof(fno->fname));
  }

  const uint8_t *base_;
  const pack_file *files_;
  int count_;
};

// Every offset in the pack must stay inside it: a pack that doesn't fit the
// partition, or a half-written one, is refused rather than read past.
static bool pack_valid(const uint8_t *base, uint32_t part_size) {
  const pack_header *hdr = (const pack_header *) base;
  if (memcmp(hdr->magic, "TBFD", 4) != 0) {
    ESP_LOGI(TAG, "no FreHD files in flash (pack with scripts/pack_trs_disk.py, "
                  "flash with idf.py trsdisk-flash)");
    return false;
  }
  uint32_t len = hdr->total_len;
  if (hdr->version != 1 || len > part_size || hdr->file_count == 0 ||
      len < sizeof(pack_header) + hdr->file_count * sizeof(pack_file)) {
    ESP_LOGE(TAG, "bad pack header (version %u, %u files, %lu bytes)",
             hdr->version, hdr->file_count, (unsigned long) len);
    return false;
  }
  uint32_t crc = esp_rom_crc32_le(0, base + sizeof(pack_header), len - sizeof(pack_header));
  if (crc != hdr->crc32) {
    ESP_LOGE(TAG, "pack CRC mismatch (%08lx, expected %08lx): reflash it",
             (unsigned long) crc, (unsigned long) hdr->crc32);
    return false;
  }
  const pack_file *files = (const pack_file *) (base + sizeof(pack_header));
  for (int i = 0; i < hdr->file_count; i++) {
    const pack_file *f = &files[i];
    if (memchr(f->name, '\0', sizeof(f->name)) == NULL ||
        f->extents > len || f->extent_count > (len - f->extents) / sizeof(pack_extent)) {
      ESP_LOGE(TAG, "bad file entry %d", i);
      return false;
    }
    const pack_extent *ext = (const pack_extent *) (base + f->extents);
    for (uint32_t j = 0; j < f->extent_count; j++) {
      const pack_extent *e = &ext[j];
      if (e->data & FILL_FLAG) {
        continue;
      }
      if (e->data > len || e->block_count > (len - e->data) / BLOCK_SIZE) {
        ESP_LOGE(TAG, "%s: extent %lu out of bounds", f->name, (unsigned long) j);
        return false;
      }
    }
  }
  return true;
}

void flash_disk_mount(void) {
  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "trsdisk");
  if (part == NULL) {
    ESP_LOGW(TAG, "no trsdisk partition");
    return;
  }
  const void *ptr;
  esp_partition_mmap_handle_t handle;
  esp_err_t err = esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA,
                                     &ptr, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mmap failed: %s", esp_err_to_name(err));
    return;
  }
  const uint8_t *base = (const uint8_t *) ptr;
  if (!pack_valid(base, part->size)) {
    esp_partition_munmap(handle);
    return;
  }
  const pack_header *hdr = (const pack_header *) base;
  const pack_file *files = (const pack_file *) (base + sizeof(pack_header));
  for (int i = 0; i < hdr->file_count; i++) {
    ESP_LOGI(TAG, "%-12s %10lu bytes", files[i].name, (unsigned long) files[i].size);
  }
  // Lives for the rest of the run, like the mapping.
  TRS_FS_FLASH *fs = new TRS_FS_FLASH(base, hdr);
  const char *msg = init_trs_fs_local(fs);
  ESP_LOGI(TAG, "%u files, %lu KB of flash, %s", hdr->file_count,
           (unsigned long) (hdr->total_len / 1024), msg ? msg : "serving FreHD");
}
