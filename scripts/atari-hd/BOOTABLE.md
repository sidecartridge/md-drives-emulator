# Bootable images

Background and walkthrough for turning an `atari_hd.py` image into a
self-bootable disk on real Atari ST/STe hardware.

This document is **prerequisite-driven**: each format (AHDI / PPDRIVER /
HDDRIVER) needs a third-party driver binary that this tool does not
ship. The sections below cover where to obtain each driver, how to
verify it before passing it to the image builder, and how the various
epic-004 stories chain together.

> Status: this is the docs side of epic-004 stage 1 (driver
> acquisition). The actual image builder still produces non-bootable
> images today; the `--ahdi-driver` / `--ppdriver-binary` /
> `--hddriver-pbl` flags described here are pending stories
> 011 / 005 / 006 respectively. Acquire the binaries now so the
> later stories can proceed without a tool-chain detour.

---

## AHDI — ICD driver

AHDI (Atari Hard Disk Interface) is the original Atari hard-disk
partition format. To boot AHDI from real hardware you need both the
`$1234` boot checksum at the root sector (story 001) **and** a driver
in the boot partition's `AUTO/` folder so TOS finds a hard-disk
handler at startup (story 011).

The de-facto driver for AHDI bootability today is **ICD's hard-disk
driver**. In the ICD Pro 6.5.5 (`ICDP655A`) distribution it ships as
`ICDBOOT.PRG` (with a copy under the dist's `AUTO/` subfolder).
**On a real ICD-formatted boot disk the same bytes are stored as
`/ICDBOOT.SYS` at the root of the boot partition** — verified
byte-for-byte against a real ICD reference image: identical content,
filename renamed `.PRG` → `.SYS`, no `AUTO/` directory on the disk.

The `.SYS` extension is meaningful — it tells ICD's IPL stub at
sector 0 to load the file directly via a small FAT16 reader rather
than going through TOS's `AUTO/*.PRG` scan. Story 011's image
builder must write the driver at root with the `.SYS` name; an
`AUTO/`-folder placement (which my earlier draft of these docs
suggested) does **not** match how ICD's IPL actually finds the
driver.

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

### What's next

The AHDI image builder gains `--ahdi-driver=PATH`. Pass the
validated `ICDBOOT.PRG` and the resulting image:

1. Has the patched ICD boot sector at LBA 0
   (`scripts/atari-hd/assets/icd_boot_sector.bin` with the
   partition table and sigword patched in; see story 011).
2. Stores the driver bytes verbatim as `/ICDBOOT.SYS` at FAT
   cluster 2 of the boot partition, with `attr=0`.

Both pieces are required: the stock ICD IPL relies on
`ICDBOOT.SYS` being at cluster 2 so it can skip the FAT16
directory walk entirely.

### How story 011's bootable AHDI is built

The image-builder embeds ICD's actual sector-0 IPL (saved as
`scripts/atari-hd/assets/icd_boot_sector.bin`, SHA-256
`442d2b795b2490de18ebd89a1324154feedd51659f07a08871a01bec5ea4c245`)
and patches the geometry-dependent fields. The IPL relies on one
on-disk invariant — `ICDBOOT.SYS` at FAT cluster 2 — which the
writer maintains by writing the driver as the first file in a
freshly formatted boot partition.

Four-step recipe (full detail in story 011):

1. **Patch the boot sector**: load the asset, overwrite `hd_siz`
   (0x1C2..0x1C5), AHDI partition slots (0x1C6..0x1F5), BSL
   pointer (0x1FA..0x1FD); recompute the sigword (0x1FE..0x1FF)
   so the 256-BE-word-sum of the whole sector mod 0x10000
   equals `$1234`.
2. **Write driver bytes at cluster 2** of the boot partition's
   FAT16. Cluster 2 LBA =
   `firstLBA + resv + nfats*spfat + ceil(nroot*32 / bps)`.
3. **Patch FAT1 and FAT2** with the contiguous chain
   `2 → 3 → … → (1 + n_clusters) → 0xFFFF`.
4. **Write the root-directory entry**: name `"ICDBOOT "` + ext
   `"SYS"`, attr `0x00`, cluster low-word `0x0002`, size in
   bytes.

Reference images used to derive this recipe:
- `sd_card_icdpro.img` (14.21 MiB, SHA-256 `210dc0de…f892`):
  golden boot-payload reference. Sector 0 sums to `$1234`,
  contains a single 14.21 MB GEM boot partition with
  `/ICDBOOT.SYS` at cluster 2 (`attr=0`) plus the rest of the
  ICD Pro 6.5.5 utility set at later clusters (none required
  for boot).
- `atari_2gb_empty_ICD.img` (2 GiB, SHA-256 `33092198…ffdd`):
  multi-partition slot-table reference (GEM boot + XGM container
  + two direct BGM primaries; confirms ICD mixes XGM with direct
  primaries freely).

Neither image is in the repo (both untracked at repo root).

---

## PPDRIVER — Peter Putnik PPTOSDOS

Pending — see [story 009](epics/epic-004-real-hardware-compat/story-009-acquire-ppdriver-binary.md)
for the planned acquisition workflow and validator
(`tools/check_ppdriver_binary.py`).

## HDDRIVER — Uwe Seimet

Pending — see [story 010](epics/epic-004-real-hardware-compat/story-010-acquire-hddriver-binary.md)
for the planned acquisition workflow and validator
(`tools/check_hddriver_binary.py`). Note: HDDRIVER is **commercial**
(free demo + paid full version); licensing for that one is
unambiguous, no community-freeware ambiguity.

---

## Story chain (epic-004)

```
stage 1 (acquire)        stage 2 (checksum)    stage 3 (bootable)
─────────────────        ──────────────────    ─────────────────────
story 008  ICD ────────► story 001  $1234 ───► story 011  AHDI loader
story 009  PPDRIVER ──►                   ───► story 005  PPDRIVER loader
story 010  HDDRIVER ──► story 002  PBL ───►    story 006  FAT16 file-writer
                                               (used by 011 + 005 + 006)
```

Story 008 — this story — gives the AHDI track its acquisition
prerequisite. Stories 009 and 010 cover PPDRIVER and HDDRIVER.
