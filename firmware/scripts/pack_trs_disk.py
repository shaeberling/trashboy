#!/usr/bin/env python3
"""Pack files for FreHD into an image for the "trsdisk" flash partition.

The TRS-80 boots a hard-disk DOS from internal flash through FreHD: the boot
ROM's loader opens FREHD.ROM, which offers the hard-disk images it finds
(the ones with an autoboot entry in their header). So pack at least
FREHD.ROM and one image:

    scripts/pack_trs_disk.py -o trsdisk.bin ../trs-io-sd-card/FREHD.ROM \\
        ../trs-io-sd-card/NEWDOS3D
    idf.py trsdisk-flash

A hard-disk image is mostly formatted-but-unused sectors (all E5H) and
zeros: NEWDOS3D is a 64 MB file with 760 KB of data. Every file is stored
as extents of 256-byte blocks; an extent either holds the blocks' data or
says "all bytes are X", and blocks no extent covers are the file's fill
byte. The firmware (main/flash_disk.cpp) shows each file at its full size.

The flash copy is read-only, so hard-disk images get their write-protect
flag set (header byte 7, bit 7) unless --writable is given.

Layout, little-endian, offsets from the start of the partition:
  header   "TBFD", u16 version, u16 file count, u32 total length,
           u32 CRC-32 of bytes [16, total length)               16 bytes
  files    char name[13], u8 fill, u16 0, u32 size,
           u32 extent count, u32 extent table offset, u32 0    32 bytes each
  extents  u32 first block, u32 block count,
           u32 data offset, or FILL_FLAG | byte                 12 bytes each
  data     the stored blocks, in extent order
"""
import argparse
import os
import struct
import sys
import zlib
from collections import Counter

MAGIC = b"TBFD"
VERSION = 1
BLOCK = 256
FILL_FLAG = 0x80000000
HEADER = struct.Struct("<4sHHII")
FILE = struct.Struct("<13sBHIIII")
EXTENT = struct.Struct("<III")


def reed_write_protect(data):
    """Set the write-protect bit of a Reed/xtrshard hard-disk header."""
    if len(data) < BLOCK or data[0] != 0x56 or data[1] != 0xCB:
        return data
    hdr = bytearray(data[:BLOCK])
    hdr[7] |= 0x80
    # Byte 3 (checksum) stays as it is: FreHD never checks it, and the xtrs
    # images in trs-io-sd-card/ don't follow the formula in reed.h anyway.
    return bytes(hdr) + data[BLOCK:]


def name_83(path):
    name = os.path.basename(path).upper()
    base, _, ext = name.partition(".")
    if not base or len(base) > 8 or len(ext) > 3 or "." in ext:
        sys.exit("%s: not an 8.3 file name" % name)
    return name


def extents_for(data):
    """Return (fill byte, [(first block, count, kind, payload)]).

    kind is "fill" (payload = the byte) or "data" (payload = the bytes).
    """
    nblocks = (len(data) + BLOCK - 1) // BLOCK
    kinds = []
    for i in range(nblocks):
        b = data[i * BLOCK:(i + 1) * BLOCK]
        kinds.append(b[0] if b.count(b[:1]) == len(b) == BLOCK else None)
    uniform = Counter(k for k in kinds if k is not None)
    fill = uniform.most_common(1)[0][0] if uniform else 0
    extents = []
    i = 0
    while i < nblocks:
        k = kinds[i]
        j = i + 1
        while j < nblocks and kinds[j] == k:
            j += 1
        if k is None:
            extents.append((i, j - i, "data", data[i * BLOCK:j * BLOCK]))
        elif k != fill:
            extents.append((i, j - i, "fill", k))
        i = j
    return fill, extents


def pack(paths, writable):
    files = []
    for path in paths:
        with open(path, "rb") as f:
            data = f.read()
        if not writable:
            data = reed_write_protect(data)
        fill, extents = extents_for(data)
        files.append((name_83(path), data, fill, extents))
    names = [f[0] for f in files]
    if len(set(names)) != len(names):
        sys.exit("duplicate file names: %s" % names)

    table_off = HEADER.size + FILE.size * len(files)
    ext_off = table_off
    data_off = ext_off + EXTENT.size * sum(len(f[3]) for f in files)
    entries, ext_blob, data_blob = [], bytearray(), bytearray()
    for name, data, fill, extents in files:
        entries.append(FILE.pack(name.encode(), fill, 0, len(data), len(extents),
                                 ext_off + len(ext_blob), 0))
        for first, count, kind, payload in extents:
            if kind == "fill":
                ref = FILL_FLAG | payload
            else:
                ref = data_off + len(data_blob)
                # A short last block is padded; reads stop at the file size.
                data_blob += payload + bytes(count * BLOCK - len(payload))
            ext_blob += EXTENT.pack(first, count, ref)
    body = b"".join(entries) + bytes(ext_blob) + bytes(data_blob)
    total = HEADER.size + len(body)
    header = HEADER.pack(MAGIC, VERSION, len(files), total, zlib.crc32(body))
    return header + body, files


def unpack(image, name):
    """Rebuild one file from the image, as the firmware reads it."""
    _, _, count, _, _ = HEADER.unpack_from(image, 0)
    for n in range(count):
        fname, fill, _, size, n_ext, ext_off, _ = FILE.unpack_from(
            image, HEADER.size + n * FILE.size)
        if fname.rstrip(b"\0").decode() != name:
            continue
        out = bytearray([fill]) * size
        for e in range(n_ext):
            first, blocks, ref = EXTENT.unpack_from(image, ext_off + e * EXTENT.size)
            start = first * BLOCK
            end = min(size, (first + blocks) * BLOCK)
            if ref & FILL_FLAG:
                out[start:end] = bytes([ref & 0xFF]) * (end - start)
            else:
                out[start:end] = image[ref:ref + end - start]
        return bytes(out)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="+", help="FREHD.ROM, disk images, other files")
    ap.add_argument("-o", "--output", default="trsdisk.bin")
    ap.add_argument("--size", default="0x200000",
                    help="partition size to check against (default 2 MB)")
    ap.add_argument("--writable", action="store_true",
                    help="leave the write-protect flag of hard-disk images alone")
    args = ap.parse_args()

    image, files = pack(args.files, args.writable)
    for name, data, fill, extents in files:
        if unpack(image, name) != data:
            sys.exit("%s: unpacked copy differs, packer bug" % name)
        stored = sum(e[1] for e in extents if e[2] == "data") * BLOCK
        print("%-12s %10d bytes -> %8d stored, %4d extents, fill %02XH"
              % (name, len(data), stored, len(extents), fill))
    limit = int(args.size, 0)
    print("total %d bytes (%.0f%% of the %d KB partition)"
          % (len(image), 100.0 * len(image) / limit, limit // 1024))
    if len(image) > limit:
        sys.exit("too big for the partition")
    if "FREHD.ROM" not in [f[0] for f in files]:
        print("warning: no FREHD.ROM, FreHD's boot loader needs it")
    with open(args.output, "wb") as f:
        f.write(image)


if __name__ == "__main__":
    main()
