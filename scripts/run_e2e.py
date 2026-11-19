#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Golden-image end-to-end tests for the UI, run on the Linux emulator.

Each test is a script in tests/e2e/ made of emulator shell commands, one per
line, which is piped into the emulator. Every "screenshot_sync <name>.bmp"
line is an assertion: once the emulator exits, the screenshot is compared
pixel by pixel against tests/e2e/golden/<test>/<variant>/<name>.png. Lines
starting with "#" are comments and are not sent to the emulator.

A test runs on every emulator variant unless its name ends in _<variant>,
which restricts it to that variant (about_module17.txt runs on module17 and
stores its goldens under golden/about/module17/), or it has a line such as

    # variants: default smallscreen

A line such as "# codeplug: channels.rtxc" runs the test with that file
from tests/e2e as the emulator's codeplug.

The variants and the meson targets they are built from are:

    default      openrtx_linux               160x128 colour, GPS and RTC
    smallscreen  openrtx_linux_smallscreen   128x64 monochrome, GPS and RTC
    module17     openrtx_linux_mod17         128x64 monochrome, no GPS or RTC

Configured with -Dgfx_band_rows=16, the default variant draws in bands as
the colour radios do, and must match the same goldens.

Scripts should wait for the screen to settle with "wait_stable" after every
key press instead of sleeping for a fixed time, and take screenshots with
"screenshot_sync" so the file exists before the next command runs.

Every run starts from a clean state: the emulator runs in a temporary
directory with its own settings storage and no codeplug (unless the
script names one), offscreen with
SDL's software renderer and dummy audio, in UTC, under faketime with the
wall clock frozen at 2000-01-01 00:00:00. FAKETIME_DONT_FAKE_MONOTONIC=1
keeps the monotonic clock, which the firmware tick is derived from, running
at normal speed.

The emulators must be built with a fixed version string, so that the Info
screen does not change with every commit:

    meson setup build_linux -Dtest_version=e2e-test
    meson compile -C build_linux openrtx_linux openrtx_linux_mod17 \\
        openrtx_linux_smallscreen

Then run every test, or only some of them:

    python3 scripts/run_e2e.py
    python3 scripts/run_e2e.py --build-dir build_linux -j 4
    python3 scripts/run_e2e.py tests/e2e/about.txt --variant smallscreen

To add a test or accept a change to the UI, write or update its script, then
store the screenshots as the new goldens and review them before committing:

    python3 scripts/run_e2e.py --update-golden tests/e2e/about.txt

CI runs the tests in the devcontainer image: generate goldens there too, so
that they are known to match.

When a screenshot does not match, the actual and expected images and a diff
image with the differing pixels in red are saved to <build-dir>/e2e-failures.

