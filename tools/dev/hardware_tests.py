#!/usr/bin/env python3
"""Run a test harness on the hardware and keep its log.

    tools/dev/hardware_tests.py --harness floptest --disk rw
    tools/dev/hardware_tests.py --harness floptest --disk ro-hd --name 2.06-ro-hd
    tools/dev/hardware_tests.py --harness fstests

One command makes a whole run, with the device in its setup menu and the card
on USB. The harness goes into the AUTO folder of the GEMDRIVE folder, as
<HARNESS>.PRG, and the other harness comes out of it: whichever finishes first
restarts the device. For FLOPTEST the disk pair is made fresh in the floppy
folder by make_floppy_image.py and chosen in the menu over SWD - the disk in
drive A, the other one of the pair in slot 2 for the cycle case - and the read
failure the harness checks is armed. Then the card is ejected and checked
gone, [E] starts the emulation, SELECT is pressed when FLOPTEST reads the
sector that says it waits for drive A to cycle, and when the harness has
restarted the device and the card is back, the run's section of its log is
copied to tools/dev/logs/<harness>-hw-<name>.txt and summed up. A writable
disk is then checked with make_floppy_image.py check-rw.

The harness ends by restarting the device and rebooting the computer, so the
computer is back in the setup menu and the next run can follow at once.

Needs a debug build (the menu and the read failure go through the devhooks
mailbox), `console.py watch` running (the SELECT press waits for FLOPTEST's
cue on the console), GEMDRIVE on as C:, and the harnesses built with file
logging (the third argument to tests/atarist/build.sh).

--release runs on a release build, which has neither the mailbox nor the
console: nothing is chosen in the menu, so drive A must already hold the disk
the run is for - FLOPTEST.ST.RW for rw and rw-hd, FLOPTEST.ST for ro and
ro-hd, with the other one in slot 2 - set on a debug build before flashing the
release one (the settings survive a flash). The images are written under those
names, and once the card is ejected someone presses [E] on the ST's keyboard:
the menu's countdown stays stopped once the card has been on USB, and a
release build takes no keys over SWD. The read-failure and cycle cases skip.
"""

import argparse
import glob
import hashlib
import os
import re
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DIST = os.path.join(REPO, "tests", "atarist", "dist")
LOGS = os.path.join(HERE, "logs")
SWD = [sys.executable, os.path.join(HERE, "swd.py")]
CONSOLE = [sys.executable, os.path.join(HERE, "console.py")]
MAKE_IMAGE = [sys.executable, os.path.join(HERE, "make_floppy_image.py")]
CARD = "/Volumes/IMG"

HARNESSES = {
    "floptest": {"program": "FLOPTEST", "log": "FLOPTEST.TXT",
                 "banner": "Atari ST floppy test suite"},
    "fstests": {"program": "FSTESTS", "log": "LOG.TXT",
                "banner": "Atari ST GEMDRIVE Test Suite"},
}

# FLOPTEST's disks: what make_floppy_image.py makes, the file it goes in, and
# the disk put in slot 2 - the other one of the pair, so that the cycle case
# sees the mode change. The one-sided disk has no cycle case.
DISKS = {
    "rw": ("rw", "FLOPTEST.ST.RW", ("ro", "FLOPTEST.ST")),
    "ro": ("ro", "FLOPTEST.ST", ("rw", "FLOPTEST.ST.RW")),
    "rw-hd": ("rw-hd", "FLOPHD.ST.RW", ("ro-hd", "FLOPHD.ST")),
    "ro-hd": ("ro-hd", "FLOPHD.ST", ("rw-hd", "FLOPHD.ST.RW")),
    "ro-ss": ("ro-ss", "FLOPSS.ST", ("ro", "FLOPTEST.ST")),
}

FAILING_SECTOR = 1320  # floppy_tests.c: the read the host makes fail
CYCLE_CUE = "LSECTOR: 1330 "  # floppy_tests.c: read to ask for SELECT


def swd(*args):
    return subprocess.run(SWD + list(args), capture_output=True,
                          text=True).stdout


def screen_lines():
    return swd("text").splitlines()


