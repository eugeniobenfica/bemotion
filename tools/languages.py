#!/usr/bin/env python3
"""The interface's texts and the language files (assets/languages/*.json).

  languages.py --check            every language file has every text, with the same placeholders
                                  (run by ctest; fails listing what is missing)
  languages.py --missing pt-BR    the texts that language does not have yet, as JSON to fill in
  languages.py --list             every text found

Texts are found in src/: literals in tr("..."), N_("...") (the core's tables) and
TR_NOOP("...") (the interface's tables), and the names and categories in the JSON of the
built-in presets.
"""
import glob
import io
import json
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
LANGS = os.path.join(ROOT, 'assets', 'languages')

MARK = re.compile(r'\b(?:tr|N_|TR_NOOP|QT_TR_NOOP)\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)[,)]')
LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
PRESET = re.compile(r'"(?:name|category)"\s*:\s*"([^"]+)"')
PLACEHOLDER = re.compile(r'%\d{1,2}|%n|\{[A-Za-z0-9_]+\}')


def unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == '\\' and i + 1 < len(s):
            n = s[i + 1]
            out.append({'n': '\n', 't': '\t', '"': '"', '\\': '\\', "'": "'"}.get(n, '\\' + n))
            i += 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def strip_comments(src):
    # Line comments (outside strings) and block comments go; strings stay.
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith('//', i):
            j = src.find('\n', i)
            i = n if j < 0 else j
        elif src.startswith('/*', i):
            j = src.find('*/', i)
            i = n if j < 0 else j + 2
        elif src[i] == '"':
            j = i + 1
            while j < n and src[j] != '"':
                j += 2 if src[j] == '\\' else 1
            out.append(src[i:j + 1])
            i = j + 1
        elif src[i] == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == '\\' else 1
            out.append(src[i:j + 1])
            i = j + 1
        else:
            out.append(src[i])
            i += 1
    return ''.join(out)


def texts():
    found = {}
    files = glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True) + \
        glob.glob(os.path.join(ROOT, 'src', '**', '*.h'), recursive=True)
    for f in sorted(files):
        raw = io.open(f, encoding='utf-8').read()
        src = strip_comments(raw)
        rel = os.path.relpath(f, ROOT).replace('\\', '/')
        for m in MARK.finditer(src):
            text = unescape(''.join(LIT.findall(m.group(1))))
            if text.strip():
                found.setdefault(text, rel)
        if rel.endswith(('core/Presets.cpp', 'core/Effects.cpp')):
            for m in PRESET.finditer(raw):
                found.setdefault(m.group(1), rel)
    return found


def languages():
    out = {}
    for f in sorted(glob.glob(os.path.join(LANGS, '*.json'))):
        with io.open(f, encoding='utf-8') as fh:
            out[os.path.basename(f)] = json.load(fh)
    return out


def main(argv):
    sys.stdout.reconfigure(encoding='utf-8')  # the texts are UTF-8 whatever the console
    found = texts()
    if '--list' in argv:
        for t in sorted(found):
            print(json.dumps(t, ensure_ascii=False))
        return 0
    if '--missing' in argv:
        code = argv[argv.index('--missing') + 1]
        lang = languages().get(code + '.json', {'strings': {}})
        missing = {t: '' for t in sorted(found) if not lang['strings'].get(t)}
        print(json.dumps(missing, ensure_ascii=False, indent=1))
        return 0
    problems = 0
    for name, lang in languages().items():
        strings = lang.get('strings', {})
        if lang.get('format') != 'bemotion-language' or not lang.get('code'):
            print(f'{name}: not a bemotion language file (format, code)')
            problems += 1
            continue
        missing = [t for t in sorted(found) if not strings.get(t)]
        for t in missing:
            print(f'{name}: missing {json.dumps(t, ensure_ascii=False)} ({found[t]})')
        problems += len(missing)
        for english, translation in strings.items():
            if sorted(PLACEHOLDER.findall(english)) != sorted(PLACEHOLDER.findall(translation)):
                print(f'{name}: placeholders differ: {json.dumps(english, ensure_ascii=False)} -> {json.dumps(translation, ensure_ascii=False)}')
                problems += 1
        unused = [t for t in strings if t not in found]
        if unused and '--verbose' in argv:
            for t in unused:
                print(f'{name}: no longer used {json.dumps(t, ensure_ascii=False)}')
    if problems:
        print(f'{problems} problem(s). Add the texts to the language files (languages.py --missing <code>).')
        return 1
    print(f'{len(found)} texts, every language complete.')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
