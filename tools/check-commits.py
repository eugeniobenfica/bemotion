#!/usr/bin/env python3
"""Checks the commits of a change the way CONTRIBUTING.md asks for them.

  tools/check-commits.py <base>..<head>

Fails when a subject is longer than 72 characters, ends with a period, or is not followed by
a blank line, or when a commit has no body (what changed and why). Warns (without failing)
when a commit looks like several changes at once: many files in several areas, or a subject
listing three things or more. Merge commits and git subtree's squashes are skipped. Standard library only.
"""
import re
import subprocess
import sys

AREAS = ('src/core', 'src/render', 'src/media', 'src/export', 'src/ui', 'src/app', 'libs', 'assets/plugins', 'tools')


def git(*args):
    return subprocess.run(['git', *args], check=True, capture_output=True, text=True, encoding='utf-8').stdout


def main():
    if len(sys.argv) != 2 or '..' not in sys.argv[1]:
        sys.exit(__doc__)
    failed = False
    for sha in git('rev-list', '--no-merges', '--reverse', sys.argv[1]).split():
        message = git('log', '-1', '--format=%B', sha).rstrip('\n').split('\n')
        subject = message[0]
        if subject.startswith("Squashed '"):  # git subtree's own (libs/bestyle)
            continue
        problems, notes = [], []
        if len(subject) > 72:
            problems.append('subject over 72 characters')
        if subject.endswith('.'):
            problems.append('subject ends with a period')
        if len(message) > 1 and message[1].strip():
            problems.append('no blank line after the subject')
        body = [l for l in message[2:] if l.strip() and not re.match(r'^[\w-]+: ', l)]  # trailers aside
        if not body:
            problems.append('no body saying what changed and why')
        files = [f for f in git('show', '--name-only', '--format=', sha).split('\n') if f]
        areas = {a for f in files for a in AREAS if f.startswith(a + '/')}
        if len(files) > 25 and len(areas) > 2:
            notes.append('%d files in %d areas: one change?' % (len(files), len(areas)))
        if subject.count(',') >= 2:
            notes.append('the subject lists several things: one change per commit')
        for p in problems:
            print('%s %s: %s' % (sha[:8], subject[:50], p))
        for n in notes:
            print('%s %s: note: %s' % (sha[:8], subject[:50], n))
        failed = failed or bool(problems)
    print('commits: ' + ('FAILED' if failed else 'ok'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
