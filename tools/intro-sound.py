"""Makes the intro's sound (assets/welcome/Intro.wav), timed to Welcome.bemotion.

    python tools/intro-sound.py

Synthesized, so it is ours to ship: no samples, no libraries. Calm on purpose
(peaks near -8 dBFS): an airy rise while the sparks gather, a soft low thump and
a bell chord when the mark lands (0.86 s), a breath as it slides (1.95 s), two
plucks for the wordmark (2.5 s) and the tagline (3.15 s), a warm pad under it
all, gone with the picture's fade (4.45 to 5 s).
"""

import math
import random
import struct
import sys
import wave
from pathlib import Path

RATE = 48000
LENGTH = 5.0
N = int(RATE * LENGTH)
random.seed(7)

left = [0.0] * N
right = [0.0] * N


def note(f):  # semitones from A4 -> Hz
    return 440.0 * 2 ** (f / 12.0)


def add(start, seconds, sample, pan=0.0, gain=1.0):
    """Mixes sample(t) (t: seconds from `start`) in, panned -1..1."""
    a = max(0, int(start * RATE))
    b = min(N, int((start + seconds) * RATE))
    gl = gain * math.cos((pan + 1) * math.pi / 4)
    gr = gain * math.sin((pan + 1) * math.pi / 4)
    for i in range(a, b):
        v = sample((i - a) / RATE)
        left[i] += v * gl
        right[i] += v * gr


def bell(freq, start, decay, gain, pan):
    # A few inharmonic partials, each fading at its own pace: a soft glassy bell.
    partials = [(1.0, 1.0), (2.01, 0.35), (2.99, 0.12), (4.2, 0.05)]
    def s(t):
        attack = min(1.0, t / 0.004)
        return attack * sum(g * math.exp(-t / (decay / (k ** 0.7))) * math.sin(2 * math.pi * freq * k * t) for k, g in partials)
    add(start, decay * 5, s, pan, gain)


def pluck(freq, start, gain, pan):
    def s(t):
        attack = min(1.0, t / 0.003)
        env = math.exp(-t / 0.45)
        return attack * env * (math.sin(2 * math.pi * freq * t) + 0.3 * math.sin(4 * math.pi * freq * t) * math.exp(-t / 0.12))
    add(start, 2.0, s, pan, gain)


def noise_swell(start, seconds, gain, pan, rise=True, bright=0.08):
    # Noise through a one-pole low-pass whose opening follows the envelope (a whoosh).
    state = [0.0]
    def s(t):
        u = t / seconds
        env = math.sin(math.pi * (u ** (1.6 if rise else 0.6))) ** 2
        k = bright * (0.25 + 0.75 * (u if rise else 1 - u))
        state[0] += k * (random.uniform(-1, 1) - state[0])
        return env * state[0] * 3.0
    add(start, seconds, s, pan, gain)


# 0 - 0.86: the sparks gather: an airy rise, and a shimmer climbing toward the impact.
noise_swell(0.0, 0.9, 0.22, -0.3)
noise_swell(0.05, 0.85, 0.22, 0.3)
def shimmer(t):
    u = min(1.0, t / 0.86)
    f = note(3) * (1 + 1.0 * u * u)  # C5 sliding up an octave
    return (u ** 2) * math.sin(2 * math.pi * f * t) * 0.5 * (1 - max(0.0, (t - 0.8) / 0.08))
add(0.0, 0.88, shimmer, 0.0, 0.05)

# 0.86: the mark lands: a low thump, a short click of noise, a bell chord (C major 9, open).
def thump(t):
    f = 90 * math.exp(-t * 9) + 46
    phase = 2 * math.pi * (46 * t + (90 / 9) * (1 - math.exp(-t * 9)))
    return math.sin(phase) * math.exp(-t / 0.22) * min(1.0, t / 0.002)
add(0.86, 1.2, thump, 0.0, 0.55)
noise_swell(0.86, 0.06, 0.12, 0.0, rise=False, bright=0.5)
for semis, pan, gain in [(-9, -0.2, 0.18), (-2, 0.25, 0.13), (3, -0.35, 0.12), (7, 0.4, 0.09), (14, 0.0, 0.05)]:
    bell(note(semis), 0.86, 0.9, gain, pan)

# A warm pad under the rest (C, G, E, B, D), in from the impact, out with the picture.
def pad(t):
    end = 5.0 - 0.86
    env = min(1.0, t / 0.6) * min(1.0, max(0.0, (end - t) / 0.55))
    v = 0.0
    for semis, g in [(-21, 0.5), (-14, 0.35), (-5, 0.25), (2, 0.15), (5, 0.12)]:
        f = note(semis)
        v += g * (math.sin(2 * math.pi * f * t + 0.3 * math.sin(2 * math.pi * 0.25 * t)) + 0.2 * math.sin(2 * math.pi * f * 2.003 * t))
    return env * v
add(0.86, 5.0 - 0.86, pad, -0.15, 0.045)
add(0.86, 5.0 - 0.86, lambda t: pad(t + 0.37), 0.15, 0.045)

# 1.95: the mark slides aside: a breath.
noise_swell(1.9, 0.7, 0.10, -0.4, rise=False, bright=0.05)
# 2.5 and 3.15: the wordmark, then the tagline.
pluck(note(10), 2.5, 0.13, 0.2)    # G5
pluck(note(15), 3.15, 0.10, -0.2)  # C6
bell(note(22), 3.18, 0.5, 0.025, 0.3)

# Out with the picture's fade (4.45 - 5 s), then peaks brought to -8 dBFS.
for i in range(N):
    t = i / RATE
    if t > 4.45:
        g = max(0.0, 1 - (t - 4.45) / 0.55) ** 2
        left[i] *= g
        right[i] *= g
peak = max(max(abs(v) for v in left), max(abs(v) for v in right), 1e-9)
scale = 10 ** (-8 / 20) / peak

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "assets" / "welcome" / "Intro.wav"
with wave.open(str(out), "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(RATE)
    frames = bytearray()
    for l, r in zip(left, right):
        frames += struct.pack("<hh", int(max(-1, min(1, l * scale)) * 32767), int(max(-1, min(1, r * scale)) * 32767))
    w.writeframes(bytes(frames))
print("wrote", out)
