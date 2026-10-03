#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Serve the files in the "trsdisk" flash partition (FREHD.ROM and hard-disk
// images, packed by scripts/pack_trs_disk.py) to FreHD, read-only, ahead of
// an SMB share. Call once, after init_trs_io() and before the Z80 runs.
// Without the partition or with an empty one, nothing changes.
void flash_disk_mount(void);

#ifdef __cplusplus
}
#endif
