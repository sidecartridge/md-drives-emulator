# atari-hd

Welcome to **atari-hd** — a general-purpose Atari ST hard-disk
image builder with both a terminal UI (the default) and a
scriptable command-line. The output is a raw `.img` you can drop
straight into any tool that reads a block image:

- Atari ST emulators — Hatari, STeem.
- Hardware bridges — SidecarTridge Multi-device, ACSI2STM,
  SatanDisk.
- Direct media — write the image to a microSD / SD / CompactFlash
  card with `dd` (or the Windows equivalent) and plug it into a
  real SCSI / IDE / CF adapter on the Atari side. There's a
  step-by-step at the end of this README:
  [Writing the image to a physical device](#writing-the-image-to-a-physical-device).

Three on-disk formats are supported:

| Format | Self-bootable from this tool? | How |
|---|---|---|
| **AHDI** (ICD driver) | ✅ yes | Embed your `ICDBOOT.PRG`. Boot partition capped at 15 MB. |
| **PPDRIVER** (Peter Putnik) | ✅ yes | Bundled boot blob from a known-good reference. No extra binary required. |
| **HDDRIVER** (Uwe Seimet) | ⚠️ experimental | Build the format here, install the driver via `HDDRUTIL.APP` on first boot. |

See [`BOOTABLE.md`](BOOTABLE.md) for the full per-format bootable-image
guide; this README covers everyday usage of the tool itself.

**Requirements:** Python 3.10+ on your PATH. Stdlib only — no `pip
install`, no external binaries. Runs on Windows, macOS, and Linux.

## Why this tool exists

If you've ever tried to set up a hard disk on a real Atari ST, you
already know the story. The tutorials out there are scattered,
incomplete, or aimed at people who already know which TOS-side
BPB sector size goes with which driver. The drivers themselves
live in different corners of the internet — some freeware, some
commercial, some abandoned but still required. Even the partition
tables differ between AHDI, PPDRIVER, and HDDRIVER, and getting any
one of them wrong leaves you with a disk the Atari simply won't
recognise.

`atari-hd` exists to cut through all of that. One command. A few
prompts. A working `.img` you can dump on a microSD card and boot.
The TUI walks you through size, format, partitions, and bootable
mode in plain language; the CLI gives you the same flow in a
single line for scripted runs. The byte-level output is validated
against real ICDFMT- and PPTOSDOS-formatted reference disks, so
what comes out of the tool matches what a real Atari setup tool
would have written — only without the multi-evening rabbit hole.

The goal is **as simple as possible without hiding what's
underneath**. If you want to read the on-disk format details,
they're in the [Internals](#internals) section below; if you just
want a working disk, you don't have to.

## Installing

### One-liner (recommended)

**macOS / Linux:**

```
curl -fsSL https://raw.githubusercontent.com/sidecartridge/md-drives-emulator/main/scripts/atari-hd/install.sh | sh
```

**Windows (PowerShell):**

```
irm https://raw.githubusercontent.com/sidecartridge/md-drives-emulator/main/scripts/atari-hd/install.ps1 | iex
```

What the installer does:

1. Downloads the current `main`-branch tarball of this repository.
2. Extracts only `scripts/atari-hd/` into a stable location:
   - macOS / Linux: `~/.local/share/atari-hd/`.
   - Windows: `%LOCALAPPDATA%\atari-hd\`.
3. Drops a launcher shim onto PATH so `atari-hd` works from any
   shell:
   - macOS / Linux: `~/.local/bin/atari-hd` → the package's launcher.
   - Windows: `%LOCALAPPDATA%\Microsoft\WindowsApps\atari-hd.cmd`
     (this directory is on every Windows 10+ user's default PATH).
4. If `~/.local/bin` isn't on your PATH, prints exact add-to-PATH
   instructions for your shell — the installer never edits rc files
   for you.

The installer is **idempotent**: re-runs fetch the latest `main`
snapshot and replace the install in place. The version reported by
`atari-hd --version` and shown in the TUI header reflects whatever
`scripts/atari-hd/version.txt` was at the time you installed.

> ℹ️ The trust boundary here is HTTPS to `github.com`. There's no
> tagged-release / signed-checksum step today — we install whatever's
> on `main`. If you'd rather pin to a specific branch, tag, or commit
> SHA, use `--ref=` on POSIX, or set `$env:ATARI_HD_REF` *before*
> the `irm | iex` line on Windows (PowerShell can't pass named
> parameters into a piped script). The tool may move to a dedicated
> repo later, at which point the URLs here will be updated.

No `sudo` / admin needed for the default install root.

### Pinned ref or custom prefix

`--ref=` (POSIX) and `$env:ATARI_HD_REF` (Windows) accept any git
ref the tarball endpoint understands: a branch name, a tag, or a
commit SHA.

```
# macOS / Linux -- env vars on the sh side of the pipe (the var
# assignment must precede the command that reads it; on the curl
# side it would only set vars for curl, not for sh)
curl -fsSL https://raw.githubusercontent.com/sidecartridge/md-drives-emulator/main/scripts/atari-hd/install.sh \
    | ATARI_HD_REF=v0.1.0 ATARI_HD_PREFIX=/opt sh

# ...or download install.sh first and pass flags
curl -fsSLo install.sh https://raw.githubusercontent.com/sidecartridge/md-drives-emulator/main/scripts/atari-hd/install.sh
sh install.sh --ref=v0.1.0 --prefix=/opt
```

```powershell
# Windows -- set env vars BEFORE the irm|iex line
$env:ATARI_HD_REF    = 'v0.1.0'
$env:ATARI_HD_PREFIX = 'C:\Tools\atari-hd'
irm https://raw.githubusercontent.com/sidecartridge/md-drives-emulator/main/scripts/atari-hd/install.ps1 | iex
```

### Manual install (contributors and offline use)

Prefer to run from a clone? Clone the repo and invoke the script
directly. No installer, no PATH shim — handy when you're hacking
on the writer or working without internet access:

```
git clone https://github.com/sidecartridge/md-drives-emulator.git
cd md-drives-emulator
python3 scripts/atari-hd/atari_hd.py
```

### Uninstall

The same installer scripts handle uninstall via `--uninstall`
(POSIX) / `-Uninstall` (Windows). They remove only the package
dir and the launcher shim — your generated `.img` files, the
`drivers/` tree, and anything else under your prefix stay
exactly where they are.

**macOS / Linux:**

```
# Pipe mode -- ATARI_HD_YES=1 acknowledges the removal because
# pipe mode can't show a y/N prompt
curl -fsSL https://raw.githubusercontent.com/sidecartridge/md-drives-emulator/main/scripts/atari-hd/install.sh \
    | ATARI_HD_UNINSTALL=1 ATARI_HD_YES=1 sh

# ...or run the local copy in your install (interactive y/N prompt)
sh ~/.local/share/atari-hd/install.sh --uninstall
```

If you installed with a custom prefix, pass it through:

```
curl -fsSL https://...install.sh \
    | ATARI_HD_UNINSTALL=1 ATARI_HD_YES=1 ATARI_HD_PREFIX=/opt sh
```

**Windows (PowerShell):**

```powershell
# Pipe mode (irm | iex) -- env vars set BEFORE the pipe
$env:ATARI_HD_UNINSTALL = '1'
$env:ATARI_HD_YES       = '1'
irm https://raw.githubusercontent.com/sidecartridge/md-drives-emulator/main/scripts/atari-hd/install.ps1 | iex

# ...or run the local copy with -Uninstall (interactive prompt)
& "$env:LOCALAPPDATA\atari-hd\install.ps1" -Uninstall
```

**Manual fallback** if you just want to delete the files yourself:

```
# macOS / Linux
rm -rf ~/.local/share/atari-hd ~/.local/bin/atari-hd

# Windows (PowerShell)
Remove-Item -Recurse $env:LOCALAPPDATA\atari-hd
Remove-Item $env:LOCALAPPDATA\Microsoft\WindowsApps\atari-hd.cmd
```

The manual paths above assume the default prefix. If you
installed elsewhere, replace `~/.local` (POSIX) or
`$env:LOCALAPPDATA\atari-hd` (Windows) with the value you used.

---

## Quick start (TUI)

The TUI is what you'll see when you run the tool from a regular
terminal — it's the default whenever both `stdin` and `stdout` are
a TTY. Once you've installed via the one-liner above:

```
atari-hd
```

If you'd rather run from a clone (the manual-install path), the
equivalent command is:

```
python3 scripts/atari-hd/atari_hd.py
```

The rest of this guide uses `atari-hd` for brevity. Substitute the
clone-relative form if you haven't installed.

### Landing screen

```
atari-hd image creator  (C) 2026 - GOODDATA LABS SL  v0.1.0           (no image)
────────────────────────────────────────────────────────────────────────────────
    No image selected. Press N to create one or L to load an existing image.
────────────────────────────────────────────────────────────────────────────────
N=New   L=Load   Q=Quit   ?=Help
```

Press `N` to start a new image or `L` to load an existing one and edit
its plan. `?` opens the help overlay from any screen.

### `N` — create a new image

The New flow walks four prompts in order: filename → size → format →
strict-TOS (AHDI only). Each shows up at the bottom of the screen.

**1. Filename:** prompts for the output path. Existing files trigger an
overwrite confirm.

**2. Size picker:**

```
Image size:  1)16  2)64  3)128  4)256  5)512  6)1024  7)2048  8)4096   c)Custom
```

Press `1`–`8` for a preset; `c` opens a numeric prompt for any value in
16–8192 MB.

**3. Format chooser:**

```
Format: [A]HDI  [P]PDRIVER  [H]DDRIVER (experimental)  (Esc cancel)
```

**4. TOS strict prompt** (AHDI only): tighter caps for original 520ST /
1040ST / Mega ST hardware (16 MB GEM, 256 MB BGM instead of the
TOS 1.04+ defaults of 31 / 511 MB).

After the wizard you land on the main screen with an empty partition
list:

```
atari-hd image creator  (C) 2026 - GOODDATA LABS SL  v0.1.0  image: /...boot.img
────────────────────────────────────────────────────────────────────────────────
         No partitions defined -- press A to add or U to autopartition
  Format: AHDI  TOS<1.04: off  Size: 256 MB  Bootable: no (B to enable)
