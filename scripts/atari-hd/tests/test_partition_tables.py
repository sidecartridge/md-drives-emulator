"""Partition-table writer tests for atari_hd.py.

Builds minimal in-memory ImagePlan objects and exercises:
  - build_root_sector_ahdi (1..4 primaries; 5+ via XGM chain)
  - build_root_sector_ppdriver (MBR primaries + extended container)
  - build_root_sector_hddriver (1 primary + AHDI overlap marker; +ext)
  - build_xgm_descriptor_sector (XGM sub-descriptors)
  - build_ebr_sector (MBR extended-chain EBRs)

Tests construct sectors in memory only -- no temp files. CHS bytes are
deliberately not asserted (they're a derivative of start_lba via
lba_to_chs and would couple this test to that helper); we lock LBA,
size, type, ident, flags, and the 0x55AA signature.
"""

import unittest

from _support import (
    atari_hd,
    AHDI_SLOT_OFFSETS,
    MBR_SLOT_OFFSETS,
    parse_ahdi_root,
    parse_ahdi_entry,
    parse_mbr_root,
    parse_ebr,
    parse_xgm_descriptor,
)


# AHDI flag combinations
AHDI_EXISTS = 0x01
AHDI_BOOT = 0x80
AHDI_EXISTS_BOOT = 0x81

# MBR partition types
MBR_TYPE_FAT16 = 0x06
MBR_TYPE_EXTENDED = 0x0F


def _make_partition(name, size_mb, size_sectors, start_lba,
                    ebr_lba=0, dos_bps=512, dos_spc=2, dos_res=1,
                    tos_bps=0):
    """Produce a fully-populated Partition. The writers only consume the
    derived fields, so we set them all explicitly rather than running
    plan_image()."""
    return atari_hd.Partition(
        name=name, size_mb=size_mb,
        size_sectors=size_sectors, start_lba=start_lba, ebr_lba=ebr_lba,
        dos_bps=dos_bps, dos_spc=dos_spc, dos_res=dos_res, tos_bps=tos_bps,
    )


def _make_plan(format_id, partitions, primary_count, has_extended=False,
               strict_tos=False):
    plan = atari_hd.ImagePlan(
        format_id=format_id, image_path="<test>", image_mb=0,
        partitions=partitions, strict_tos=strict_tos,
    )
    plan.primary_count = primary_count
    plan.has_extended = has_extended
    return plan


def _slot_msg(label, slot_index, raw):
    return (f"\n  {label} slot {slot_index} raw={raw.hex()}")


