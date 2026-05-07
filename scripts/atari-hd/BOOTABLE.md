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

## HDDRIVER — Uwe Seimet (manual install, not self-bootable)

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
checksum), 002 (PPDRIVER 0x1BC adjust). Real remaining work in
the epic: story 012 (TUI surface) and 007 (parity harness).
