#!/usr/bin/env python3
"""Golden-image render tests: what the renderer draws, kept as PNGs to compare against.

    tools/golden.py <bemotion executable> [--out <folder>]   compare (ctest runs this)
    tools/golden.py <bemotion executable> --update           draw the goldens again, on purpose

Renders a contact sheet (6 moments) of every project in examples/, of the Welcome project,
of the old files in tests/files/ and of the fuzzer's projects (tests/corpus/project/) with
`bemotion render` (the CPU renderer), and compares it with tests/golden/<name>.png. Text is
drawn by each system's font engine, so the comparison forgives antialiasing: both images are
averaged over 4x4 blocks first, and a block counts as different only past a small threshold.
Where a system still draws a project differently, its own golden goes in
tests/golden/<system>/<name>.png (system: windows, linux, macos), copied from the renders
the failed run kept. Run --update only when a change to the drawing is meant, look at the
new PNGs, and commit them with the change.
Standard library only (a small PNG reader below).
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GOLDEN = os.path.join(ROOT, "tests", "golden")
BLOCK = 4          # pixels averaged together before comparing
THRESHOLD = 24     # a block differs past this on any channel (0..255)
MAX_DIFFERENT = 0.005  # fraction of blocks allowed to differ
SYSTEM = {"win32": "windows", "darwin": "macos"}.get(sys.platform, "linux")


def projects():
    found = []
    for folder in (os.path.join(ROOT, "examples"), os.path.join(ROOT, "tests", "files"),
                   os.path.join(ROOT, "tests", "corpus", "project")):
        if os.path.isdir(folder):
            found += [os.path.join(folder, f) for f in sorted(os.listdir(folder)) if f.endswith(".bemotion")]
    found.append(os.path.join(ROOT, "assets", "welcome", "Welcome.bemotion"))
    return found


def golden_for(name):
    """This system's own golden when there is one, else the shared one."""
    own = os.path.join(GOLDEN, SYSTEM, name + ".png")
    return own if os.path.isfile(own) else os.path.join(GOLDEN, name + ".png")


def read_png(path):
    """(width, height, rows of RGBA bytes) for 8-bit, non-interlaced PNGs."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(path + ": not a PNG")
    pos, idat, width = 8, b"", 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if depth != 8 or interlace or ctype not in (0, 2, 4, 6):
                raise ValueError(path + ": unsupported PNG (depth %d, type %d)" % (depth, ctype))
            channels = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
        elif kind == b"IDAT":
            idat += chunk
        elif kind == b"IEND":
            break
    raw = zlib.decompress(idat)
    stride = width * channels
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(height):
        filt, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = prev[x]
            c = prev[x - channels] if x >= channels else 0
            if filt == 1:
                line[x] = (line[x] + a) & 255
            elif filt == 2:
                line[x] = (line[x] + b) & 255
            elif filt == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif filt == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    if channels != 4:  # to RGBA
        out = []
        for line in rows:
            rgba = bytearray()
            for x in range(width):
                px = line[x * channels:(x + 1) * channels]
                if channels == 1:
                    rgba += bytes((px[0], px[0], px[0], 255))
                elif channels == 2:
                    rgba += bytes((px[0], px[0], px[0], px[1]))
                else:
                    rgba += px + b"\xff"
            out.append(rgba)
        rows = out
    return width, height, rows


def blocks(width, height, rows):
    """The image averaged over BLOCK x BLOCK squares: a list of (r, g, b, a)."""
    out = []
    for by in range(0, height - BLOCK + 1, BLOCK):
        sums = [[0, 0, 0, 0] for _ in range(width // BLOCK)]
        for y in range(by, by + BLOCK):
            line = rows[y]
            for bx in range(width // BLOCK):
                s = sums[bx]
                for x in range(bx * BLOCK * 4, (bx + 1) * BLOCK * 4, 4):
                    s[0] += line[x]
                    s[1] += line[x + 1]
                    s[2] += line[x + 2]
                    s[3] += line[x + 3]
        n = BLOCK * BLOCK
        out += [tuple(v // n for v in s) for s in sums]
    return out


def compare(actual, expected):
    """Empty when the images match closely enough, else what differs."""
    with open(actual, "rb") as fa, open(expected, "rb") as fe:
        if fa.read() == fe.read():  # the usual case on the system that drew the goldens
            return ""
    a, e = read_png(actual), read_png(expected)
    if a[:2] != e[:2]:
        return "size %dx%d, expected %dx%d" % (a[0], a[1], e[0], e[1])
    ba, be = blocks(*a), blocks(*e)
    different = sum(1 for p, q in zip(ba, be) if max(abs(x - y) for x, y in zip(p, q)) > THRESHOLD)
    if different > MAX_DIFFERENT * len(ba):
        return "%d of %d blocks differ (%.2f%%, allowed %.2f%%)" % (
            different, len(ba), 100.0 * different / len(ba), 100.0 * MAX_DIFFERENT)
    return ""


def render(exe, project, png):
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    env.setdefault("BEMOTION_APPDATA", tempfile.mkdtemp(prefix="bemotion-golden-"))  # never the user's settings
    result = subprocess.run([exe, "render", project, "--count", "6", "--out", png],
                            env=env, capture_output=True, text=True)
    if result.returncode != 0 or not os.path.isfile(png):
        return "render failed (exit code %d): %s" % (result.returncode, (result.stderr or result.stdout).strip())
    return ""


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("exe", help="the bemotion executable")
    parser.add_argument("--update", action="store_true", help="write the goldens again")
    parser.add_argument("--out", help="where to keep this run's renders (default: a temporary folder)")
    args = parser.parse_args()
    out = args.out or tempfile.mkdtemp(prefix="bemotion-golden-out-")
    os.makedirs(out, exist_ok=True)
    os.makedirs(GOLDEN, exist_ok=True)
    failed = 0
    for project in projects():
        name = os.path.splitext(os.path.basename(project))[0]
        golden = golden_for(name)
        png = golden if args.update else os.path.join(out, name + ".png")
        problem = render(args.exe, project, png)
        if not problem and not args.update:
            problem = compare(png, golden) if os.path.isfile(golden) else "no golden (run with --update)"
        print("[%s] %s%s" % ("FAIL" if problem else " ok ", name, ": " + problem if problem else ""))
        failed += bool(problem)
    if failed and not args.update:
        print("renders kept in " + out)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