class TestAhdiRoot(unittest.TestCase):
    def test_primary_count_sweep_1_through_4(self):
        # Per spec: cover N in {1, 2, 3, 4}. Same-size 16 MB GEM partitions
        # so the only thing that changes is which slots are populated.
        for n in (1, 2, 3, 4):
            with self.subTest(primary_count=n):
                size_sectors = 32768  # 16 MB
                partitions = [
                    _make_partition(f"P{i + 1}", size_mb=16,
                                    size_sectors=size_sectors,
                                    start_lba=2 + i * size_sectors)
                    for i in range(n)
                ]
                plan = _make_plan(atari_hd.FORMAT_AHDI, partitions,
                                  primary_count=n)
                sec = atari_hd.build_root_sector_ahdi(plan)
                slots = parse_ahdi_root(sec)

                for i, part in enumerate(partitions):
                    raw = sec[AHDI_SLOT_OFFSETS[i]:AHDI_SLOT_OFFSETS[i] + 12]
                    expected_flag = AHDI_EXISTS_BOOT if i == 0 else AHDI_EXISTS
                    self.assertEqual(slots[i]["flag"], expected_flag,
                                     f"N={n} slot {i} flag"
                                     f"{_slot_msg('AHDI', i, raw)}")
                    self.assertEqual(slots[i]["ident"], b"GEM",
                                     f"N={n} slot {i} ident"
                                     f"{_slot_msg('AHDI', i, raw)}")
                    self.assertEqual(slots[i]["start_lba"], part.start_lba,
                                     f"N={n} slot {i} start_lba"
                                     f"{_slot_msg('AHDI', i, raw)}")
                    self.assertEqual(slots[i]["size_sectors"],
                                     part.size_sectors,
                                     f"N={n} slot {i} size_sectors"
                                     f"{_slot_msg('AHDI', i, raw)}")
                # Slots beyond `n` must be empty.
                for i in range(n, 4):
                    self.assertEqual(slots[i]["flag"], 0,
                                     f"N={n} slot {i} should be empty")
                    self.assertEqual(slots[i]["ident"], b"\x00\x00\x00")
                    self.assertEqual(slots[i]["start_lba"], 0)
                    self.assertEqual(slots[i]["size_sectors"], 0)

                # Pure AHDI: no MBR signature regardless of N.
                self.assertEqual(sec[510:512], b"\x00\x00",
                                 f"N={n}: AHDI root must not carry "
                                 "the 0x55AA MBR signature")

    def test_four_primaries_mixed_gem_bgm(self):
        # Slot 0 is forced to GEM regardless of size; slots 1+ pick GEM/BGM
        # from the size threshold (32 MB by default).
        partitions = [
            _make_partition("BOOT", size_mb=16, size_sectors=32768,  start_lba=2),
            _make_partition("S1",   size_mb=32, size_sectors=65536,  start_lba=32770),
            _make_partition("S2",   size_mb=64, size_sectors=131072, start_lba=98306),
            _make_partition("S3",   size_mb=128, size_sectors=262144, start_lba=229378),
        ]
        plan = _make_plan(atari_hd.FORMAT_AHDI, partitions, primary_count=4)
        sec = atari_hd.build_root_sector_ahdi(plan)
        slots = parse_ahdi_root(sec)

        expected_idents = [b"GEM", b"GEM", b"BGM", b"BGM"]
        for i, (part, want_ident) in enumerate(zip(partitions, expected_idents)):
            raw = sec[AHDI_SLOT_OFFSETS[i]:AHDI_SLOT_OFFSETS[i] + 12]
            with self.subTest(slot=i, partition=part.name):
                want_flag = AHDI_EXISTS_BOOT if i == 0 else AHDI_EXISTS
                self.assertEqual(slots[i]["flag"], want_flag,
                                 f"flag{_slot_msg('AHDI', i, raw)}")
                self.assertEqual(slots[i]["ident"], want_ident,
                                 f"ident{_slot_msg('AHDI', i, raw)}")
                self.assertEqual(slots[i]["start_lba"], part.start_lba,
                                 f"start_lba{_slot_msg('AHDI', i, raw)}")
                self.assertEqual(slots[i]["size_sectors"], part.size_sectors,
                                 f"size_sectors{_slot_msg('AHDI', i, raw)}")

        self.assertEqual(sec[510:512], b"\x00\x00")

    def test_xgm_chain_five_partitions(self):
        # 3 primaries + slot-3 XGM link covering a chain of 2 logicals.
        # Chain layout: chain_base at LBA 1000; descriptor 0 at 1000, logical 0
        # at 1001 (size 1024); descriptor 1 at 2025, logical 1 at 2026
        # (size 2048).
        primaries = [
            _make_partition("P1", 16, 32768, start_lba=2),
            _make_partition("P2", 16, 32768, start_lba=32770),
            _make_partition("P3", 16, 32768, start_lba=65538),
        ]
        log0 = _make_partition("L1", 1, 1024, start_lba=1001, ebr_lba=1000)
        log1 = _make_partition("L2", 1, 2048, start_lba=2026, ebr_lba=2025)
        plan = _make_plan(atari_hd.FORMAT_AHDI,
                          primaries + [log0, log1],
                          primary_count=3, has_extended=True)

        sec = atari_hd.build_root_sector_ahdi(plan)
        slots = parse_ahdi_root(sec)

        # Slot 3 is the XGM link.
        raw3 = sec[AHDI_SLOT_OFFSETS[3]:AHDI_SLOT_OFFSETS[3] + 12]
        self.assertEqual(slots[3]["flag"], AHDI_EXISTS,
                         f"XGM flag{_slot_msg('AHDI', 3, raw3)}")
        self.assertEqual(slots[3]["ident"], b"XGM",
                         f"XGM ident{_slot_msg('AHDI', 3, raw3)}")
        # Container: from the first sub-descriptor (1000) through the end
        # of the last logical (2026 + 2048 = 4074).
        self.assertEqual(slots[3]["start_lba"], 1000)
        self.assertEqual(slots[3]["size_sectors"], 4074 - 1000)

        # Now build the descriptors themselves and walk the chain.
        # Descriptor 0 lives at LBA 1000; the next descriptor is at 2025.
        chain_base = 1000
        desc0 = atari_hd.build_xgm_descriptor_sector(
            logical_abs_start=log0.start_lba,
            logical_size=log0.size_sectors,
            logical_ident=b"BGM",
            desc_abs_lba=log0.ebr_lba,
            xgm_base_abs_lba=chain_base,
            next_desc_abs_lba=log1.ebr_lba,
            next_chain_size=(log1.start_lba + log1.size_sectors) - log1.ebr_lba,
        )
        d0 = parse_xgm_descriptor(desc0)
        self.assertEqual(d0["logical"]["ident"], b"BGM")
        # Logical's start is RELATIVE to the descriptor's own LBA.
        self.assertEqual(d0["logical"]["start_lba"],
                         log0.start_lba - log0.ebr_lba)
        self.assertEqual(d0["logical"]["size_sectors"], log0.size_sectors)
        # XGM link's start is RELATIVE to the chain base.
        self.assertEqual(d0["link"]["ident"], b"XGM")
        self.assertEqual(d0["link"]["start_lba"], log1.ebr_lba - chain_base)

        # Descriptor 1: end of chain, no link.
        desc1 = atari_hd.build_xgm_descriptor_sector(
            logical_abs_start=log1.start_lba,
            logical_size=log1.size_sectors,
            logical_ident=b"BGM",
            desc_abs_lba=log1.ebr_lba,
            xgm_base_abs_lba=chain_base,
            next_desc_abs_lba=None,
            next_chain_size=0,
        )
        d1 = parse_xgm_descriptor(desc1)
        self.assertEqual(d1["logical"]["start_lba"],
                         log1.start_lba - log1.ebr_lba)
        self.assertEqual(d1["logical"]["size_sectors"], log1.size_sectors)
        # End of chain: link slot is empty.
        self.assertEqual(d1["link"]["flag"], 0)
        self.assertEqual(d1["link"]["ident"], b"\x00\x00\x00")


