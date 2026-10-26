#!/usr/bin/env python3
"""Writes the bundled looks of the Apply LUT effect (assets/luts/*.cube).

They are small (17 points per side) and made from simple formulas, so they are
free to ship and easy to change: edit a look below and run this again.
"""
import os

SIZE = 17
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'luts')


def clamp(v):
    return min(1.0, max(0.0, v))


def luma(r, g, b):
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def mix(a, b, t):
    return a + (b - a) * t


def scurve(x, k):
    # Contrast around the middle: k > 0 steeper, smooth at the ends.
    s = x * x * (3 - 2 * x)
    return mix(x, s, k)


def saturate(r, g, b, s):
    y = luma(r, g, b)
    return mix(y, r, s), mix(y, g, s), mix(y, b, s)


def teal_orange(r, g, b):
    y = luma(r, g, b)
    w = clamp((y - 0.25) / 0.5)  # 0 in the shadows, 1 in the highlights
    r, g, b = r + mix(-0.06, 0.07, w), g + mix(0.02, 0.01, w), b + mix(0.07, -0.08, w)
    r, g, b = (scurve(clamp(c), 0.35) for c in (r, g, b))
    return saturate(r, g, b, 1.12)


def warm(r, g, b):
    return scurve(clamp(r * 1.05 + 0.02), 0.15), clamp(g * 1.01 + 0.01), clamp(b * 0.9)


def cool(r, g, b):
    return clamp(r * 0.92), clamp(g * 0.99 + 0.01), scurve(clamp(b * 1.06 + 0.03), 0.15)


def bleach_bypass(r, g, b):
    r, g, b = saturate(r, g, b, 0.45)
    return tuple(scurve(clamp(c), 0.6) for c in (r, g, b))


def film_fade(r, g, b):
    r, g, b = saturate(r, g, b, 0.8)
    lift = lambda c, lo, hi: lo + scurve(clamp(c), 0.2) * (hi - lo)
    return lift(r, 0.09, 0.95), lift(g, 0.08, 0.93), lift(b, 0.07, 0.88)


LOOKS = {
    'Teal & Orange': teal_orange,
    'Warm': warm,
    'Cool': cool,
    'Bleach Bypass': bleach_bypass,
    'Film Fade': film_fade,
}


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, look in LOOKS.items():
        lines = [f'TITLE "{name}"', '# bemotion bundled look (tools/make-luts.py)', f'LUT_3D_SIZE {SIZE}', '']
        n = SIZE - 1
        for b in range(SIZE):  # red changes fastest
            for g in range(SIZE):
                for r in range(SIZE):
                    out = [clamp(c) for c in look(r / n, g / n, b / n)]
                    lines.append(' '.join(f'{c:.4f}' for c in out))
        with open(os.path.join(OUT, name + '.cube'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
