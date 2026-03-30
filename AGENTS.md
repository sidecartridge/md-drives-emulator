# AGENTS.md — Drives Emulator Workspace Notes

Welcome to the `md-drives-emulator` workspace. This file captures the local rules and build habits that matter when working on this repository.

## 1. Environment Setup
- **Workspace root:** `/Users/diego/mister_wkspc/md-drives-emulator`
- **SDK paths used by the RP build:**
  - `PICO_SDK_PATH=$REPO_ROOT/pico-sdk`
  - `PICO_EXTRAS_PATH=$REPO_ROOT/pico-extras`
  - `FATFS_SDK_PATH=$REPO_ROOT/fatfs-sdk`
- **Tooling used by this repo:**
  - Raspberry Pi Pico SDK / Extras
  - ARM GCC toolchain for RP2040
  - `stcmd` for the Atari target build
- **TTY note:** `stcmd` may require a PTY when run through an agent/tool wrapper.

## 2. Common Commands
```bash
# Incremental RP build from the existing build directory
cmake --build rp/build -j4

# Configure the RP build directory
cmake -S rp/src -B rp/build

# RP helper build script
./rp/build.sh pico_w release

# Full repo build
./build.sh pico_w debug 44444444-4444-4444-8444-444444444444
```

- Use `44444444-4444-4444-8444-444444444444` as the local validation UUID unless the user asks for a different one.

## 3. Build Notes & Gotchas
- `rp/build.sh` is not a lightweight incremental build. It:
  - reinitializes submodules
  - checks out pinned SDK tags
  - patches `fatfs-sdk`
  - drops `ffconf.h.bak` in the repo root
  - deletes and recreates `rp/build`
- Prefer `cmake -S rp/src -B rp/build` and `cmake --build rp/build -j4` for normal RP validation.
- The top-level `build.sh` wipes and recreates `dist/`.
- The Atari target build uses `stcmd` inside [target/atarist/build.sh](/Users/diego/mister_wkspc/md-drives-emulator/target/atarist/build.sh).

## 4. Editing Guardrails
- Do not modify vendored code unless the user explicitly asks for it:
  - `/pico-sdk`
  - `/pico-extras`
  - `/fatfs-sdk`
- Keep ROM bus timing changes isolated and minimal. A build passing does not prove the cartridge bus behavior is correct on hardware.
- Prefer incremental validation after each RP-side change when touching PIO, DMA, or linker behavior.
- `romemul` is the ROM4 path. `commemul` is the ROM3 sampled-command path.
- `romemul` and `commemul` currently run together from RP startup in [emul.c](/Users/diego/mister_wkspc/md-drives-emulator/rp/src/emul.c).
- `term.c` and `chandler.c` both ingest ROM3 samples via `commemul_poll()`. Do not reintroduce the old ROM4 DMA IRQ command path unless the user explicitly wants that rollback.
- ROM3/ROM4 work is timing-sensitive because both paths can touch shared bus control signals. If you change either PIO program, assume hardware validation is required even if the firmware builds.
- Protocol ACK timing matters. The remote side can retransmit if shared-memory token ACK writes are delayed behind slow handlers. Treat ACK-order changes as behavior changes, not refactors.

## 5. Formatting Rules
- Do **not** use `PRIu32`, `PRIx32`, or related `PRI*` format macros in this repo.
- When printing fixed-width integers, use normal `printf` format specifiers with explicit casts instead.
  - Example: cast to `unsigned long` and print with `%lu`
  - Example: cast to `unsigned int` and print with `%u` or `%04X`
- Keep debug traces readable and low-noise. Prefer batch or state-transition logs over per-cycle spam.

## 6. Network Notes
- `network.c` / `network.h` are aligned with the `md-browser` implementation as of March 2026.
- mDNS support is optional and guarded by `LWIP_MDNS_RESPONDER`. If it is disabled in `lwipopts.h`, the mDNS code should compile out cleanly.
- Prefer stack buffers over heap allocation in the network path unless there is a strong reason not to. The current WiFi STA path avoids `strdup()` for DNS/password parsing.

Keep this file updated when new repo-specific workflow rules or hardware gotchas are discovered.
