#!/usr/bin/env python3
"""Run the test harnesses against the firmware's own drivers under EmuMD.

hatari_tests.py runs the harnesses against what Hatari offers (its GEMDOS
drive, its floppy drive), the reference for what is correct. This runs them
against the firmware: the plugin from tools/dev/mdfw.sh build in EmuMD's
Hatari, the harness in the AUTO folder of GEMDRIVE's C:, on a microSD card
that is a folder made fresh for the run. Nothing else is needed, no card and
no ST, but nothing is timed: the bus and sign-off stay on the device.

Usage:
    tools/dev/emu_tests.py [--harness fstests|floptest|rtctest ...]
                           [--tos 1.04 ...] [--timeout SECONDS]

Each run leaves its card, flash and Hatari output in
tools/dev/logs/emu/<harness>-<TOS>/. Exit status 1 if a test fails or a run
does not finish. A failure EmuMD cannot help is listed as a limit, by name
(KNOWN_LIMITS), and does not count.
"""
import argparse
import os
import shutil
import signal
import subprocess
import sys
import time

import md_settings
from hatari_tests import (DEFAULT_TOS_DIR, DIST, DONE_MARKER, HARNESSES,
                          MAKE_IMAGE, TOS_IMAGES, parse_log)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
LOGS = os.path.join(HERE, "logs", "emu")
PLUGIN = os.path.join(ROOT, "build", "drives-emulator.mdfw")
HATARI = os.path.join(HERE, "builds/emumd-cache/hatari-fork/build/src/hatari")
ENV = dict(os.environ, EMUMD_HATARI=HATARI)
MEMSIZE = "4"
FLOPPY_FOLDER = "floppies"

# Every driver set explicitly: what a run leaves on is what it tests.
SETTINGS = {
    "GEMDRIVE_ENABLED": "true", "GEMDRIVE_FOLDER": "/hd", "GEMDRIVE_DRIVE": "C",
    "ACSI_ENABLED": "false", "FLOPPY_ENABLED": "false", "RTC_ENABLED": "false",
}

# What each harness runs on: a name for the run, the disk in drive A with its
# file name, the disk in slot 2, and the settings that differ.
FLOPTEST_DISKS = {"rw": ("FLOPTEST.ST.RW", "ro", "FLOPTEST.ST"),
                  "ro": ("FLOPTEST.ST", "rw", "FLOPTEST.ST.RW")}

# Failures that come from EmuMD's FatFs, a thin layer over the host's files,
# and not from the firmware. (test name without its "(user) " prefix, why)
KNOWN_LIMITS = [
    ("Concurrent write to an open file must fail.", "no file locks"),
    ("Hidden bit is set on FATTR.TXT", "f_chmod: read-only bit only"),
    ("Readonly+hidden bits are set on FATTR.TXT", "f_chmod: read-only bit only"),
    ("FDTIME.TXT ", "f_utime is a stand-in"),
    ("FDTOTHER.TXT ", "f_utime is a stand-in"),
]


def limit_of(test):
    """Why a failed test is a known limit of EmuMD, or None."""
    name = test[len("(user) "):] if test.startswith("(user) ") else test
    for start, why in KNOWN_LIMITS:
        if name.startswith(start):
            return why
    return None


def mdfw_run(tos, machine, work, flash, extra=()):
    """The command that runs the plugin, its card and flash in work/."""
    return [
        os.path.join(HERE, "mdfw.sh"), "run", PLUGIN, "--headless",
        "--no-user-config", "--tos", tos, "--sd", os.path.join(work, "sd"),
        "-O", "flash=" + flash, "--log", os.path.join(work, "hatari.log"),
    ] + list(extra) + ["--", "--machine", machine, "--memsize", MEMSIZE,
                       "--fast-forward", "on"]


def make_seed(tos_dir):
    """A flash as the Booster and the firmware's first start leave it."""
    work = os.path.join(LOGS, "seed")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(work, "sd"))
    flash = os.path.join(work, "flash.bin")
    tos = os.path.join(tos_dir, TOS_IMAGES["1.04"][0])
    subprocess.run(mdfw_run(tos, "st", work, flash, ("--frames", "300")),
                   env=ENV, check=True, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, timeout=120)
    return flash