────────────────────────────────────────────────────────────────────────────────
N=New  L=Load  A=Add  D=Delete  E=Edit  T=Type  W=Write  B=Boot  U=Autopart  F=Format  Q=Quit
```

### `A` — add partitions manually

Opens a modal dialog:

```
┌──────────────────────────────────────────────────────┐
│ Add Partition                                        │
│                                                      │
│   Size (2-31 MB):      15                            │
│   Type:                GEM (locked)                  │
│   Label:               BOOT                          │
│                                                      │
│ Status: OK                                           │
│                                                      │
│ [Tab] field  [t] cycle  [Enter] save  [Esc] cancel   │
│                                                      │
└──────────────────────────────────────────────────────┘
```

`Tab` moves between fields, `t` cycles the Type column, `Enter` saves.
The size cap shown in the prompt label is always live: it adapts to
the format, the slot index, the strict-TOS flag, and (for AHDI)
whether bootable mode is on.

### `U` — auto-partition

A two-prompt wizard that fills the partition list with sensible
defaults derived from the format and image size.

```
Auto-partition mode:  [D]efault (few large)   [M]ax (many equal)   (Esc cancel)
```

- **Default**: small boot at the format's minimum + as few data slots
  as possible at the per-slot maximum. Good for "give me a usable
  layout, fewest slots".
- **Max**: same boot + remaining space split equally across the
  format's max partition count. Good for "I want lots of small
  partitions" (e.g. one drive letter per project).

Then asks for the partition count, with the natural value as the
default:

```
Number of partitions [default 3]: _  (Esc cancel)
```

After confirm, the populated list looks like:

```
atari-hd image creator  (C) 2026 - GOODDATA LABS SL  v0.1.0  image: /...boot.img
────────────────────────────────────────────────────────────────────────────────
  Slot  Type     Start (LBA)        Size  Label
     0  GEM                2     15.0 MB  BOOT
     1  BGM           30,722    241.0 MB  DATA1
  Format: AHDI  TOS<1.04: off  Size: 256 MB  Bootable: yes (ICDBOOT.PRG)