class TestPpdriverRoot(unittest.TestCase):
    def test_one_primary(self):
        p = _make_partition("DATA", size_mb=128, size_sectors=262144,
                            start_lba=1)
        plan = _make_plan(atari_hd.FORMAT_PPDRIVER, [p], primary_count=1)
        sec = atari_hd.build_root_sector_ppdriver(plan)
        mbr = parse_mbr_root(sec)

        raw0 = sec[MBR_SLOT_OFFSETS[0]:MBR_SLOT_OFFSETS[0] + 16]
        self.assertEqual(mbr["slots"][0]["boot"], 0x80,
                         f"P0 boot{_slot_msg('MBR', 0, raw0)}")
        self.assertEqual(mbr["slots"][0]["part_type"], MBR_TYPE_FAT16,
                         f"P0 type{_slot_msg('MBR', 0, raw0)}")
        self.assertEqual(mbr["slots"][0]["rel_start_lba"], 1,
                         f"P0 start{_slot_msg('MBR', 0, raw0)}")
        self.assertEqual(mbr["slots"][0]["sector_count"], 262144,
                         f"P0 size{_slot_msg('MBR', 0, raw0)}")

        for i in (1, 2, 3):
            with self.subTest(slot=i):
                self.assertEqual(mbr["slots"][i]["part_type"], 0,
                                 f"slot {i} should be unused")
                self.assertEqual(mbr["slots"][i]["sector_count"], 0)

        self.assertEqual(mbr["signature"], b"\x55\xAA")

    def test_four_primaries(self):
        partitions = [
            _make_partition("P1", 64, 131072,  start_lba=1),
            _make_partition("P2", 64, 131072,  start_lba=131073),
            _make_partition("P3", 64, 131072,  start_lba=262145),
            _make_partition("P4", 64, 131072,  start_lba=393217),
        ]
        plan = _make_plan(atari_hd.FORMAT_PPDRIVER, partitions,
                          primary_count=4)
        sec = atari_hd.build_root_sector_ppdriver(plan)
        mbr = parse_mbr_root(sec)

        for i, part in enumerate(partitions):
            raw = sec[MBR_SLOT_OFFSETS[i]:MBR_SLOT_OFFSETS[i] + 16]
            with self.subTest(slot=i, partition=part.name):
                expected_boot = 0x80 if i == 0 else 0x00
                self.assertEqual(mbr["slots"][i]["boot"], expected_boot,
                                 f"boot{_slot_msg('MBR', i, raw)}")
                self.assertEqual(mbr["slots"][i]["part_type"],
                                 MBR_TYPE_FAT16,
                                 f"type{_slot_msg('MBR', i, raw)}")
                self.assertEqual(mbr["slots"][i]["rel_start_lba"],
                                 part.start_lba,
                                 f"start{_slot_msg('MBR', i, raw)}")
                self.assertEqual(mbr["slots"][i]["sector_count"],
                                 part.size_sectors,
                                 f"size{_slot_msg('MBR', i, raw)}")
        self.assertEqual(mbr["signature"], b"\x55\xAA")

    def test_extended_chain_5_partitions(self):
        # 3 primaries + extended container in slot 3 covering 2 logicals.
        primaries = [
            _make_partition("P1", 64, 131072, start_lba=1),
            _make_partition("P2", 64, 131072, start_lba=131073),
            _make_partition("P3", 64, 131072, start_lba=262145),
        ]
        log0 = _make_partition("L1", 32, 65536, start_lba=393218, ebr_lba=393217)
        log1 = _make_partition("L2", 32, 65536, start_lba=458755, ebr_lba=458754)
        plan = _make_plan(atari_hd.FORMAT_PPDRIVER,
                          primaries + [log0, log1],
                          primary_count=3, has_extended=True)
        sec = atari_hd.build_root_sector_ppdriver(plan)
        mbr = parse_mbr_root(sec)

        # Slot 3 is the extended container.
        raw3 = sec[MBR_SLOT_OFFSETS[3]:MBR_SLOT_OFFSETS[3] + 16]
        ext = mbr["slots"][3]
        self.assertEqual(ext["part_type"], MBR_TYPE_EXTENDED,
                         f"ext type{_slot_msg('MBR', 3, raw3)}")
        # Container spans from the first EBR LBA through the end of the
        # last logical.
        ext_start = log0.ebr_lba
        ext_end = log1.start_lba + log1.size_sectors
        self.assertEqual(ext["rel_start_lba"], ext_start)
        self.assertEqual(ext["sector_count"], ext_end - ext_start)
        self.assertEqual(mbr["signature"], b"\x55\xAA")

        # Build EBR sectors and walk the chain.
        ext_base = log0.ebr_lba
        ebr0 = atari_hd.build_ebr_sector(
            logical_abs_start=log0.start_lba,
            logical_size=log0.size_sectors,
            ebr_abs_lba=log0.ebr_lba,
            ext_base_abs_lba=ext_base,
            next_ebr_abs_lba=log1.ebr_lba,
            next_chain_size=(log1.start_lba + log1.size_sectors) - log1.ebr_lba,
        )
        e0 = parse_ebr(ebr0)
        # EBR slot 0: the logical, start RELATIVE to the EBR.
        self.assertEqual(e0["slots"][0]["part_type"], MBR_TYPE_FAT16)
        self.assertEqual(e0["slots"][0]["rel_start_lba"],
                         log0.start_lba - log0.ebr_lba)
        self.assertEqual(e0["slots"][0]["sector_count"], log0.size_sectors)
        # EBR slot 1: link to next EBR, start RELATIVE to the chain base.
        self.assertEqual(e0["slots"][1]["part_type"], MBR_TYPE_EXTENDED)
        self.assertEqual(e0["slots"][1]["rel_start_lba"],
                         log1.ebr_lba - ext_base)
        self.assertEqual(e0["signature"], b"\x55\xAA")

        # Last EBR: no next link.
        ebr1 = atari_hd.build_ebr_sector(
            logical_abs_start=log1.start_lba,
            logical_size=log1.size_sectors,
            ebr_abs_lba=log1.ebr_lba,
            ext_base_abs_lba=ext_base,
            next_ebr_abs_lba=None,
            next_chain_size=0,
        )
        e1 = parse_ebr(ebr1)
        self.assertEqual(e1["slots"][0]["rel_start_lba"],
                         log1.start_lba - log1.ebr_lba)
        # Chain ends: slot 1 cleared.
        self.assertEqual(e1["slots"][1]["part_type"], 0)
        self.assertEqual(e1["slots"][1]["rel_start_lba"], 0)
        self.assertEqual(e1["slots"][1]["sector_count"], 0)


