#!/usr/bin/env python3
"""Download a game's CMD image from RetroStore:

    scripts/fetch_retrostore_cmd.py APP_ID OUT.cmd

The same request the firmware's Sync Games makes (fetchMediaImages, media type
COMMAND), for builds that carry a game in the firmware image instead of
downloading it. The SenseCAP Watcher build embeds Breakdown:

    scripts/fetch_retrostore_cmd.py 29b20252-680f-11e8-b4a9-1f10b5491ef5 \\
        main/watcher/breakdown.cmd

App ids are listed in GAME_KEYS.md. The API speaks protobuf; the two messages
involved are small enough to encode and decode by hand.
"""
import sys
import urllib.request

URL = "http://retrostore.org/api/fetchMediaImages"
MEDIA_TYPE_COMMAND = 3


def varint(buf, i):
    value = shift = 0
    while True:
        byte = buf[i]
        i += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            return value, i


def fields(buf):
    """Yield (field number, value) for each field of a protobuf message."""
    i = 0
    while i < len(buf):
        key, i = varint(buf, i)
        number, wire_type = key >> 3, key & 7
        if wire_type == 0:
            value, i = varint(buf, i)
        elif wire_type == 2:
            size, i = varint(buf, i)
            value = buf[i:i + size]
            i += size
        elif wire_type == 1:
            value = buf[i:i + 8]
            i += 8
        elif wire_type == 5:
            value = buf[i:i + 4]
            i += 4
        else:
            raise ValueError(f"unsupported protobuf wire type {wire_type}")
        yield number, value


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    app_id = sys.argv[1].encode()
    # FetchMediaImagesParams { app_id = 1; repeated media_type = 2 }
    body = bytes([0x0A, len(app_id)]) + app_id + bytes([0x10, MEDIA_TYPE_COMMAND])
    request = urllib.request.Request(
        URL, data=body, headers={"Content-Type": "application/octet-stream"})
    response = urllib.request.urlopen(request, timeout=30).read()

    # ApiResponseMediaImages { success = 1; message = 2; repeated MediaImage = 3 }
    # MediaImage { type = 1; filename = 2; data = 3 }
    message = b""
    for number, value in fields(response):
        if number == 2:
            message = value
        elif number == 3:
            image = dict(fields(value))
            if image.get(1) == MEDIA_TYPE_COMMAND and image.get(3):
                with open(sys.argv[2], "wb") as out:
                    out.write(image[3])
                print(f"{sys.argv[2]}: {len(image[3])} bytes "
                      f"({image.get(2, b'').decode(errors='replace')})")
                return
    sys.exit(f"no CMD image for {sys.argv[1]} ({message.decode(errors='replace')})")


if __name__ == "__main__":
    main()