────────────────────────────────────────────────────────────────────────────────
N=New  L=Load  A=Add  D=Delete  E=Edit  T=Type  W=Write  B=Boot  U=Autopart  F=Format  Q=Quit
```

### `B` — toggle bootable mode

Per format:

- **AHDI** — opens a path prompt for the user-supplied `ICDBOOT.PRG`.
  The file's `.PRG` magic is validated inline. With bootable on, slot 0
  is hard-capped at 15 MB.
- **PPDRIVER** — single-key toggle (uses the bundled boot blob; no
  path needed).
- **HDDRIVER** — prints a docs pointer; this tool can't make HDDRIVER
  images self-bootable, see [`BOOTABLE.md`](BOOTABLE.md).

The format-hint row on the main screen surfaces the current state:
`Bootable: yes (ICDBOOT.PRG)` / `Bootable: no (B to enable)` /
`Bootable: manual (HDDRUTIL.APP)`.

### `W` — write the image

Validates the plan against the format's caps, allocates the file,
formats each partition's FAT16, stamps the partition table (and IPL
when bootable), and atomically `os.replace`s into the target path.
Pre-existing files prompt for overwrite confirmation.

### Other keys at a glance

| Key | What it does |
|---|---|
| `D` | Delete the selected partition. |
| `E` | Edit the selected partition (same dialog as `A`). |
| `T` | Toggle GEM/BGM ident on the selected AHDI partition. |
| `F` | Re-pick format (preserves partitions if compatible). |
| `↑` / `↓` / `k` / `j` | Move selection. |
| `?` | Open the help overlay (full key list). |
| `Q` | Quit (warns on unsaved changes). |

### Help overlay

```
┌──────────── atari-hd help (Esc / ? to close) ────────────┐
│Files / Quit                                              │
│  N                 New image                             │
│  L                 Load image                            │
│  Q                 Quit (warns if unsaved)               │
│Partitions                                                │
│  Up / Dn / k / j   Move selection                        │
│  A                 Add partition                         │
│  D                 Delete selected                       │
│  E                 Edit selected                         │
│  T                 Toggle GEM/BGM (AHDI)                 │
│  F                 Change format                         │
│  W                 Write image                           │
│  B                 Toggle bootable mode (AHDI/PPDRIVER)  │
│  U                 Auto-fill partitions (default / max)  │
│Dialogs                                                   │
│  Tab               Next field                            │
│  t / Left / Right  Cycle Type                            │
│  Enter / S         Save                                  │
│  A / P / H         Pick format (in selector)             │
│  y / N / O         Confirm prompts (O = overwrite)       │
│  Esc               Cancel dialog / prompt                │
│Anywhere                                                  │
│  ?                 Toggle this help                      │
│                                                          │
│           (C) 2026 - GOODDATA LABS SL  v0.1.0            │
└──────────────────────────────────────────────────────────┘
```

---

## CLI (prompt mode + scripted runs)

If you'd rather not run the TUI — over SSH, in a CI pipeline, or
just because you prefer linear prompts — pass `--no-tui` (or
simply pipe a script into the tool). The flow walks the same
steps as the TUI in order: filename → size picker → format →
strict-TOS → auto-partition prompt → partition list → confirm →
build. Every prompt has a sensible default; press Enter to accept
it and keep moving.

### Flags

```
--no-tui              Force the prompt-mode CLI even on a TTY.
--tui                 Force the TUI even when stdin/stdout aren't a TTY.

