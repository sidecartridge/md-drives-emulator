#!/usr/bin/env python3
"""Build the floppy image FLOPTEST reads, with every byte predictable.

    tools/dev/make_floppy_image.py ro FLOPTEST.ST          # the read-only disk
    tools/dev/make_floppy_image.py rw FLOPTEST.ST.RW       # the writable one
    tools/dev/make_floppy_image.py ro-hd FLOPTEST.ST       # 1.44 MB, read-only
    tools/dev/make_floppy_image.py rw-hd FLOPTEST.ST.RW    # 1.44 MB, writable
    tools/dev/make_floppy_image.py ro-ss FLOPTEST.ST       # one-sided BPB, two sides
    tools/dev/make_floppy_image.py check-rw FLOPTEST.ST.RW # after a run

A 720 KB double-sided disk: 80 tracks, 2 sides, 9 sectors of 512 bytes - or,
with -hd, a 1.44 MB one with 18 sectors a track and 5-sector FATs. It is a
normal FAT12 disk that TOS and the desktop can open, and it is also a disk
whose raw sectors a test can check without knowing anything but their number.

ro-ss is the shape of many menu disks: a two-sided 720 KB disk whose boot
sector says one side, with a one-sided file system (720 sectors, 2-sector
FATs) on side 0 and side 1 left to the disk's own loader. TOS reads the file
system with the BPB's geometry, so record r is sector r % 9 of track r / 9,
side 0; there every sector's signature names where it sits on the two-sided
disk, whichever way it is read. MODE.TXT says SS.

- README.TXT and MODE.TXT hold known text. MODE.TXT says RO or RW, so a test
  knows which of the two it is reading without being told.
- PATTERN.BIN is 20480 bytes, byte i being (i * 7 + (i >> 9)) & 0xFF: the
  second term changes every 512 bytes, so two sectors swapped do not compare
  equal.
- PROG.TOS is a program of one instruction pair, Pterm0, whose header asks for
  flags 5. Loading it from here is TOS's own loader at work, not a hard-disk
  driver's, so it shows what TOS does with a program's flags.
- Every free sector starts "FL" followed by its own sector number (big
  endian), and byte k after that is (sector * 31 + k) & 0xFF. A sector read
  from the wrong place, the wrong side or the wrong drive says so.

A test that writes leaves one sector changed on purpose: sector 1300 holds
(1300 * 17 + k * 3 + 0x5A) & 0xFF afterwards. check-rw reads the image back
after a run and says whether that write reached the card.

The same rules are written in tests/atarist/src/floppy_tests.c; the two must
agree.
"""

import struct
import sys

SECTOR = 512
SIDES = 2
TRACKS = 80
SECTORS_PER_CLUSTER = 2
RESERVED = 1
FATS = 2
ROOT_ENTRIES = 112
ROOT_SECTORS = ROOT_ENTRIES * 32 // SECTOR  # 7

# Sectors per track, sectors per FAT and media byte of the two densities. The
# FATs are sized for the cluster count; FLOPTEST expects exactly these.
DENSITIES = {
    "dd": (9, 3, 0xF9),   # 1440 sectors, data from 14, 713 clusters
    "hd": (18, 5, 0xF0),  # 2880 sectors, data from 18, 1431 clusters
    "ss": (9, 2, 0xF8),   # one side: 720 sectors, data from 12, 354 clusters
}

README = b"SidecarTridge floppy test image.\r\n"
PATTERN_SIZE = 20480
PROGRAM_FLAGS = 5
# clr.w -(sp) ; trap #1 - Pterm0 - after a 28-byte header, and a zero long
# that says there is nothing to relocate
PROGRAM = struct.pack(">HIIIIIIH", 0x601A, 4, 0, 0, 0, 0, PROGRAM_FLAGS, 0) + \
    b"\x42\x67\x4e\x41" + bytes(4)
WRITTEN_SECTOR = 1300

# 2026-09-20 12:00:00, in the directory entry's DOS format
DOS_DATE = ((2026 - 1980) << 9) | (9 << 5) | 20
DOS_TIME = 12 << 11


def signature(lba):
    """What a free sector holds: its own number, and a pattern from it."""
    data = bytearray(SECTOR)
    data[0:4] = bytes((0x46, 0x4C, (lba >> 8) & 0xFF, lba & 0xFF))
    for k in range(4, SECTOR):
        data[k] = (lba * 31 + k) & 0xFF
    return bytes(data)


def written(lba):
    """What a test writes over a free sector, to see the write arrive."""
    return bytes((lba * 17 + k * 3 + 0x5A) & 0xFF for k in range(SECTOR))


def pattern(size):
    return bytes((i * 7 + (i >> 9)) & 0xFF for i in range(size))


class Geometry:
    def __init__(self, density):
        self.spt, self.fat_sectors, self.media = DENSITIES[density]
        self.sides = 1 if density == "ss" else SIDES
        self.total = self.spt * self.sides * TRACKS
        self.first_data = RESERVED + FATS * self.fat_sectors + ROOT_SECTORS
        self.serial = {"dd": 1, "hd": 2, "ss": 3}[density]

    def cluster_sector(self, cluster):
        return self.first_data + (cluster - 2) * SECTORS_PER_CLUSTER