Requires faketime and Pillow.
"""

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

try:
    from PIL import Image, ImageChops
except ImportError:
    sys.exit("Pillow is required: pip install -r requirements.txt")

PROJECT_ROOT = Path(__file__).resolve().parent.parent
E2E_DIR = PROJECT_ROOT / "tests" / "e2e"
GOLDEN_DIR = E2E_DIR / "golden"

# Variant name and emulator binary built for it by meson
VARIANTS = {
    "default": "openrtx_linux",
    "module17": "openrtx_linux_mod17",
    "smallscreen": "openrtx_linux_smallscreen",
}

# Version string the emulators must be built with (-Dtest_version=...)
TEST_VERSION = "e2e-test"

FAKE_TIME = "2000-01-01 00:00:00"

# Emulator output meaning that a command in the script did not work
SHELL_ERRORS = (
    "(type h or help for help)",
    "Error running that command",
    "Got too many arguments",
    "too many keys!",
    "wait_stable: screen not stable",
    "screenshot_sync: timed out",
)

# Key names understood by the key and keycombo commands, which silently ignore
# any other name
KEY_NAMES = {
    *(str(n) for n in range(10)),
    *(f"F{n}" for n in range(1, 7)),
    "STAR", "HASH", "ENTER", "ESC", "UP", "DOWN", "LEFT", "RIGHT", "MONI",
    "VOLUP", "VOLDOWN", "KNOB_LEFT", "KNOB_RIGHT",
}

KEY_RE = re.compile(r"^\s*(?:key|keycombo)\s+(.*)$")
SCREENSHOT_RE = re.compile(r"^\s*screenshot(?:_sync)?\s+(\S+)\s*$")
VARIANTS_RE = re.compile(r"^#\s*variants:\s*(.*)$")
CODEPLUG_RE = re.compile(r"^#\s*codeplug:\s*(\S+)\s*$")


class TestScript:
    """A test script, parsed."""

    def __init__(self, path):
        self.path = path
        self.name = path.stem
        self.variants = list(VARIANTS)
        self.codeplug = None
        self.commands = []
        self.screenshots = []
        self.errors = []

        for v in VARIANTS:
            if self.name.endswith(f"_{v}"):
                self.name = self.name[: -len(v) - 1]
                self.variants = [v]

        for lineno, text in enumerate(path.read_text().splitlines(), 1):
            self._parse(lineno, text.strip())

        if not self.screenshots:
            self.errors.append("no screenshot_sync command, nothing to check")

    def _parse(self, lineno, line):
        m = VARIANTS_RE.match(line)
        if m:
            self.variants = m.group(1).split()
            unknown = set(self.variants) - set(VARIANTS)
            if unknown:
                self.errors.append(
                    f"line {lineno}: unknown variant(s)"
                    f" {', '.join(sorted(unknown))}"
                )
            return

        m = CODEPLUG_RE.match(line)
        if m:
            self.codeplug = self.path.parent / m.group(1)
            if not self.codeplug.is_file():
                self.errors.append(
                    f"line {lineno}: codeplug {m.group(1)!r} not found"
                )
            return

        if not line or line.startswith("#"):
            return

        m = KEY_RE.match(line)
        if m:
            keys = [k.upper() for k in m.group(1).split()]
            unknown = [
                k for k in keys if k.removeprefix("KEY_") not in KEY_NAMES
            ]
            if unknown:
                self.errors.append(
                    f"line {lineno}: unknown key(s) {', '.join(unknown)}"
                )

        m = SCREENSHOT_RE.match(line)
        if m:
            shot = m.group(1)
            if shot != Path(shot).name or not shot.endswith(".bmp"):
                self.errors.append(
                    f"line {lineno}: screenshot name {shot!r} must be a"
                    " plain file name ending in .bmp"
                )
            elif shot in self.screenshots:
                self.errors.append(
                    f"line {lineno}: duplicate screenshot {shot!r}"
                )
            self.screenshots.append(shot)
            # The asynchronous screenshot command can race with "quit"
            line = f"screenshot_sync {shot}"

        self.commands.append(line)


def load_rgb(path):
    """Load an image as RGB, so that BMP and PNG files compare equal."""
    with Image.open(path) as img:
        return img.convert("RGB")


def diff_mask(actual, expected):
    """Return a mask that is 255 where two RGB images differ, 0 elsewhere."""
    r, g, b = ImageChops.difference(actual, expected).split()
    diff = ImageChops.lighter(ImageChops.lighter(r, g), b)
    return diff.point(lambda v: 255 if v else 0)


def count_diff(actual, expected):
    """Return the number of differing pixels between two RGB images."""
    if actual.size != expected.size:
        return actual.width * actual.height
    return diff_mask(actual, expected).histogram()[255]


def save_png(img, path):
    """
    Save an RGB image as a PNG as small as possible without losing any
    information: screenshots have few colours, so they are stored with an
    exact palette and the bit depth it needs.
    """
    colours = img.getcolors(256)
    if colours is not None:
        palette = Image.new("P", (1, 1))
        palette.putpalette([v for _, c in sorted(colours) for v in c])
        indexed = img.quantize(palette=palette, dither=Image.Dither.NONE)
        if count_diff(indexed.convert("RGB"), img) == 0:
            img = indexed

    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path, optimize=True)


def check_binary(binary):
    """Return None if the binary can run the tests, or the reason why not."""
    if not os.access(binary, os.X_OK):
        return f"{binary} not found, build it first"

    if f"{TEST_VERSION}\0".encode() not in binary.read_bytes():
        return (
            f"{binary} was not built with -Dtest_version={TEST_VERSION},"
            " reconfigure its build directory with"
            f" 'meson configure <dir> -Dtest_version={TEST_VERSION}'"
        )

    return None


def run_emulator(script, binary, workdir, args):
    """
    Run a test script in the emulator, in the given working directory.
    Return the emulator output and a list of problems.
    """
    problems = []
    if script.codeplug is not None:
        # The emulator reads its codeplug from default.rtxc in its cwd
        shutil.copy2(script.codeplug, workdir / "default.rtxc")
    env = {
        **os.environ,
        "TZ": "UTC",
        "XDG_STATE_HOME": str(workdir / "state"),
        "SDL_VIDEODRIVER": "offscreen",
        "SDL_RENDER_DRIVER": "software",
        "SDL_FRAMEBUFFER_ACCELERATION": "0",
        "SDL_AUDIODRIVER": "dummy",
        "FAKETIME_DONT_FAKE_MONOTONIC": "1",
    }
    cmd = ["faketime", "-f", FAKE_TIME, *args.wrapper, str(binary)]

    try:
        result = subprocess.run(
            cmd,
            input="\n".join(script.commands) + "\n",
            capture_output=True,
            text=True,
            timeout=args.timeout,
            cwd=workdir,
            env=env,
            check=False,
        )
        output = result.stdout + result.stderr
        if result.returncode != 0:
            problems.append(f"emulator exited with code {result.returncode}")
    except subprocess.TimeoutExpired as e:
        streams = (e.stdout or b"", e.stderr or b"")
        output = "".join(
            st.decode(errors="replace") if isinstance(st, bytes) else st
            for st in streams
        )
        problems.append(f"emulator still running after {args.timeout}s")

    for line in output.splitlines():
        if any(err in line for err in SHELL_ERRORS):
            problems.append(f"emulator: {line.strip()}")

    return output, problems


def check_screenshot(actual_path, golden, fail_dir, args):
    """
    Compare a screenshot with its golden image, or replace the golden with it
    when updating them. Return (problem, note), either of which can be None.
    """
    if not actual_path.is_file():
        return f"{actual_path.name}: screenshot not taken", None

    actual = load_rgb(actual_path)
    expected = load_rgb(golden) if golden.is_file() else None

    if args.update_golden:
        if (expected is not None) and (count_diff(actual, expected) == 0):
            return None, None
        save_png(actual, golden)
        return None, f"updated {golden.relative_to(PROJECT_ROOT)}"

    if expected is None:
        save_png(actual, fail_dir / f"actual_{golden.name}")
        return f"{actual_path.name}: no golden image {golden}", None

    diff = count_diff(actual, expected)
    if diff <= args.tolerance:
        return None, None

    save_png(actual, fail_dir / f"actual_{golden.name}")
    if actual.size != expected.size:
        return (
            f"{actual_path.name}: size {actual.size} instead of"
            f" {expected.size}"
        ), None

    red = Image.new("RGB", actual.size, (255, 0, 0))
    grey = actual.convert("L").convert("RGB")
    diff_img = Image.composite(red, grey, diff_mask(actual, expected))
    save_png(expected, fail_dir / f"expected_{golden.name}")
    save_png(diff_img, fail_dir / f"diff_{golden.name}")
    return f"{actual_path.name}: {diff} pixels differ", None


def run_test(script, variant, binary, args):
    """Run a test script on one variant. Return (passed, report lines)."""
    golden_dir = GOLDEN_DIR / script.name / variant
    fail_dir = args.build_dir / "e2e-failures" / script.name / variant
    shutil.rmtree(fail_dir, ignore_errors=True)
    notes = []

    with tempfile.TemporaryDirectory(prefix="openrtx-e2e-") as tmp:
        workdir = Path(tmp)
        output, problems = run_emulator(script, binary, workdir, args)

        if args.update_golden and problems:
            return False, [*problems, "goldens not updated"]

        for shot in script.screenshots:
            golden = golden_dir / (Path(shot).stem + ".png")
            problem, note = check_screenshot(
                workdir / shot, golden, fail_dir, args
            )
            problems += [problem] if problem else []
            notes += [note] if note else []

    if args.update_golden and not problems and golden_dir.is_dir():
        taken = {Path(shot).stem + ".png" for shot in script.screenshots}
        for old in sorted(golden_dir.glob("*.png")):
            if old.name not in taken:
                old.unlink()
                notes.append(f"removed {old.relative_to(PROJECT_ROOT)}")

    if problems and fail_dir.is_dir():
        problems.append(f"images saved to {fail_dir}")
    if problems and args.verbose:
        problems.append("emulator output:")
        problems += [f"    {line}" for line in output.splitlines()]

    return not problems, problems + notes


def parse_args():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "scripts", nargs="*", type=Path,
        help="test scripts to run (default: all of tests/e2e/*.txt)",
    )
    parser.add_argument(
        "--variant", action="append", choices=VARIANTS,
        help="run only on this variant, can be repeated (default: all)",
    )
    parser.add_argument(
        "--build-dir", type=Path, default=PROJECT_ROOT / "build_linux",
        help="meson build directory with the emulators (default:"
        " build_linux)",
    )
    parser.add_argument(
        "-j", "--jobs", type=int, default=os.cpu_count() or 1,
        help="number of tests to run in parallel (default: number of CPUs)",
    )
    parser.add_argument(
        "--tolerance", type=int, default=0,
        help="number of pixels that may differ (default: 0)",
    )
    parser.add_argument(
        "--timeout", type=int, default=60,
        help="seconds after which a test is stopped (default: 60)",
    )
    parser.add_argument(
        "--update-golden", action="store_true",
        help="store the screenshots as the new goldens instead of comparing"
        " them (goldens that still match are left untouched)",
    )
    parser.add_argument(
        "--wrapper", type=shlex.split, default=[],
        help="command to run the emulator with, between faketime and the"
        " emulator binary, e.g. 'valgrind --error-exitcode=1'",
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true",
        help="print the emulator output of failed tests",
    )
    args = parser.parse_args()
    args.build_dir = args.build_dir.resolve()
    return args


def collect_tests(args):
    """
    Parse the test scripts and return the (script, variant, binary) tuples to
    run. Exit if a script or an emulator binary cannot be used.
    """
    paths = args.scripts or sorted(E2E_DIR.glob("*.txt"))
    scripts = [TestScript(p.resolve()) for p in paths]
    errors = [f"{s.path.name}: {err}" for s in scripts for err in s.errors]

    tests = [
        (s, v, args.build_dir / VARIANTS[v])
        for s in scripts
        for v in s.variants
        if (not args.variant) or (v in args.variant)
    ]
    for binary in sorted({binary for _, _, binary in tests}):
        problem = check_binary(binary)
        errors += [problem] if problem else []

    if errors:
        sys.exit("\n".join(errors))

    return tests


def main():
    args = parse_args()
    if shutil.which("faketime") is None:
        sys.exit("faketime is required: apt install faketime")

    tests = collect_tests(args)
    failed = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        jobs = {
            pool.submit(run_test, s, v, binary, args): f"{s.name} ({v})"
            for s, v, binary in tests
        }
        for job in as_completed(jobs):
            name = jobs[job]
            passed, log = job.result()
            print(f"{'PASS' if passed else 'FAIL'}: {name}", flush=True)
            for line in log:
                print(f"    {line}", flush=True)
            if not passed:
                failed.append(name)

    print(f"\n{len(tests) - len(failed)} passed, {len(failed)} failed")
    for name in sorted(failed):
        print(f"    {name}")

    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
