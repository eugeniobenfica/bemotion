#!/usr/bin/env python3
"""Interface scenarios: the app driven by a script (app/DevScript.h) without a screen, and what
its window showed checked from the captures. The smoke test runs it on every CI platform.

    tools/ui-test.py <bemotion executable> [output dir]

Each scenario makes a project from the command line, plays a list of steps in the editor
(keys, mouse, waits) and captures the window as BMP; checks count pixels of a color in the
viewport, so they hold whatever the fonts and the window size. Never touches the user's
settings: every run gets an app data folder of its own. Standard library only.
"""
import json
import os
import pathlib
import struct
import subprocess
import sys
import tempfile

ACCENT = (0xE8, 0xB9, 0x31)  # the shape's fill in the scenarios (the brand gold)


def read_bmp(path):
    """(width, height, rows of (r, g, b)), top row first. 24 or 32 bits, uncompressed."""
    data = pathlib.Path(path).read_bytes()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    step = bpp // 8
    stride = (width * step + 3) & ~3
    rows = []
    for y in range(abs(height)):
        start = offset + y * stride
        row = data[start:start + width * step]
        rows.append([(row[x + 2], row[x + 1], row[x]) for x in range(0, width * step, step)])
    if height > 0:
        rows.reverse()  # stored bottom-up
    return width, abs(height), rows


def count(path, color, tolerance=24):
    _, _, rows = read_bmp(path)
    return sum(1 for row in rows for p in row if all(abs(a - b) <= tolerance for a, b in zip(p, color)))


def run(exe, out, name, ops, steps):
    folder = out / name
    folder.mkdir(parents=True, exist_ok=True)
    appdata = folder / "appdata"
    appdata.mkdir(exist_ok=True)
    (appdata / "settings.json").write_text(json.dumps({"experience": "expert"}))  # no first-run screen
    env = dict(os.environ, BEMOTION_APPDATA=str(appdata), QT_QPA_PLATFORM="offscreen")
    project = folder / "p.bemotion"
    subprocess.run([exe, "new", str(project)], env=env, check=True, capture_output=True)
    (folder / "ops.json").write_text(json.dumps(ops))
    subprocess.run([exe, "apply", str(project), str(folder / "ops.json")], env=env, check=True, capture_output=True)
    script = ["resize 1600 900", "wait 90"]
    for s in steps:
        script.append(s.replace("@", str(folder) + os.sep))
    script.append("quit")
    (folder / "steps.txt").write_text("\n".join(script) + "\n")
    env["BEMOTION_SCRIPT"] = str(folder / "steps.txt")
    subprocess.run([exe, str(project)], env=env, timeout=180, capture_output=True)
    return folder


def key(name):
    return [f"key {name} down", f"key {name} up", "wait 5"]


def ctrl(name):
    return ["key Ctrl down", f"key {name} down", f"key {name} up", "key Ctrl up", "wait 5"]


def scenario_edits_and_undo(exe, out):
    """Played frames are kept; an edit must never show a kept frame from before it, and undo
    brings the picture back (core/Changes, core/History through the editor)."""
    ops = [{"op": "layer.add", "type": "rectangle", "name": "Card", "duration": 10, "position": [960, 540]},
           {"op": "property.set", "layer": "Card", "prop": "size", "value": [1200, 700]},
           {"op": "property.set", "layer": "Card", "prop": "fill_color", "value": "#E8B931"},
           {"op": "layer.add", "type": "ellipse", "name": "Late", "start": 6, "duration": 3}]
    steps = (["move 800 300 3"] + key("Space") + ["wait 40"] + key("Space") + key("Home") + ["wait 10", "capture @before.bmp"]
             + ctrl("A") + key("Delete") + key("Right") + ["wait 10", "capture @deleted.bmp"]
             + ctrl("Z") + ["wait 10", "capture @undone.bmp"])
    folder = run(exe, out, "edits-and-undo", ops, steps)
    shots = {n: folder / f"{n}.bmp" for n in ("before", "deleted", "undone")}
    missing = [n for n, p in shots.items() if not p.exists()]
    if missing:
        return "no capture: " + ", ".join(missing)
    before, deleted, undone = (count(shots[n], ACCENT) for n in ("before", "deleted", "undone"))
    # The card covers a large part of the viewport; the interface's own gold (a button, the
    # playhead) is far less.
    if before < 20000:
        return f"the card is not shown ({before} gold pixels)"
    if deleted > before // 4:
        return f"deleted, the card still shows ({deleted} gold pixels): a kept frame from before the edit"
    if undone < before * 3 // 4:
        return f"undo did not bring the card back ({undone} gold pixels)"
    return None


SCENARIOS = [("edits and undo", scenario_edits_and_undo)]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    exe = sys.argv[1]
    out = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else tempfile.mkdtemp(prefix="bemotion-ui-")).resolve()
    failed = 0
    for name, test in SCENARIOS:
        try:
            problem = test(exe, out)
        except (subprocess.SubprocessError, OSError) as e:
            problem = f"could not run: {e}"
        print(f"  {name}: " + ("ok" if problem is None else "FAILED, " + problem))
        failed += problem is not None
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