def boot_sector(mode, geo):
    boot = bytearray(SECTOR)
    boot[0:2] = b"\x60\x38"                   # bra.s, as a formatted disk has
    boot[2:8] = b"FLOPTE"
    # The serial number is how TOS tells one disk from another after a swap,
    # so the two variants must not share one.
    boot[8:11] = (b"RO" if mode == "ro" else b"RW") + bytes((geo.serial,))
    struct.pack_into("<HBHBHHBHHH", boot, 11,
                     SECTOR, SECTORS_PER_CLUSTER, RESERVED, FATS,
                     ROOT_ENTRIES, geo.total, geo.media, geo.fat_sectors,
                     geo.spt, geo.sides)
    # Not executable: TOS runs a boot sector whose words add up to $1234.
    if sum(struct.unpack(">256H", boot)) & 0xFFFF == 0x1234:
        boot[0x1E] ^= 0x01
    return bytes(boot)


def fat12(chains, geo):
    entries = [0] * (geo.fat_sectors * SECTOR * 2 // 3)
    entries[0], entries[1] = 0xF00 | geo.media, 0xFFF
    for clusters in chains:
        for current, following in zip(clusters, clusters[1:] + [0xFFF]):
            entries[current] = following
    table = bytearray(geo.fat_sectors * SECTOR)
    for n, value in enumerate(entries):
        offset = n * 3 // 2
        if n % 2 == 0:
            table[offset] = value & 0xFF
            table[offset + 1] = (table[offset + 1] & 0xF0) | (value >> 8)
        else:
            table[offset] = (table[offset] & 0x0F) | ((value << 4) & 0xF0)
            table[offset + 1] = value >> 4
    return bytes(table)


def dir_entry(name, ext, cluster, size):
    return struct.pack("<8s3sB10sHHHI", name.ljust(8).encode(),
                       ext.ljust(3).encode(), 0x20, bytes(10),
                       DOS_TIME, DOS_DATE, cluster, size)


def build(mode, density="dd"):
    geo = Geometry(density)
    mode_text = b"RO\r\n" if mode == "ro" else b"RW\r\n"
    if density == "ss":
        mode_text = b"SS\r\n"
    files = [
        ("README", "TXT", [2], README),
        ("MODE", "TXT", [3], mode_text),
        ("PATTERN", "BIN", list(range(4, 4 + PATTERN_SIZE // 1024)),
         pattern(PATTERN_SIZE)),
        ("PROG", "TOS", [4 + PATTERN_SIZE // 1024], PROGRAM),
    ]
    used = {c for _, _, clusters, _ in files for c in clusters}
    image = bytearray(geo.total * SECTOR)
    image[0:SECTOR] = boot_sector(mode, geo)

    table = fat12([clusters for _, _, clusters, _ in files], geo)
    for copy in range(FATS):
        start = (RESERVED + copy * geo.fat_sectors) * SECTOR
        image[start:start + len(table)] = table

    root = bytearray(ROOT_SECTORS * SECTOR)
    for n, (name, ext, clusters, content) in enumerate(files):
        root[n * 32:(n + 1) * 32] = dir_entry(name, ext, clusters[0],
                                              len(content))
        start = geo.cluster_sector(clusters[0]) * SECTOR
        image[start:start + len(content)] = content
    start = (RESERVED + FATS * geo.fat_sectors) * SECTOR
    image[start:start + len(root)] = root

    last_cluster = 2 + (geo.total - geo.first_data) // SECTORS_PER_CLUSTER - 1
    for cluster in range(2, last_cluster + 1):
        if cluster in used:
            continue
        for n in range(SECTORS_PER_CLUSTER):
            lba = geo.cluster_sector(cluster) + n
            image[lba * SECTOR:(lba + 1) * SECTOR] = signature(lba)
    if density == "ss":
        return two_sided(image, geo)
    return bytes(image)


def two_sided(logical, geo):
    """The one-sided file system on side 0 of a two-sided disk. Record r goes
    where TOS puts it with the BPB's geometry, track r / spt side 0; a free
    record, and every sector of side 1, holds the signature of its place on
    the two-sided disk."""
    physical = [signature(lba) for lba in range(geo.total * 2)]
    for record in range(geo.total):
        track, sector = divmod(record, geo.spt)
        lba = track * 2 * geo.spt + sector
        data = logical[record * SECTOR:(record + 1) * SECTOR]
        if data != signature(record):
            physical[lba] = data
    return b"".join(physical)


def check_rw(path):
    with open(path, "rb") as handle:
        handle.seek(WRITTEN_SECTOR * SECTOR)
        data = handle.read(SECTOR)
    if data == written(WRITTEN_SECTOR):
        print("sector %d holds what the test wrote" % WRITTEN_SECTOR)
        return 0
    if data == signature(WRITTEN_SECTOR):
        print("sector %d is as generated: the write never reached the card"
              % WRITTEN_SECTOR)
    else:
        print("sector %d holds something else: %s" % (WRITTEN_SECTOR,
                                                      data[:16].hex()))
    return 1


def main():
    modes = ("ro", "rw", "ro-hd", "rw-hd", "ro-ss", "check-rw")
    if len(sys.argv) != 3 or sys.argv[1] not in modes:
        sys.exit(__doc__)
    if sys.argv[1] == "check-rw":
        sys.exit(check_rw(sys.argv[2]))
    mode, _, density = sys.argv[1].partition("-")
    image = build(mode, density or "dd")
    with open(sys.argv[2], "wb") as handle:
        handle.write(image)
    print("wrote %s (%s, %d bytes)" % (sys.argv[2], sys.argv[1].upper(),
                                     len(image)))


if __name__ == "__main__":
    main()
