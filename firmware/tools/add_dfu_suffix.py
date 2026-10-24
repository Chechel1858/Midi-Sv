#!/usr/bin/env python3
"""Appends a DFU 1.0 file suffix (VID/PID + CRC) to a raw firmware image.

dfu-util checks it before flashing, so the file can only go to the
MIDI-CV Alpha updater (1209:0002), and strips it before downloading.

usage: add_dfu_suffix.py input.bin output.dfu [vid] [pid]
"""
import struct
import sys
import zlib


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    vid = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x1209
    pid = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x0002

    data = open(src, "rb").read()
    # bcdDevice (any), idProduct, idVendor, bcdDFU 1.0, "UFD", length 16
    data += struct.pack("<HHHH", 0xFFFF, pid, vid, 0x0100) + b"UFD" + bytes([16])
    crc = ~zlib.crc32(data) & 0xFFFFFFFF  # dfu-util: CRC32 without the final inversion
    data += struct.pack("<I", crc)
    open(dst, "wb").write(data)


if __name__ == "__main__":
    main()
