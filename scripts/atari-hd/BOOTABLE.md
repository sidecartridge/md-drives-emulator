# Bootable images

Background and walkthrough for turning an `atari_hd.py` image into a
self-bootable disk on real Atari ST/STe hardware.

Per-format status:

| Format | Self-bootable from this tool? | How |
|---|---|---|
| **AHDI** (ICD) | ✅ yes | `--ahdi-driver=PATH` (or B in TUI). Needs a user-supplied `ICDBOOT.PRG` (we don't bundle ICD). Boot partition capped at 15 MB. |
| **PPDRIVER** (Peter Putnik) | ✅ yes | `--ppdriver-bootable` (or B in TUI). Boot blob bundled in-repo (freeware). |
| **HDDRIVER** (Uwe Seimet) | ⚠️ experimental | Format byte-validated against real references but cold-boot from this tool's output isn't end-to-end verified. Manual install via HDDRUTIL.APP — see HDDRIVER section below. |

---

## AHDI — ICD driver

AHDI (Atari Hard Disk Interface) is the original Atari hard-disk
partition format. To boot AHDI from real hardware you need both the
`$1234` boot checksum at the root sector (story 001) **and** a driver
in the boot partition's `AUTO/` folder so TOS finds a hard-disk
handler at startup (story 011).

The de-facto driver for AHDI bootability today is **ICD's hard-disk
driver**. In the ICD Pro 6.5.5 (`ICDP655A`) distribution it ships as
`ICDBOOT.PRG` (with a copy under the dist's `AUTO/` subfolder). On
a real ICD-formatted boot disk the same bytes are stored as
`/ICDBOOT.SYS` at the root of the boot partition (extension renamed
`.PRG` → `.SYS`, no `AUTO/` directory). Our writer reproduces that
on-disk layout — see "How the AHDI cold-boot path actually works"
below for the full chain.

ICD Inc. is defunct; the Atari community treats the ICD driver as
freeware in practice, but this tool makes no licensing claim —
**you are responsible for ensuring your use of the binary complies
with whatever licence applies to the copy you obtain**.

### Where to obtain it

This repository does **NOT** redistribute the ICD driver. Sources to
look at, in rough order of preference:

1. **Atari community archives** — `atarimania.com`, `atari-forum.com`
   downloads, `dev-docs.atariforge.org`. Search for "ICD Pro 6.5.5"
   or "ICDBOOT.PRG"; the file is usually packaged inside a `.ZIP` or
   `.MSA` floppy image.

2. **Original ICD Pro distribution media** — if you have an authentic
   ICD Pro floppy set, extract `ICDBOOT.PRG` from the install disk.

3. **Reference disk images** — disks dumped from working Atari ST
   setups (e.g. the 1 GB raw dump that this project's PPDRIVER work
   was diffed against) sometimes carry an `ICDBOOT.PRG` in their
   boot partition's `AUTO/` folder. You can extract it directly off
   the FAT16 filesystem with any host-side FAT tool.

Do **not** pull the binary from a random "drivers" megapack with no
provenance — the helper below will catch obvious corruption but
can't tell a tampered binary from a legitimate one.

### Verify the binary

Once you have a candidate file, run the validator:

```
python scripts/atari-hd/tools/check_icd_driver.py path/to/ICDBOOT.PRG
```

The helper:

- Confirms the file starts with the Atari `.PRG` magic word `0x601A`
  (a 68000 `BRA.S` past the program header). ICD Pro's `ICDBOOT.PRG`
  has been a `.PRG`-format binary in every version we've inspected —
  if yours doesn't match, it's most likely a `.ZIP` you forgot to
  extract or an unrelated file.
- Prints the SHA-256 so you have a stable record of the exact bytes
  you'll be feeding to the image builder. Save this digest alongside
  the URL or media you sourced the file from; future runs and
  bug-reports can reference it.
- Exits 0 on success, 1 on any failure (size out of plausible range,
  unreadable file, wrong magic).

Stdlib-only — no `pip install` needed.

### Known-good SHA-256 values

| Driver / version           | Filename       | SHA-256                                                              | Notes                                              |
|----------------------------|----------------|----------------------------------------------------------------------|----------------------------------------------------|
| ICD Pro 6.5.5 (`ICDP655A`)  | `ICDBOOT.PRG`  | `2a0b911ab5f7fba8d641c2f61a4e460572cda7ede2ec12329ed7f020ba595cfc`    | 48502 bytes; reported version "ICDBOOT 6.5.5" per `VERSIONS` file in the same archive. |

The binary itself is **not** committed to this repo (see
`scripts/atari-hd/.gitignore`). If you obtain a copy locally, drop
it under `scripts/atari-hd/drivers/<name>/` for your own use; that
path is gitignored. Run the helper to confirm the SHA-256 matches
the row above before passing it to the (future) image-builder.

### Building a bootable AHDI image

```
python3 scripts/atari-hd/atari_hd.py \
    --ahdi-driver scripts/atari-hd/drivers/icdp655a/ICDBOOT.PRG
```

(The TUI exposes the same option: pick AHDI, press B, paste the
driver path on the prompt.)

Constraints:

- The boot partition (slot 0) must be **≤ 15 MB**. ICD's
  continuation IPL fails to recognize disks with a larger boot
  partition. The tool refuses to build with a clear error if you
  exceed this cap. Real ICDFMT-formatted reference disks always
  use 14–15 MB boot partitions; we follow the same convention.
- Slots 1+ are unconstrained by this — keep the bulk of your
  storage there as BGM partitions.

### How the AHDI cold-boot path actually works

The cold-boot chain involves **two** ICD-supplied IPLs that we
embed as in-repo assets and patch with the user's partition
table and driver bytes:

```
Atari ROM
   │  reads sector 0, checks 256-BE word-sum == $1234
   ▼
sector 0 IPL (450 bytes; assets/icd_boot_sector.bin)
   │  reads slot-0 first-LBA from the AHDI partition table at
   │  offset 0x1C6, then loads the boot partition's first sector
   │  and JUMPS into it.
   ▼
boot partition's first sector  (at LBA 2)
   │  byte 0..1 is `60 2c` (BRA.S +$2c) -- skips past the FAT16
   │  BPB header into ICD's continuation IPL embedded at 0x40+.
   │  The continuation IPL walks the FAT16 root for the 8.3 name
   │  "ICDBOOT SYS" (literally embedded in the BPB at offset 0x21
   │  as the lookup target), reads its cluster chain, applies
   │  .PRG-format relocations, and jumps to the loaded image.
   ▼
ICDBOOT registers as the HD handler, TOS continues to boot
```

The continuation IPL walks the directory by 8.3 name — it's not
hardcoded to cluster 2. We still place `ICDBOOT.SYS` at cluster 2
to match what real ICDFMT writes, but that's byte-parity, not a
load-mechanism requirement.

What the writer does:

1. **Stamp the AHDI root sector** (LBA 0) — load
   `assets/icd_boot_sector.bin`, patch in our `hd_siz`, AHDI
   partition table, BSL pointer, and the sigword adjust at
   `0x1FE` so the sector sums to `$1234`.
2. **Stamp the boot partition's first sector** (LBA 2) — load
   `assets/icd_bpb.bin`, splice in our BPB geometry header
   (bytes `0x0B..0x10` + `0x13..0x17`), keep the asset's
   `nroot=256` and the embedded continuation IPL bytes verbatim.
3. **Place the driver** — write `ICDBOOT.PRG` bytes at FAT
   cluster 2 of the boot partition (renamed `ICDBOOT.SYS` in the
   directory entry, attr `0x20`). Patch FAT1/FAT2 with the
   contiguous cluster chain.

### Asset SHA-256s

| Asset | SHA-256 | Source |
|---|---|---|
| `assets/icd_boot_sector.bin` (512 B) | `442d2b795b2490de18ebd89a1324154feedd51659f07a08871a01bec5ea4c245` | sector 0 of `sd_card_icdpro.img` |
| `assets/icd_bpb.bin` (512 B) | `69a7052483caaddb38059b014f5c751450327f8b25f0e143123e926758d0e3a0` | LBA 2 of `TEST16MB_1PART_ORIGINAL.img` |

---

## PPDRIVER — Peter Putnik PPTOSDOS

Bootable PPDRIVER images are produced via a single CLI flag:

```
python3 scripts/atari-hd/atari_hd.py --ppdriver-bootable
```

No path argument — PPDRIVER's distribution is explicit-freeware per
Peter Putnik's terms, so we bundle the boot blob in-repo at
`scripts/atari-hd/assets/pp_boot_blob.bin` (15 sectors / 7,680 bytes,
SHA-256 `50dd9ee8…6b9a`, extracted from a known-good PPDRIVER 1 GB
reference). Cold-boot validated on real Atari hardware.

The blob covers everything: sector 0 IPL + secondary IPL at LBA 1 +
the bundled `.PRG`-format driver at LBA 2..14. PPDRIVER's boot
mechanism lives entirely in the pre-partition gap, not as a file
inside the FAT16, so no `--ppdriver-driver=PATH` flag is needed.

## HDDRIVER — Uwe Seimet (experimental)

> ⚠️ **HDDRIVER support is experimental.** The dual-BPB byte-
> fidelity work (story 003) is verified against real PPDRIVER /
> HDDRIVER reference dumps, but **end-to-end real-hardware boot
> from this tool's output has not been validated**. The image
> needs HDDRUTIL.APP (from the HDDRIVER distribution) to install
> the per-disk driver on first boot. Treat as a compatibility
> fallback, not a turnkey bootable path. Bug reports welcome.

**This tool does not produce self-bootable HDDRIVER images.** Use
the standard HDDRIVER distribution and HDDRUTIL.APP to install on
first boot.

### Why not self-bootable?

We tried the same recipe that worked for AHDI / PPDRIVER (extract
sector 0 + driver bytes from a working reference, embed them, patch
the partition table at build time) and it failed cold-boot on real
hardware: the disk wasn't recognized.

Investigation showed why: HDDRIVER's on-disk `HDDRIVER.SYS` is
**generated per-disk by HDDRUTIL.APP**, not a static binary. Two
copies we obtained had completely different sizes (24,874 B and
1,024 B) and SHAs. The dependency on the source disk is encoded
indirectly (likely a checksum of the boot sector, hardware params,
or a license fingerprint — HDDRIVER is commercial software). Without
the author's documentation, reverse-engineering it isn't realistic,
and shipping anyone's per-disk `HDDRIVER.SYS` would be both legally
questionable and functionally wrong.

### What we ship instead

The existing HDDRIVER format path (`atari_hd.py` with format
`HDDRIVER`) produces non-bootable HDDRIVER-compatible images. The
AHDI partition table, dual BPB, and `PPGDODBC` OEM stamp match real
HDDRIVER reference disks byte-for-byte (validated as part of story
003). HDDRUTIL.APP recognizes them and can install onto them
normally.

### Manual install workflow

1. Build a non-bootable HDDRIVER image with this tool (no
   bootable-mode flag).
2. Obtain HDDRIVER from the vendor (commercial software with a free
   demo; users source it themselves — we don't bundle).
3. Boot the Atari from any working HD or floppy that has HDDRIVER
   installed.
4. Mount our generated image (via SidecarTridge, second hard-disk
   bus, or any other way the Atari can see the bytes).
5. Run **HDDRUTIL.APP** from the HDDRIVER distribution.
6. Choose "Install driver" — HDDRUTIL writes the per-disk
   `HDDRIVER.SYS` plus its IPL into sector 0 of the target image.
7. The image is self-bootable from then on.

### Why AUTO/HDDRIVER.PRG is *not* a workaround

A common reflex is "just put HDDRIVER.PRG in the boot partition's
`AUTO/` folder and let TOS load it." That's chicken-and-egg: TOS
can't scan `AUTO/` on a hard disk until a hard-disk handler is
already registered. The handler is what `HDDRIVER.SYS` would
provide. Without it, TOS never reaches AUTO. Combining it with
ICD's IPL doesn't help either — only one HD handler can be
registered, and ICD claims the slot first.

---

## Story chain (epic-004)

```
stage 1 (acquire)        stage 3 (bootable)
─────────────────        ─────────────────────
story 008  ICD ────────► story 011  AHDI bootable      ✅
                          (ICDBOOT.PRG embedded as
                           /ICDBOOT.SYS at cluster 2)
story 009  PPDRIVER ──►   moot — story 005 bundles
                          assets/pp_boot_blob.bin in-repo
story 005   ────────►    PPDRIVER bootable             ✅
                          (--ppdriver-bootable, no PATH)
story 010   ────────►    folded into 006
story 006   ────────►    HDDRIVER manual-install docs  (this section)
```

Effectively-done by-side-effect of 005/011: stories 001 (AHDI
checksum), 002 (PPDRIVER 0x1BC adjust). All epic-004 stories
delivered.

---

## Real-hardware parity harness (developer aid)

`scripts/atari-hd/tests/realhw_parity.py` byte-diffs our writer's
output against user-supplied reference images, applying per-format
masks for known per-image variations (random disk signatures,
auto-aligned partition geometries, FAT[0/1] media bytes, dir-entry
timestamps, unallocated data area). Stdlib only. Skips cleanly when
no references are supplied.

```
python scripts/atari-hd/tests/realhw_parity.py \
    [--ahdi-reference PATH      [--ahdi-driver PATH]] \
    [--ppdriver-reference PATH] \
    [--hddriver-reference PATH] \
    [--out-dir DIR]
```

What's actually byte-checked per format:

- **AHDI**: sector 0 IPL (450 bytes minus the per-image 0x1C1
  byte), AHDI partition table, BPB-with-continuation-IPL,
  ICDBOOT.SYS contents in cluster 2, FAT chain entries.
- **PPDRIVER**: sector 0 IPL (450 bytes); LBA 1..14 driver blob
  (story 005 byte-fidelity claim). Everything past LBA 14 is
  masked because our writer's spfat / cluster auto-alignment
  produces different partition geometry than ICDFMT.
- **HDDRIVER** (non-bootable): partition layout + dual-BPB byte
  parity (story 003 claim).

Exit 0 iff zero unexpected diffs across all supplied references.
Use as a regression gate when changing the byte-level writers.