def menu_folders():
    """The GEMDRIVE folder and drive, and the floppy folder, from the menu."""
    lines = screen_lines()
    gemdrive = floppy = drive = None
    section = None
    for line in lines:
        if line.startswith("[G]EMDRIVE"):
            section = "gemdrive" if line.rstrip().endswith("Yes") else None
        elif line.startswith("[F]LOPPY"):
            section = "floppy"
        elif line.startswith("["):
            section = None
        found = re.search(r"F\S*der: (\S+)", line)
        if found and section == "gemdrive":
            gemdrive = found.group(1)
        elif found and section == "floppy":
            floppy = found.group(1)
        found = re.search(r"\[D\]rive: (\S)", line)
        if found and section == "gemdrive":
            drive = found.group(1)
    return gemdrive, drive, floppy


def choose(name):
    """In a file browser the menu has opened, move down to NAME and take it."""
    lines = screen_lines()
    entries = [l for l in lines[3:] if l[:2] in ("  ", "> ") and l[2:].strip()]
    names = [l[2:].strip() for l in entries]
    if name not in names:
        sys.exit("%s is not in the menu's list: %s" % (name, names))
    current = next(i for i, l in enumerate(entries) if l.startswith("> "))
    if names.index(name) < current:
        sys.exit("%s is above the cursor in the menu's list" % name)
    for _ in range(names.index(name) - current):
        swd("key", "\x0e")
    selected = [l for l in screen_lines() if l.startswith("> ")]
    if not selected or selected[0][2:].strip() != name:
        sys.exit("could not reach %s in the menu (at %s)" % (name, selected))
    swd("key", " ")


def clean_card_files(*folders):
    """macOS leaves ._ files beside what it copies, and the floppy browser
    lists ._X.ST as a disk: strip the attributes and delete them."""
    for folder in folders:
        subprocess.run(["xattr", "-cr", folder], capture_output=True)
        for path in glob.glob(os.path.join(folder, "._*")):
            os.remove(path)


def md5(path):
    try:
        with open(path, "rb") as handle:
            return hashlib.md5(handle.read()).hexdigest()
    except OSError:
        return None


def mount_card():
    """macOS does not always mount the card again when the device comes back
    in its setup menu: mount the IMG volume if its disk is there."""
    listing = subprocess.run(["diskutil", "list", "external"], capture_output=True,
                             text=True).stdout
    for line in listing.splitlines():
        fields = line.split()
        if len(fields) > 2 and "IMG" in fields and fields[-1].startswith("disk"):
            subprocess.run(["diskutil", "mount", fields[-1]], capture_output=True)
            return


