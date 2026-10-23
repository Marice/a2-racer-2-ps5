#!/usr/bin/env python3
"""Extracts the files of a Redump MODE2/2352 or MODE1/2352 disc image (one data track) into a folder.

usage: tools/extract-disc.py <image.bin> <out folder>

Reads each 2352-byte sector's 2048 user bytes (offset 24 for mode 2 form 1, 16 for mode 1), then walks the directory
tree: the Joliet one when the disc has it (the original capitalisation, as the game asks for its files: spel.dat,
Game.ini), else the ISO 9660 one (upper case). The ";1" version suffix goes.
Only for the user's own copy of the game: nothing here is shipped with the port."""
import os
import struct
import sys

SECTOR = 2352


def main(image, out):
    size = os.path.getsize(image)
    with open(image, "rb") as f:
        def sector(lba):
            f.seek(lba * SECTOR)
            raw = f.read(SECTOR)
            offset = 24 if raw[15] == 2 else 16
            return raw[offset:offset + 2048]

        def read(lba, length):
            data = bytearray()
            while len(data) < length:
                data += sector(lba)
                lba += 1
            return bytes(data[:length])

        pvd = sector(16)
        if pvd[1:6] != b"CD001":
            sys.exit("no ISO 9660 volume at sector 16")
        root, joliet = pvd[156:156 + 34], False
        for lba in range(17, 32):  # supplementary descriptors, up to the terminator
            vd = sector(lba)
            if vd[1:6] != b"CD001" or vd[0] == 255:
                break
            if vd[0] == 2 and vd[88:90] == b"%/" and vd[90:91] in (b"@", b"C", b"E"):
                root, joliet = vd[156:156 + 34], True
                break
        print("directory tree: " + ("Joliet" if joliet else "ISO 9660"))
        count = [0, 0]

        def walk(record, path):
            lba = struct.unpack_from("<I", record, 2)[0]
            length = struct.unpack_from("<I", record, 10)[0]
            data = read(lba, length)
            at = 0
            while at < len(data):
                n = data[at]
                if n == 0:  # records do not cross sectors: skip to the next one
                    at = (at // 2048 + 1) * 2048
                    continue
                entry = data[at:at + n]
                at += n
                name_len = entry[32]
                name = entry[33:33 + name_len]
                if name in (b"\x00", b"\x01"):
                    continue
                name = (name.decode("utf-16-be") if joliet else name.decode("latin-1")).split(";")[0].rstrip(".")
                target = os.path.join(path, name)
                if entry[25] & 2:
                    os.makedirs(target, exist_ok=True)
                    walk(entry, target)
                else:
                    file_lba = struct.unpack_from("<I", entry, 2)[0]
                    file_len = struct.unpack_from("<I", entry, 10)[0]
                    if (file_lba * SECTOR) + file_len > size + SECTOR:
                        print(f"skipped {target}: past the end of the image", file=sys.stderr)
                        continue
                    with open(target, "wb") as w:
                        w.write(read(file_lba, file_len))
                    count[0] += 1
                    count[1] += file_len

        os.makedirs(out, exist_ok=True)
        walk(root, out)
        print(f"{count[0]} files, {count[1]} bytes into {out}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