def run(name, disk, version, tos_dir, seed, timeout):
    """One harness on one TOS. Returns (results, log), or (None, None)."""
    harness = HARNESSES[name]
    image, machine = TOS_IMAGES[version]
    work = os.path.join(LOGS, "%s-%s" % (name + ("-" + disk if disk else ""),
                                         version))
    shutil.rmtree(work, ignore_errors=True)
    drive = os.path.join(work, "sd", "hd")
    os.makedirs(os.path.join(drive, "AUTO"))
    program = os.path.join(DIST, harness["program"])
    for copy in harness["copies"]:
        shutil.copy(program, os.path.join(drive, copy))
    shutil.copy(program, os.path.join(
        drive, "AUTO", harness["program"].split(".")[0] + ".PRG"))
    log_path = os.path.join(drive, harness["log"])

    settings = dict(SETTINGS)
    if name == "rtctest":
        settings["RTC_ENABLED"] = "true"
    image_a = None
    if disk:
        floppies = os.path.join(work, "sd", FLOPPY_FOLDER)
        os.makedirs(floppies)
        a_name, other_kind, other_name = FLOPTEST_DISKS[disk]
        image_a = os.path.join(floppies, a_name)
        for kind, path in ((disk, image_a),
                           (other_kind, os.path.join(floppies, other_name))):
            subprocess.run([sys.executable, MAKE_IMAGE, kind, path], check=True,
                           stdout=subprocess.DEVNULL)
        settings.update(FLOPPY_ENABLED="true",
                        FLOPPY_FOLDER="/" + FLOPPY_FOLDER,
                        FLOPPY_DRIVE_A="/%s/%s" % (FLOPPY_FOLDER, a_name),
                        FLOPPY_DRIVE_A_2="/%s/%s" % (FLOPPY_FOLDER, other_name))
    flash = os.path.join(work, "flash.bin")
    with open(seed, "rb") as handle:
        contents = md_settings.set_values(handle.read(), settings)
    with open(flash, "wb") as handle:
        handle.write(contents)

    command = mdfw_run(os.path.join(tos_dir, image), machine, work, flash,
                       # headless runs stop after 500 frames by default
                       ("--frames", "100000000", "--timeout", str(timeout + 30)))
    with open(os.path.join(work, "mdfw.out"), "w") as out:
        # Its own session: a run must end Hatari as well as mdfw.
        process = subprocess.Popen(command, env=ENV, stdout=out,
                                   stderr=subprocess.STDOUT,
                                   start_new_session=True)
        deadline = time.time() + timeout
        # The harness ends by restarting the device, after which Hatari stops
        # answering, even to SIGTERM: what the log holds by then is the result.
        while time.time() < deadline and process.poll() is None:
            if os.path.exists(log_path):
                with open(log_path, errors="replace") as handle:
                    if DONE_MARKER in handle.read():
                        time.sleep(2)
                        break
            time.sleep(1)
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
    if not os.path.exists(log_path):
        return None, None
    with open(log_path, errors="replace") as handle:
        log = handle.read()
    if disk == "rw":
        check = subprocess.run([sys.executable, MAKE_IMAGE, "check-rw", image_a],
                               stdout=subprocess.PIPE, universal_newlines=True)
        print("  image: " + check.stdout.strip())
    return parse_log(log, harness["banner"]), log


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--harness", action="append",
                        choices=list(HARNESSES),
                        help="repeatable; default all of them")
    parser.add_argument("--tos", action="append", metavar="VERSION",
                        help="a TOS of %s (repeatable; default 1.04)"
                        % ", ".join(TOS_IMAGES))
    parser.add_argument("--tos-dir", default=DEFAULT_TOS_DIR)
    parser.add_argument("--timeout", type=int, default=600,
                        help="seconds for each run")
    args = parser.parse_args()

    seed = make_seed(args.tos_dir)
    ok = True
    for version in args.tos or ["1.04"]:
        for name in args.harness or list(HARNESSES):
            for disk in FLOPTEST_DISKS if name == "floptest" else [None]:
                print("%s%s, TOS %s" % (name.upper(), " " + disk if disk else "",
                                        version), flush=True)
                results, log = run(name, disk, version, args.tos_dir, seed,
                                   args.timeout)
                if results is None:
                    print("  no log: did the program run?")
                    ok = False
                    continue
                if DONE_MARKER not in log:
                    print("  the run did not finish: partial results")
                    ok = False
                failed = [t for t, r in results.items() if r == "FAIL"]
                limits = [t for t in failed if limit_of(t)]
                real = [t for t in failed if not limit_of(t)]
                values = list(results.values())
                if name == "rtctest":
                    # A measurement of what TOS does with the date, and with
                    # no NTP the firmware leaves TOS alone: compare with
                    # hatari_tests.py --harness rtctest, nothing passes or fails.
                    print("  measured: %d yes, %d no (not a pass or fail)" % (
                        values.count("OK"), values.count("FAIL")))
                    continue
                print("  %d OK, %d FAIL, %d SKIP, %d EmuMD limits" % (
                    values.count("OK"), len(real), values.count("SKIP"),
                    len(limits)))
                for test in real:
                    print("  FAIL: " + test)
                ok = ok and not real and bool(values)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
