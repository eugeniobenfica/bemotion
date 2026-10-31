#!/usr/bin/env python3
"""Measures what the README promises: a quick start, real-time playback, fast exports.

  tools/bench.py <build dir or bemotion executable> [--runs 3] [--json out.json]
                 [--max-startup S] [--max-frame-ms MS] [--min-export-fps FPS]

On the Welcome project (1920 x 1080, 30 fps, 5 s), with a scratch app data folder and Qt's
offscreen platform:
- startup: the editor opened on the project, first frame drawn and the app gone again
  (BEMOTION_CAPTURE), in seconds;
- frame: one frame drawn on the CPU from the command line (`bemotion render`, 12 moments),
  in milliseconds — under 33 ms is real time at 30 fps;
- export: an H.264 export at half size, in frames per second.
The median of --runs is reported. A --max / --min given makes the run fail past it (CI uses
generous ones: shared machines are slow and noisy). Standard library only.
"""
import argparse
import json
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def find_exe(path):
    if os.path.isfile(path):
        return path
    for dirpath, _, files in os.walk(path):
        for name in ('bemotion.exe', 'bemotion'):
            if name in files and dirpath.count(os.sep) - path.count(os.sep) <= 3:
                return os.path.join(dirpath, name)
    sys.exit('no bemotion executable in ' + path)


def timed(cmd, env):
    start = time.perf_counter()
    subprocess.run(cmd, env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
    return time.perf_counter() - start


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('build')
    ap.add_argument('--runs', type=int, default=3)
    ap.add_argument('--json')
    ap.add_argument('--max-startup', type=float)
    ap.add_argument('--max-frame-ms', type=float)
    ap.add_argument('--min-export-fps', type=float)
    args = ap.parse_args()
    exe = find_exe(args.build)

    work = tempfile.mkdtemp(prefix='bemotion-bench-')
    try:
        project = os.path.join(work, 'welcome.bemotion')
        shutil.copy(os.path.join(ROOT, 'assets', 'welcome', 'Welcome.bemotion'), project)
        comp = json.load(open(project, encoding='utf-8'))['composition']
        frames = int(round(comp['duration'] * comp['fps']))
        env = dict(os.environ, BEMOTION_APPDATA=os.path.join(work, 'appdata'), QT_QPA_PLATFORM='offscreen')
        os.makedirs(env['BEMOTION_APPDATA'])

        startup, frame_ms, export_fps = [], [], []
        moments = 12
        for i in range(args.runs):
            capture = os.path.join(work, 'start%d.png' % i)
            startup.append(timed([exe, project], dict(env, BEMOTION_CAPTURE=capture, BEMOTION_CAPTURE_FRAME='1')))
            sheet = os.path.join(work, 'sheet%d.png' % i)
            frame_ms.append(1000 * timed([exe, 'render', project, '--count', str(moments), '--out', sheet], env) / moments)
            video = os.path.join(work, 'export%d.mp4' % i)
            export_fps.append(frames / timed([exe, project, '--export', video, '--scale', '0.5'], env))
    finally:
        shutil.rmtree(work, ignore_errors=True)

    result = {
        'startup_s': round(statistics.median(startup), 3),
        'frame_ms': round(statistics.median(frame_ms), 1),
        'export_fps': round(statistics.median(export_fps), 1),
        'runs': args.runs,
    }
    print('startup      %6.2f s    (editor on the Welcome project, first frame)' % result['startup_s'])
    print('frame        %6.1f ms   (CPU, 1920 x 1080; real time at 30 fps: under 33)' % result['frame_ms'])
    print('export       %6.1f fps  (H.264, half size)' % result['export_fps'])
    if args.json:
        with open(args.json, 'w', encoding='utf-8') as f:
            json.dump(result, f, indent=2)

    failed = []
    if args.max_startup is not None and result['startup_s'] > args.max_startup:
        failed.append('startup over %.2f s' % args.max_startup)
    if args.max_frame_ms is not None and result['frame_ms'] > args.max_frame_ms:
        failed.append('frame over %.1f ms' % args.max_frame_ms)
    if args.min_export_fps is not None and result['export_fps'] < args.min_export_fps:
        failed.append('export under %.1f fps' % args.min_export_fps)
    if failed:
        print('FAILED: ' + ', '.join(failed))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
