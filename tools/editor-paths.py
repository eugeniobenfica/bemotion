#!/usr/bin/env python3
"""How many of the editor's edits go through logic shared with operations (ARCHITECTURE §4).

The editor's tools change the project directly and call touch(); the gradual direction is to
move each edit's logic into the core (core/Editing, Clipboard, Arrange, Transitions, Presets,
Expressions), where operations use it too. This counts the touch() sites in src/ui and, for
each, whether a function of those core files is called in the lines just before it (in the
same function). A heuristic, for watching the trend, not a proof.

  tools/editor-paths.py            per file and in total
  tools/editor-paths.py --direct   also lists the direct sites (file:line)
"""
import glob
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SHARED_HEADERS = ['Editing', 'Clipboard', 'Arrange', 'Transitions', 'Presets', 'Expressions']
WINDOW = 12  # lines looked at before a touch()


def shared_functions():
    names = set()
    for h in SHARED_HEADERS:
        text = open(os.path.join(ROOT, 'src', 'core', h + '.h'), encoding='utf-8').read()
        # free functions: "<type> name(" at the start of a line, outside classes (no indentation)
        names.update(re.findall(r'^[A-Za-z_][\w:<>,\s\*&]*?\b([a-z]\w*)\(', text, re.M))
    return {n for n in names if len(n) > 3}


def main():
    names = shared_functions()
    call = re.compile(r'\b(' + '|'.join(sorted(names)) + r')\(')
    total = shared = 0
    direct_sites = []
    rows = []
    for path in sorted(glob.glob(os.path.join(ROOT, 'src', 'ui', '**', '*.cpp'), recursive=True)):
        lines = open(path, encoding='utf-8').read().split('\n')
        n = s = 0
        for i, line in enumerate(lines):
            if not re.search(r'\btouch\(\)', line) or 'void EditorContext::touch' in line:
                continue
            n += 1
            before = []
            for j in range(i, max(-1, i - WINDOW), -1):
                before.append(lines[j])
                if j != i and re.match(r'^\S.*\)\s*(const\s*)?\{$', lines[j]):  # the function's start
                    break
            if call.search('\n'.join(before)):
                s += 1
            else:
                direct_sites.append('%s:%d' % (os.path.relpath(path, ROOT).replace(os.sep, '/'), i + 1))
        if n:
            rows.append((os.path.relpath(path, ROOT).replace(os.sep, '/'), n, s))
            total += n
            shared += s
    width = max(len(r[0]) for r in rows)
    for name, n, s in sorted(rows, key=lambda r: -r[1]):
        print('%-*s  %3d edits, %3d shared' % (width, name, n, s))
    print('\n%d edits in the editor, %d (%d%%) through logic shared with operations' % (total, shared, round(100 * shared / max(1, total))))
    if '--direct' in sys.argv:
        print('\nDirect:')
        for site in direct_sites:
            print('  ' + site)


if __name__ == '__main__':
    main()
