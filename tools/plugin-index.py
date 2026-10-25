#!/usr/bin/env python3
"""Writes the official plugin catalog, assets/plugins/index.json: each plugin folder in
assets/plugins with its plugin.json fields and its files (path, size, SHA-256). bemotion
fetches it from GitHub when the user asks for official plugins, and checks every file it
downloads against it, so it must match the folders: run this after changing a plugin.

    tools/plugin-index.py           # write it
    tools/plugin-index.py --check   # fail when it is out of date (tests)
"""
import hashlib
import json
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parent.parent / "assets" / "plugins"
index_file = root / "index.json"


def catalog():
    plugins = []
    for folder in sorted(p for p in root.iterdir() if (p / "plugin.json").is_file()):
        manifest = json.loads((folder / "plugin.json").read_text(encoding="utf-8"))
        files = []
        for f in sorted(p for p in folder.rglob("*") if p.is_file()):
            data = f.read_bytes()
            files.append({"path": f.relative_to(folder).as_posix(), "size": len(data),
                          "sha256": hashlib.sha256(data).hexdigest()})
        plugins.append({key: manifest[key] for key in
                        ("id", "name", "version", "author", "description", "api", "permissions") if key in manifest}
                       | {"files": files})
    return {"catalog": 1, "plugins": plugins}


text = json.dumps(catalog(), indent=2, ensure_ascii=False) + "\n"
if "--check" in sys.argv:
    current = index_file.read_text(encoding="utf-8").replace("\r\n", "\n") if index_file.exists() else ""
    if current != text:
        print("assets/plugins/index.json is out of date: run tools/plugin-index.py")
        sys.exit(1)
    print("plugin catalog up to date")
else:
    index_file.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {index_file}")
