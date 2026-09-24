#!/usr/bin/env python3
"""Run a test harness under Hatari and report what each TOS makes of it.

    tools/dev/hatari_tests.py [--tos 1.04 --tos 2.06] [--hardware "1.04=path/LOG.TXT"]
    tools/dev/hatari_tests.py --harness floptest [--hardware "1.04 rw=path/FLOPTEST.TXT"]

Hatari is the reference for what correct means. For FSTESTS it is Hatari's
GEMDOS drive (--harddrive) standing in for GEMDRIVE. For FLOPTEST it is
Hatari's floppy drive: an emulated WD1772 read by TOS's own floppy driver, the
thing our floppy emulation replaces. Each floppy TOS is run twice, on the
writable disk and on the read-only one, both made fresh by
make_floppy_image.py, and after the writable run the image is checked for the
sector the test leaves written.

A run needs no hands: the harness writes its log into the GEMDOS drive
directory, which is a host directory, so the run ends as soon as the log says
it is over. Build the harnesses with file logging (the third argument to
tests/atarist/build.sh), or there is no log to read.

Hardware results come from the log left on the card, passed as
--hardware "<column>=<path>". The report is a table: one row per test, one
column per platform and TOS.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from collections import OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DIST = os.path.join(REPO, "tests", "atarist", "dist")
MAKE_IMAGE = os.path.join(HERE, "make_floppy_image.py")
DEFAULT_TOS_DIR = os.path.expanduser("~/mister_wkspc/TOS")

# What each harness is: its program and the copies of it the tests look for,
# its log, the line that starts each run in the log, and the disks it runs on
# (None: no floppy at all).
HARNESSES = OrderedDict([
    ("fstests", {"program": "FSTESTS.TOS",
                 "copies": ("FSTESTS.TOS", "FSTESTS.TTP"),
                 "log": "LOG.TXT",
                 "banner": "Atari ST GEMDRIVE Test Suite",
                 "disks": (None,),
                 "report": "fstests-matrix.md"}),
    ("floptest", {"program": "FLOPTEST.TOS",
                  "copies": ("FLOPTEST.TOS",),
                  "log": "FLOPTEST.TXT",
                  "banner": "Atari ST floppy test suite",
                  "disks": ("rw", "ro", "rw-hd", "ro-hd"),
                  "report": "floptest-matrix.md"}),
])

# Machines with a high-density drive: only they get the 1.44 MB disks.
HD_MACHINES = ("megaste", "tt", "falcon")

# TOS version -> (image file, Hatari machine). The machine matters: Hatari
# refuses a TOS its machine cannot run. TOS below 1.04 is not here at all:
# Hatari's own GEMDOS drive refuses it ("Please use at least TOS v1.04 for the
# HD directory emulation"), and so does its autostart. Our GEMDRIVE does work
# there, because it hooks the GEMDOS trap instead of using TOS's hard-disk
# support, so 1.00 and 1.02 can only be measured on hardware.
UNSUPPORTED_BY_HATARI = ("1.00", "1.02")
TOS_IMAGES = OrderedDict([
    ("1.04", ("tos104us.img", "st")),
    ("1.06", ("tos106us.img", "ste")),
    ("1.62", ("TOS v1.62 (1990)(Atari Corp)(STE)(US)[b].img", "ste")),
    ("2.06", ("TOS v2.06 (1991)(Atari Corp)(Mega-STE)(US).img", "megaste")),
    ("EmuTOS", ("etos512us.img", "st")),
])

DONE_MARKER = "All tests completed."
RESULT_RE = re.compile(r"^\[(?P<status> OK |FAIL|SKIP)\] (?P<name>.*?)(?: \(R: .*\))?\s*$")


def parse_log(text, banner):
    """The results of the last run in a log, as {test name: OK|FAIL|SKIP}.

    A log grows with every run (FSTESTS appends), so only the last run counts.
    """
    runs = text.split(banner)
    last = runs[-1] if len(runs) > 1 else text
    results = OrderedDict()
    for line in last.splitlines():
        match = RESULT_RE.match(line.strip())
        if match:
            status = match.group("status").strip() or "OK"
            results[match.group("name").strip()] = status
    return results


def run_hatari(harness, tos_path, machine, timeout, keep_dir=None, disk=None):
    """Run a harness under Hatari on one TOS. Returns its log, or None."""
    work = keep_dir or tempfile.mkdtemp(prefix="hatari-tests-")
    os.makedirs(work, exist_ok=True)
    drive = os.path.join(work, "c")
    os.makedirs(drive, exist_ok=True)
    for name in harness["copies"]:
        shutil.copy(os.path.join(DIST, harness["program"]),
                    os.path.join(drive, name))
    log_path = os.path.join(drive, harness["log"])
    floppy = []
    if disk:
        # Hatari decides the format by the extension, so both disks are .ST
        # here; which one it is lives inside, in MODE.TXT.
        image = os.path.join(work, "FLOPTEST_%s.ST" % disk.upper())
        subprocess.run([sys.executable, MAKE_IMAGE, disk, image], check=True,
                       stdout=subprocess.DEVNULL)
        floppy = ["--disk-a", image,
                  "--protect-floppy", "on" if disk.startswith("ro") else "off"]

    env = dict(os.environ, SDL_VIDEODRIVER="dummy")
    command = [
        "hatari",
        "--tos", tos_path,
        "--machine", machine,
        "--memsize", "1",
        "--sound", "off",
        "--conout", "2",
        "--fast-forward", "on",
        "--confirm-quit", "off",
        "--harddrive", drive,
        "--auto", "C:\\" + harness["program"],
    ] + floppy
    process = subprocess.Popen(command, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    deadline = time.time() + timeout
    log = None
    while time.time() < deadline:
        if process.poll() is not None:
            break
        if os.path.exists(log_path):
            with open(log_path, errors="replace") as handle:
                log = handle.read()
            if DONE_MARKER in log:
                break
        time.sleep(1)
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
    if os.path.exists(log_path):
        with open(log_path, errors="replace") as handle:
            log = handle.read()
    if disk and disk.startswith("rw"):
        # Hatari writes the disk back when it stops: did the write the test
        # leaves behind get there?
        check = subprocess.run([sys.executable, MAKE_IMAGE, "check-rw", image],
                               stdout=subprocess.PIPE, universal_newlines=True)
        print("  image: " + check.stdout.strip())
    if keep_dir is None:
        shutil.rmtree(work, ignore_errors=True)
    return log


def report(title, columns, results):
    """The matrix, as Markdown."""
    names = OrderedDict()
    for column in columns:
        for name in results[column]:
            names[name] = True

    lines = []
    lines.append("# %s results" % title)
    lines.append("")
    lines.append("Rows are tests, columns are where they ran. Produced by "
                 "`tools/dev/hatari_tests.py`.")
    lines.append("")
    lines.append("Hatari's GEMDOS drive needs TOS 1.04 or later, so TOS 1.00 and 1.02 "
                 "appear only as hardware columns.")
    lines.append("")
    counts = []
    for column in columns:
        values = results[column].values()
        counts.append("%s: %d OK, %d FAIL, %d SKIP, %d not run" % (
            column,
            sum(1 for v in values if v == "OK"),
            sum(1 for v in values if v == "FAIL"),
            sum(1 for v in values if v == "SKIP"),
            len(names) - len(results[column])))
    for line in counts:
        lines.append("- " + line)
    lines.append("")
    lines.append("| Test | " + " | ".join(columns) + " |")
    lines.append("| --- | " + " | ".join("---" for _ in columns) + " |")
    for name in names:
        row = [results[column].get(name, "–") for column in columns]
        lines.append("| %s | %s |" % (name.replace("|", "\\|"), " | ".join(row)))
    lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--harness", choices=list(HARNESSES), default="fstests",
                        help="which test program to run (default: %(default)s)")
    parser.add_argument("--tos", action="append", metavar="VERSION",
                        help="a TOS to run (default: all of %s)" % ", ".join(TOS_IMAGES))
    parser.add_argument("--tos-dir", default=DEFAULT_TOS_DIR,
                        help="where the TOS images are (default: %(default)s)")
    parser.add_argument("--hardware", action="append", default=[], metavar="COLUMN=LOG",
                        help="a log from the card, as a column")
    parser.add_argument("--timeout", type=int, default=300,
                        help="seconds to give one run (default: %(default)s)")
    parser.add_argument("--report",
                        help="where to write the table (default: "
                             "tools/dev/logs/<harness>-matrix.md)")
    parser.add_argument("--keep", metavar="DIR",
                        help="keep the emulated drive here instead of a temporary directory")
    args = parser.parse_args()
    harness = HARNESSES[args.harness]
    program = os.path.join(DIST, harness["program"])
    report_path = args.report or os.path.join(HERE, "logs", harness["report"])

    if not os.path.exists(program):
        sys.exit("No %s: build it first, with logging on:\n"
                 "  ./tests/atarist/build.sh \"$PWD/tests/atarist\" release 1" % program)

    columns, results = [], OrderedDict()
    for version in (args.tos or list(TOS_IMAGES)):
        if version in UNSUPPORTED_BY_HATARI:
            print("TOS %s: Hatari's GEMDOS drive needs TOS 1.04 or later, so this "
                  "one is for hardware only" % version)
            continue
        if version not in TOS_IMAGES:
            sys.exit("Unknown TOS %s: pick from %s (and %s on hardware only)" % (
                version, ", ".join(TOS_IMAGES), ", ".join(UNSUPPORTED_BY_HATARI)))
        image, machine = TOS_IMAGES[version]
        path = os.path.join(args.tos_dir, image)
        if not os.path.exists(path):
            print("skipping TOS %s: no %s" % (version, path))
            continue
        for disk in harness["disks"]:
            if disk and disk.endswith("-hd") and machine not in HD_MACHINES:
                continue
            column = "Hatari %s" % version + (" %s" % disk if disk else "")
            print("running TOS %s (%s)%s..." % (
                version, machine, ", %s disk" % disk if disk else ""), flush=True)
            keep = (os.path.join(args.keep, column.replace(" ", "-"))
                    if args.keep else None)
            log = run_hatari(harness, path, machine, args.timeout, keep, disk)
            if not log:
                print("  no %s: did the program run? is it built with logging?"
                      % harness["log"])
                continue
            results[column] = parse_log(log, harness["banner"])
            if DONE_MARKER not in log:
                print("  the run did not finish (timeout): partial results")
            columns.append(column)
            values = results[column].values()
            print("  %d OK, %d FAIL, %d SKIP" % (
                sum(1 for v in values if v == "OK"),
                sum(1 for v in values if v == "FAIL"),
                sum(1 for v in values if v == "SKIP")), flush=True)

    for entry in args.hardware:
        version, _, path = entry.partition("=")
        if not path:
            sys.exit('--hardware wants "<column>=<path to the log>"')
        with open(os.path.expanduser(path), errors="replace") as handle:
            column = "Hardware %s" % version
            results[column] = parse_log(handle.read(), harness["banner"])
            columns.append(column)

    if not columns:
        sys.exit("nothing ran")

    table = report(harness["program"].split(".")[0], columns, results)
    os.makedirs(os.path.dirname(os.path.abspath(report_path)), exist_ok=True)
    with open(report_path, "w") as handle:
        handle.write(table)
    print("\n" + table.split("| Test |")[0].rstrip())
    print("written to %s" % report_path)


if __name__ == "__main__":
    main()
