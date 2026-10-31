#!/usr/bin/env python3
"""Fails when an operation has no test: every name in Ops.cpp's table (kOps) must be used,
quoted, in tests/*.cpp or tools/mcp-test.py. Operations are what scripts, plugins and AI
assistants rely on, so a new one comes with its test (AGENTS.md). ctest runs it.

    tools/ops-tested.py
"""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
ops = re.findall(r'\{"([a-z_.]+)", &Applier::', (root / "src/core/Ops.cpp").read_text(encoding="utf-8"))
if not ops:
    print("no operations found in src/core/Ops.cpp (did the table change shape?)")
    sys.exit(1)
sources = [p.read_text(encoding="utf-8", errors="replace") for p in (root / "tests").glob("*.cpp")]
sources.append((root / "tools/mcp-test.py").read_text(encoding="utf-8"))
text = "\n".join(sources)
missing = [op for op in ops if f'"{op}"' not in text]
if missing:
    print("operations without a test: " + ", ".join(missing))
    sys.exit(1)
print(f"all {len(ops)} operations are tested")
