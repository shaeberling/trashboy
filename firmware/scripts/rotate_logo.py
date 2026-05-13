#!/usr/bin/env python3
"""Rotate logo90c.c (182x596 portrait RGB565) 90° CW to produce
logoc.c (596x182 landscape RGB565). The destination C array matches
the same LVGL lv_image_dsc_t layout.

Run from the trashboy/firmware directory:
  python3 scripts/rotate_logo.py main/logo90c.c main/logoc.c
"""
import re
import sys


def parse_logo_c(path):
    with open(path) as f:
        text = f.read()

    m = re.search(r"\.header\.w\s*=\s*(\d+)", text)
    w = int(m.group(1))
    m = re.search(r"\.header\.h\s*=\s*(\d+)", text)
    h = int(m.group(1))

    # Find the map[] body: from `{` after `_map[] = ` to matching `};`
    m = re.search(r"_map\[\]\s*=\s*\{([^}]*)\};", text, re.DOTALL)
    body = m.group(1)

    bytes_hex = re.findall(r"0x([0-9a-fA-F]{2})", body)
    pixels = []
    # RGB565: little-endian 2 bytes per pixel
    for i in range(0, len(bytes_hex), 2):
        lo = int(bytes_hex[i], 16)
        hi = int(bytes_hex[i + 1], 16)
        pixels.append((hi << 8) | lo)
    assert len(pixels) == w * h, f"expected {w*h} pixels, got {len(pixels)}"
    return w, h, pixels


def rotate_cw_90(src_w, src_h, pixels):
    """Rotate a (src_w x src_h) image 90° CW into a (src_h x src_w) image.

    Mapping: source (sx, sy) -> dest (dx, dy) where
        dx = src_h - 1 - sy
        dy = sx
    Equivalently, dest (dx, dy) reads from source (sy=dy, sx=src_h-1-dx).
    Wait, let me redo: if dx = src_h-1-sy then sy = src_h-1-dx.
    And dy = sx, so sx = dy.
    """
    dest_w = src_h
    dest_h = src_w
    out = [0] * (dest_w * dest_h)
    for dy in range(dest_h):
        for dx in range(dest_w):
            sx = dy
            sy = src_h - 1 - dx
            out[dy * dest_w + dx] = pixels[sy * src_w + sx]
    return dest_w, dest_h, out


def emit_logo_c(path, w, h, pixels, sym):
    lines = []
    lines.append(
        "#ifdef __has_include\n"
        "    #if __has_include(\"lvgl.h\")\n"
        "        #ifndef LV_LVGL_H_INCLUDE_SIMPLE\n"
        "            #define LV_LVGL_H_INCLUDE_SIMPLE\n"
        "        #endif\n"
        "    #endif\n"
        "#endif\n\n"
        "#if defined(LV_LVGL_H_INCLUDE_SIMPLE)\n"
        "    #include \"lvgl.h\"\n"
        "#else\n"
        "    #include \"lvgl/lvgl.h\"\n"
        "#endif\n\n"
        "#ifndef LV_ATTRIBUTE_MEM_ALIGN\n"
        "#define LV_ATTRIBUTE_MEM_ALIGN\n"
        "#endif\n\n"
        f"#ifndef LV_ATTRIBUTE_IMAGE_{sym.upper()}\n"
        f"#define LV_ATTRIBUTE_IMAGE_{sym.upper()}\n"
        "#endif\n\n"
        f"const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST "
        f"LV_ATTRIBUTE_IMAGE_{sym.upper()} uint8_t {sym}_map[] = {{\n"
    )
    chunk = []
    for px in pixels:
        lo = px & 0xFF
        hi = (px >> 8) & 0xFF
        chunk.append(f"0x{lo:02x}, 0x{hi:02x}")
    # 16 pixels per line
    per_line = 16
    for i in range(0, len(chunk), per_line):
        lines.append("  " + ", ".join(chunk[i : i + per_line]) + ",\n")
    lines.append("};\n\n")
    lines.append(
        f"const lv_image_dsc_t {sym} = {{\n"
        f"  .header.cf = LV_COLOR_FORMAT_RGB565,\n"
        f"  .header.magic = LV_IMAGE_HEADER_MAGIC,\n"
        f"  .header.w = {w},\n"
        f"  .header.h = {h},\n"
        f"  .data_size = {len(pixels)} * 2,\n"
        f"  .data = {sym}_map,\n"
        f"}};\n"
    )
    with open(path, "w") as f:
        f.write("".join(lines))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    src_path, dst_path = sys.argv[1], sys.argv[2]
    w, h, pixels = parse_logo_c(src_path)
    print(f"source: {w}x{h} ({len(pixels)} px)")
    new_w, new_h, new_pixels = rotate_cw_90(w, h, pixels)
    print(f"dest:   {new_w}x{new_h}")
    sym = "logoc"
    emit_logo_c(dst_path, new_w, new_h, new_pixels, sym)
    print(f"wrote {dst_path} (symbol: {sym})")


if __name__ == "__main__":
    main()