class TestHddriverRoot(unittest.TestCase):
    def test_one_primary_with_overlap_marker(self):
        p = _make_partition("DATA", size_mb=128, size_sectors=262144,
                            start_lba=1)
        plan = _make_plan(atari_hd.FORMAT_HDDRIVER, [p], primary_count=1)
        sec = atari_hd.build_root_sector_hddriver(plan)
        mbr = parse_mbr_root(sec)

        # MBR P0: standard FAT16 entry.
        self.assertEqual(mbr["slots"][0]["boot"], 0x80)
        self.assertEqual(mbr["slots"][0]["part_type"], MBR_TYPE_FAT16)
        self.assertEqual(mbr["slots"][0]["rel_start_lba"], 1)
        self.assertEqual(mbr["slots"][0]["sector_count"], 262144)

        # MBR P1: empty (no extended).
        self.assertEqual(mbr["slots"][1]["part_type"], 0)
        self.assertEqual(mbr["slots"][1]["sector_count"], 0)

        # AHDI overlap marker at slot 2 (0x1DE). The TOS view sees the
        # primary partition starting one sector later (TOS BPB lives at
        # firstLBA + 1 in the HDDRIVER convention).
        ahdi_marker = parse_ahdi_entry(sec, 0x1DE)
        raw_marker = sec[0x1DE:0x1DE + 12]
        self.assertEqual(ahdi_marker["flag"], AHDI_EXISTS_BOOT,
                         f"AHDI marker flag{_slot_msg('AHDI@0x1DE', 2, raw_marker)}")
        self.assertEqual(ahdi_marker["start_lba"], 2,
                         f"AHDI marker start{_slot_msg('AHDI@0x1DE', 2, raw_marker)}")
        self.assertEqual(ahdi_marker["size_sectors"], 262143,
                         f"AHDI marker size{_slot_msg('AHDI@0x1DE', 2, raw_marker)}")

        self.assertEqual(mbr["signature"], b"\x55\xAA")

    def test_extended_chain(self):
        primary = _make_partition("DATA", 64, 131072, start_lba=1)
        log0 = _make_partition("L1", 32, 65536, start_lba=131074, ebr_lba=131073)
        plan = _make_plan(atari_hd.FORMAT_HDDRIVER, [primary, log0],
                          primary_count=1, has_extended=True)
        sec = atari_hd.build_root_sector_hddriver(plan)
        mbr = parse_mbr_root(sec)

        # P0 unchanged, P1 = extended container.
        self.assertEqual(mbr["slots"][0]["part_type"], MBR_TYPE_FAT16)
        self.assertEqual(mbr["slots"][1]["part_type"], MBR_TYPE_EXTENDED)
        self.assertEqual(mbr["slots"][1]["rel_start_lba"], log0.ebr_lba)
        self.assertEqual(mbr["slots"][1]["sector_count"],
                         (log0.start_lba + log0.size_sectors) - log0.ebr_lba)

        # AHDI overlap marker still at 0x1DE for the primary.
        marker = parse_ahdi_entry(sec, 0x1DE)
        self.assertEqual(marker["flag"], AHDI_EXISTS_BOOT)
        self.assertEqual(marker["start_lba"], 2)
        self.assertEqual(marker["size_sectors"], 131071)

        self.assertEqual(mbr["signature"], b"\x55\xAA")


if __name__ == "__main__":
    unittest.main()
