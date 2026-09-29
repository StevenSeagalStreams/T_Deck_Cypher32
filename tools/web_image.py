#!/usr/bin/env python3
"""Turn an Arduino IDE build into what the browser flasher publishes.

The web installer ships exactly what the Arduino IDE builds — the same core
(Espressif Arduino-ESP32 3.2.0), the same board settings the README gives
(ESP32S3 Dev Module, OPI PSRAM, 16 MB, 3 MB app / 9 MB FATFS, USB CDC on
boot) — because that is the build players have run on real T-Decks. An
image from a different toolchain that "should" be equivalent is how a
flasher ends up writing something that boots to a black screen.

arduino-cli's merged image is the whole 16 MB chip, almost all of it blank
(0xFF). Blank flash is 0xFF after an erase, so everything past the last byte
of real data is dropped (rounded up to a 4 kB sector) — the same bytes the
board ends up with, a hundredth of the download.

    tools/web_image.py --build out --profile long --dest artifact/

writes artifact/cypher32-long.bin and artifact/parts-long.json in the form
tools/make_manifest.py expects.
"""
import argparse
import json
import os
import sys

SECTOR = 4096
APP_OFFSET = 0x10000          # app3M_fat9M_16MB: app0 at 0x10000


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True, help="arduino-cli --output-dir")
    ap.add_argument("--profile", required=True)
    ap.add_argument("--dest", required=True)
    args = ap.parse_args()

    merged = os.path.join(args.build, "cypher32.ino.merged.bin")
    app = os.path.join(args.build, "cypher32.ino.bin")
    boot = os.path.join(args.build, "cypher32.ino.bootloader.bin")
    for p in (merged, app, boot):
        if not os.path.isfile(p):
            print("missing %s" % p, file=sys.stderr)
            return 1

    data = open(merged, "rb").read()
    app_bytes = open(app, "rb").read()

    # Sanity: an ESP image starts with 0xE9, the bootloader at 0x0 on the S3
    # and the application where the partition table puts it. If either is not
    # where we think, the image would flash cleanly and never boot.
    if data[0] != 0xE9:
        print("no bootloader image at 0x0 (first byte 0x%02x)" % data[0], file=sys.stderr)
        return 1
    if data[APP_OFFSET:APP_OFFSET + len(app_bytes)] != app_bytes:
        print("the application is not at 0x%x in the merged image" % APP_OFFSET, file=sys.stderr)
        return 1

    end = len(data.rstrip(b"\xff"))
    end = (end + SECTOR - 1) // SECTOR * SECTOR
    out = data[:end]

    os.makedirs(args.dest, exist_ok=True)
    name = "cypher32-%s.bin" % args.profile
    with open(os.path.join(args.dest, name), "wb") as f:
        f.write(out)
    meta = {
        "env": args.profile,
        "chip": "esp32s3",
        "toolchain": "arduino-cli, esp32:esp32@3.2.0",
        "merged": name,
        "merged_bytes": len(out),
        "app_bytes": len(app_bytes),
        "parts": [{"offset": 0, "file": name, "bytes": len(out)}],
    }
    with open(os.path.join(args.dest, "parts-%s.json" % args.profile), "w") as f:
        json.dump(meta, f, indent=2)
    print("%s: %d bytes (app %d) from a %d-byte merged image"
          % (name, len(out), len(app_bytes), len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