--ahdi-driver=PATH    Make a self-bootable AHDI image. PATH is your
                      ICDBOOT.PRG (validate first with
                      tools/check_icd_driver.py).
--ppdriver-bootable   Make a self-bootable PPDRIVER image (uses the
                      bundled boot blob; no path required).

--size MB             Pre-fill the image-size prompt (16..8192 MB).
--auto MODE           Auto-fill partitions: default | max | off.
--auto-n N            N-limit for --auto (total partition count).

--version             Print "atari-hd <version>" and exit.
-h / --help           Show all flags.
```

### One-liner: scripted bootable AHDI

```
atari-hd --no-tui \
    --ahdi-driver path/to/ICDBOOT.PRG \
    --size 256 --auto default --auto-n 2
```

(Or `python3 scripts/atari-hd/atari_hd.py …` if you're running from
a clone without installing.)

The user only types the output filename and confirms at the final
proceed prompt. Everything else is pre-filled.

---

## Internals

The remainder of this document is reference material for contributors
and the curious. Nothing below is required reading for everyday use.

### Supported layouts

| Menu | Label | Sector 0 | Per-partition boot area |
|------|-------|----------|-------------------------|
| 1 | `AHDI` | AHDI partition table (no MBR signature) | single DOS-style BPB |
| 2 | `PPDRIVER` | standard MBR (`0x55AA`) | DOS BPB @ `firstLBA`, TOS BPB @ `firstLBA+1`, both stamped with OEM `PPGDODBC` |
| 3 | `HDDRIVER` | MBR (`0x55AA`) + AHDI overlap marker at slot 2 (`0x1DE`) | DOS BPB @ `firstLBA`, TOS BPB @ `firstLBA+1` |

The FAT16 filesystem for each partition is produced by an in-tree
pure-Python writer; this script also assembles the partition table, the
hybrid BPBs, and the outer image bytes.

### Per-format partition layout

All three formats cap at **14 partitions** total per image (the TOS
drive-letter ceiling: C: through P:). They differ in how many of those
can be primaries vs. how many have to live in an extended chain:

| Format | Max primary | Primary slots | Extended chain type | When extended kicks in |
|--------|------------:|---------------|---------------------|------------------------|
| AHDI | 4 | AHDI root slots 0..3 at 0x1C6 / 0x1D2 / 0x1DE / 0x1EA | XGM (AHDI-native) | N > 4: use slots 0..2 as primaries, slot 3 as the XGM chain head |
| PPDRIVER | 4 | MBR slots 0..3 at 0x1BE | MBR extended (type 0x0F) | N > 4: 3 primaries + extended container |
| HDDRIVER | 1 | MBR P0 only (slot 2 is consumed by the AHDI overlap marker at 0x1DE) | MBR extended (type 0x0F) | N > 1: P0 primary + extended container |

For AHDI, each XGM sub-descriptor sector follows the Atari convention:
AHDI slot 0 holds the logical partition (start **relative to the
descriptor's own LBA**); AHDI slot 1 holds the XGM link to the next
sub-descriptor (start **relative to the chain base = the first
sub-descriptor's LBA**). No MBR signature anywhere in the image.

For the hybrid formats, each EBR sector follows the Linux/Windows
convention: MBR slot 0 is the logical partition (start-LBA relative to
the EBR); MBR slot 1 is the next-EBR link (start-LBA relative to the
extended container's base). The `0x55AA` signature sits at byte 510 of
every EBR.

### TOS compatibility (AHDI only)

| Mode | GEM cap | BGM cap |
|---|:---:|:---:|
| TOS 1.04+ (default) | 31 MB | 511 MB |
| TOS < 1.04 (strict) | 16 MB | 256 MB |

The "512 MB" figure quoted in much of the AHDI literature is rounded
up; the real ceiling is `NSECTS = 65535` at `bps = 8192` =
**511.99 MB**. A 512 MB BGM partition trips the Hatari sector-doubling
rule into `bps = 16384`, which TOS 1.04 - 3.x doesn't support. The
hybrid formats keep the same caps because they share the underlying
TOS view.

### Bootable images

AHDI bootable mode embeds two ICD assets (sector 0 IPL +
BPB-with-continuation-IPL) as repo-shipped blobs and patches the
geometry-dependent fields at build time. PPDRIVER bootable mode
embeds the entire 15-sector boot blob from a known-good reference.
Full chain diagrams + asset SHAs live in
[`BOOTABLE.md`](BOOTABLE.md).

### Tests

```
python3 -m unittest discover scripts/atari-hd/tests
```

68+ unit tests cover the FAT16 writer, the partition-table writers /
parsers, the auto-partition layout helper, round-trip parse-back, and
caps. There's also a real-hardware byte-parity harness under
`tests/realhw_parity.py` for manual regression-checking against
user-supplied reference images.

### Project layout

```
scripts/atari-hd/
├── atari_hd.py          Top-level entry point + writer / parser
├── tui/                 Curses-free terminal UI (epic-003)
├── assets/              ICD + PPDRIVER boot-asset blobs
├── tools/               Stdlib validators for user-supplied drivers
├── drivers/             User-supplied third-party drivers (gitignored)
├── tests/               Unit tests + parity harness
├── atari-hd             POSIX launcher shim (exec'd by ~/.local/bin/atari-hd)
├── atari-hd.cmd         Windows launcher shim (exec'd by the WindowsApps shim)
├── install.sh           macOS / Linux one-line installer
├── install.ps1          Windows one-line installer
├── BOOTABLE.md          Per-format bootable-image guide
├── README.md            (this file)
└── version.txt          Single-line semver
```

The `drivers/` directory is gitignored — it's where users keep
their copies of `ICDBOOT.PRG` and other third-party binaries the
tool doesn't redistribute.

> **Looking for `ICDBOOT.PRG` / `ICDBOOT.SYS`?** ICD Pro 6.55a is
> still available as a free download:
> <http://joo.kie.sk/wp-content/uploads/2013/05/icdp655a.zip>.
> The boot driver inside the archive is `ICDBOOT.PRG`; once it
> lands on a bootable AHDI partition the loader picks it up as
> `ICDBOOT.SYS`. Drop your copy under `drivers/` and point the
> tool at it when prompted.

---

## Writing the image to a physical device

The output of this tool is a raw disk image (`.img`). You can use it
several ways:

- **SidecarTridge Multi-device** — the drives-emulator firmware reads
  the image from its own SD card. See the SidecarTridge documentation
  for how to load images onto the device; you don't need to `dd` for
  that workflow.
- **ACSI2STM** — TinyUSB / Raspberry Pi-based ACSI bridge that reads
  raw `.img` files off a microSD / SD card.
- **SatanDisk** — SD-card-backed ACSI hard disk for the Atari ST.
- **Direct SCSI / IDE / CompactFlash** — written to a CF / IDE / SCSI
  card via a USB adapter, then plugged into a real ACSI bus, an IDE
  upgrade, or a SatanDisk-style host.

For all of the "raw card" targets above, the workflow is the same:
write the `.img` byte-for-byte onto the card using `dd` (POSIX) or
its equivalent on Windows. The card then carries the bytes the Atari
expects to see at LBA 0 onwards — partition table, bootable IPL, FAT
filesystem, the works.

> ⚠️ **Writing to the wrong device wipes whatever was on it.** The
> commands below use *raw block device names* (`/dev/disk2`,
> `/dev/sdb`, `\\.\PhysicalDriveN`). A typo here can erase your
> system disk in seconds. Always run a `list` command first, confirm
> the size matches the card you just inserted, and unmount before
> writing.

### macOS

```
diskutil list                              # find the card -- look for
                                            # the matching size
diskutil unmountDisk /dev/disk2            # release any auto-mounts
sudo dd if=path/to/your.img of=/dev/rdisk2 bs=1m
diskutil eject /dev/disk2                  # safe to remove now
```

Notes:

- Use `/dev/rdiskN` (the **r**aw character device) rather than
  `/dev/diskN`. The `r` variant bypasses the buffer cache; on
  modern macOS the buffered path is dramatically slower (often 20×)
  and on some releases the kernel refuses raw writes through the
  buffered path even after `unmountDisk`.
- `unmountDisk` (not `umount`) detaches every partition on the
  device at once — Finder will have re-mounted any FAT partitions
  the moment macOS saw them.
- If macOS still refuses with `Operation not permitted`, give
  Terminal full-disk access in System Settings → Privacy & Security
  → Full Disk Access, then relaunch.

### Linux

```
lsblk                                       # find the card by size
sudo umount /dev/sdb*                       # unmount any auto-mounted
                                            # partitions (sdb1, sdb2, ...)
sudo dd if=path/to/your.img of=/dev/sdb bs=4M conv=fsync status=progress
sudo eject /dev/sdb                         # safe to remove
```

Notes:

- Replace `sdb` with whatever `lsblk` reports for the card (often
  `sdb`, `sdc`, or `mmcblk0` for built-in SD readers — for the
  latter use `of=/dev/mmcblk0`).
- `conv=fsync` flushes the kernel cache before `dd` returns, so
  pulling the card right after the command finishes is safe.
- `status=progress` is GNU coreutils-specific; drop it on BusyBox /
  Alpine.

### Windows

The Windows command line doesn't ship `dd`. Three good options, in
descending order of safety / convenience:

**Option A — [Win32 Disk Imager](https://sourceforge.net/projects/win32diskimager/)**
(graphical, recommended for first-time users):

1. Insert the card; note the drive letter Windows assigns.
2. Launch Win32 Disk Imager **as Administrator**.
3. Select the `.img` file under "Image File".
4. Pick the matching drive letter under "Device".
5. Click **Write**, confirm the warning. Done.

**Option B — [Rufus](https://rufus.ie)** (graphical, good for tricky
cards): pick the `.img`, the device, set "Image option" to *DD Image*
(important — leave it on *ISO Image* and Rufus will rewrite the
image), click Start.

**Option C — `dd for Windows`** (CLI, for scripting). After
[installing chrysocome.net's
build](http://www.chrysocome.net/dd):

```
dd --list                                   # find your card under
                                            # \\.\PhysicalDriveN
dd if=path\to\your.img of=\\.\PhysicalDriveN bs=1M --progress
```

Run from an Administrator-elevated `cmd.exe`. As with macOS / Linux,
make sure no Explorer windows are open on the card before writing.

### After writing

- **Eject** the card via your OS (don't just yank it; pending writes
  may not be flushed).
- **Insert** it into the target device (SidecarTridge / ACSI2STM /
  SatanDisk / etc.).
- **Boot** the Atari. For bootable AHDI / PPDRIVER images this should
  be cold-boot; for HDDRIVER you'll need to install the driver via
  `HDDRUTIL.APP` on first boot (see [`BOOTABLE.md`](BOOTABLE.md)).

If the Atari doesn't recognise the disk, the most common causes are
(a) writing to the wrong physical device (re-check `diskutil list`
/ `lsblk` / Disk Management) and (b) writing only the partition
file rather than the whole-disk image — `dd` over the *device*, not
a partition slice (`/dev/sdb`, **not** `/dev/sdb1`).

## 📄 License

This project is licensed under the **GNU General Public License v3.0**.  
See the [LICENSE](https://github.com/sidecartridge/md-drives-emulator/blob/main/LICENSE) file for full terms.

## 🤝 Contributing
Made with ❤️ by [SidecarTridge](https://sidecartridge.com)