def wait_for_card(timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.isdir(CARD):
            return True
        mount_card()
        time.sleep(1)
    return False


def press_select_on_cue():
    subprocess.run(CONSOLE + ["wait", CYCLE_CUE, "--timeout", "900"],
                   capture_output=True)
    answer = swd("select", "short").strip().splitlines()
    print(answer[-1] if answer else "SELECT: no answer from swd.py")


def last_run(text, banner):
    start = text.rfind(banner)
    return text[start:] if start >= 0 else ""


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--harness", choices=sorted(HARNESSES), default="floptest")
    parser.add_argument("--disk", choices=sorted(DISKS), default="rw",
                        help="FLOPTEST's disk in drive A (default rw)")
    parser.add_argument("--name", help="the log's name in tools/dev/logs "
                        "(default: the TOS the run reports, and the disk)")
    parser.add_argument("--timeout", type=int, default=1800,
                        help="seconds to wait for the run (default 1800)")
    parser.add_argument("--release", action="store_true",
                        help="a release build: the menu is not driven (see above)")
    args = parser.parse_args()
    harness = HARNESSES[args.harness]

    if not wait_for_card(60):
        sys.exit("the card is not mounted: is the device in its setup menu?")
    if not args.release:
        swd("app", "countdown_stop")
    gemdrive, drive, floppy = menu_folders()
    if gemdrive is None or drive != "C":
        sys.exit("GEMDRIVE must be on as C: to start the harness from C:\\AUTO")

    auto = os.path.join(CARD, gemdrive.strip("/"), "AUTO")
    os.makedirs(auto, exist_ok=True)
    program = os.path.join(DIST, harness["program"] + ".TOS")
    if not os.path.exists(program):
        sys.exit("%s is not built" % program)
    with open(program, "rb") as source, \
            open(os.path.join(auto, harness["program"] + ".PRG"), "wb") as target:
        target.write(source.read())
    for other in HARNESSES.values():
        if other is not harness:
            stale = os.path.join(auto, other["program"] + ".PRG")
            if os.path.exists(stale):
                os.remove(stale)
    folders = [auto]

    image = None
    if args.harness == "floptest":
        if floppy is None:
            sys.exit("floppy emulation must be on")
        disks = os.path.join(CARD, floppy.strip("/"))
        kind, name, (other_kind, other_name) = DISKS[args.disk]
        if args.release:
            if args.disk == "ro-ss":
                sys.exit("ro-ss needs its own disk in drive A: a debug build")
            name, other_name = (("FLOPTEST.ST.RW", "FLOPTEST.ST")
                                if kind.startswith("rw") else
                                ("FLOPTEST.ST", "FLOPTEST.ST.RW"))
            shown = [l for l in screen_lines() if "(SHFT+)A] Drive:" in l]
            if not shown or not shown[0].rstrip().endswith("/" + name):
                sys.exit("drive A must hold %s for a release run (menu: %s); "
                         "set it on a debug build" % (name, shown))
        image = os.path.join(disks, name)
        for make, file_name in ((kind, name), (other_kind, other_name)):
            subprocess.run(MAKE_IMAGE + [make, os.path.join(disks, file_name)],
                           check=True, capture_output=True)
        folders.append(disks)
        clean_card_files(*folders)
        os.sync()
        if not args.release:
            swd("key", "a")
            choose(name)
            swd("key", "\x01")
            swd("key", "2")
            choose(other_name)
            swd("key", "m")
            print(swd("app", "floppy_fail_read", str(FAILING_SECTOR)).strip())
    else:
        clean_card_files(*folders)

    log = os.path.join(CARD, gemdrive.strip("/"), harness["log"])
    before = md5(log)
    os.sync()
    subprocess.run(["diskutil", "eject", CARD], capture_output=True)
    time.sleep(2)
    if os.path.isdir(CARD):
        sys.exit("the card is still mounted: [E] would do nothing")

    if args.release:
        print("card ejected: press [E] on the ST's keyboard to start the run")
    else:
        if args.harness == "floptest":
            threading.Thread(target=press_select_on_cue, daemon=True).start()
        print(swd("key", "e").strip())

    deadline = time.time() + args.timeout
    text = ""
    while time.time() < deadline:
        time.sleep(5)
        if not os.path.isdir(CARD):
            mount_card()
            continue
        time.sleep(3)  # the card has only just come back
        now = md5(log)
        if now is not None and now != before:
            try:
                with open(log, "r", errors="replace") as handle:
                    text = last_run(handle.read(), harness["banner"])
            except OSError:
                text = ""  # the card went away again while it was read
            if "All tests completed." in text:
                break
        text = ""
    if not text:
        sys.exit("no new run in the log within %d s" % args.timeout)
    if not args.release:
        swd("app", "countdown_stop")

    tos = re.search(r"^TOS (\S+),", text, re.M)
    name = args.name or "-".join(
        [tos.group(1) if tos else "tos"] +
        ([args.disk] if args.harness == "floptest" else []))
    kept = os.path.join(LOGS, "%s-hw-%s.txt" % (args.harness, name))
    with open(kept, "w") as handle:
        handle.write(text)

    counts = [len(re.findall(r"^\[%s\]" % re.escape(mark), text, re.M))
              for mark in (" OK ", "FAIL", "SKIP")]
    for line in text.splitlines():
        if line.startswith("TOS ") or line.startswith("Test disk"):
            print(line)
    print("OK %d FAIL %d SKIP %d" % tuple(counts))
    for line in text.splitlines():
        if line.startswith("[FAIL]") or line.startswith("[SKIP]"):
            print(line)
    if image and args.disk.startswith("rw"):
        print(subprocess.run(MAKE_IMAGE + ["check-rw", image], capture_output=True,
                             text=True).stdout.strip())
    print("log: %s" % os.path.relpath(kept, REPO))
    return 1 if counts[1] else 0


if __name__ == "__main__":
    sys.exit(main())
