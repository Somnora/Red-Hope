"""rh_sfx_build - every non-music sound in Red Hope, from code + CC0 picks.

  python3 scripts/audio/rh_sfx_build.py --kenney <dir with the 3 unpacked Kenney packs> \
      [--out Martians/gen/audio]

The game shipped silent until 2026-10-14. This writes 48 kHz / 16-bit WAVs into
<out>/<category>/<name>.wav plus <out>/manifest.json, which
scripts/unreal/rh_import_audio.py imports as SoundWaves under
/Game/RedHope/Audio/<category>/A_<name>.

Direction (docs/the-red-hope-design-brief.md, "Presentation Direction"):
"Thin-atmosphere sound design (muffled exterior, rich interior)" and a tone of
"Awe, isolation, then community". So the surface is a soft, low-passed wind
with no hard edges, and the habitat interior is warmer and busier - air
handlers, a mains hum, the odd structural tick.

Sources:
  - SYNTHESISED here (numpy/scipy, seeded - rebuilds are identical): the three
    ambience beds, the machine loops, the elevator chime, the milestone and
    alert stingers.
  - KENNEY (CC0, kenney.nl: Sci-Fi Sounds, Interface Sounds, Impact Sounds,
    downloaded 2026-10-14): UI clicks / confirm / error / cards, the elevator
    doors, the command module's computer chatter, the lander's idle engine,
    the borer's mining hits, the crew-arrival thruster.

Loops are made seamless by rendering a little long and crossfading the tail
into the head. Levels are set per category (RMS for beds and loops, peak for
one-shots) so the mix starts balanced; the game's volume dials do the rest.
"""
import argparse
import json
import os
import subprocess
import sys

import numpy as np
import soundfile as sf
from scipy import signal

SR = 48000
argv = sys.argv[1:]
ap = argparse.ArgumentParser()
ap.add_argument("--kenney", required=True)
ap.add_argument("--out", default="Martians/gen/audio")
args = ap.parse_args(argv)
OUT = os.path.abspath(args.out)
KEN = os.path.abspath(args.kenney)
RNG = np.random.default_rng(20261014)
MANIFEST = []


# ------------------------------------------------------------------ helpers
def t_axis(sec):
    return np.arange(int(sec * SR)) / SR


def white(n, ch=1):
    return RNG.standard_normal((n, ch)) if ch > 1 else RNG.standard_normal(n)


def brown(n):
    x = np.cumsum(RNG.standard_normal(n))
    x = signal.sosfilt(signal.butter(2, 18, "hp", fs=SR, output="sos"), x)
    return x / (np.abs(x).max() + 1e-9)


def pink(n):
    # Paul Kellet's economy pink filter
    b = [0.049922035, -0.095993537, 0.050612699, -0.004408786]
    a = [1, -2.494956002, 2.017265875, -0.522189400]
    return signal.lfilter(b, a, RNG.standard_normal(n))


def bp(x, lo, hi, order=2):
    return signal.sosfilt(signal.butter(order, [lo, hi], "bp", fs=SR, output="sos"), x, axis=0)


def lp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, f, "lp", fs=SR, output="sos"), x, axis=0)


def hp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, f, "hp", fs=SR, output="sos"), x, axis=0)


def lfo(t, rates, lo=0.0, hi=1.0):
    v = sum(np.sin(2 * np.pi * r * t + RNG.uniform(0, 2 * np.pi)) for r in rates) / len(rates)
    return lo + (hi - lo) * (v * 0.5 + 0.5)


def seamless(x, xfade_s):
    """x was rendered xfade_s too long: fold the tail over the head."""
    k = int(xfade_s * SR)
    body, tail = x[:-k].copy(), x[-k:]
    ramp = np.linspace(0, 1, k)
    if body.ndim == 2:
        ramp = ramp[:, None]
    body[:k] = body[:k] * ramp + tail * (1 - ramp)
    return body


def rms_db(x):
    return 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)


def to_rms(x, db):
    return x * (10 ** ((db - rms_db(x)) / 20))


def to_peak(x, db):
    return x * (10 ** (db / 20) / (np.abs(x).max() + 1e-12))


def limit(x, rms_target, ceiling_db=-3.0):
    """Level a loop to rms_target with a soft (tanh) ceiling, so a few spikes
    (forge crackle, drill thumps, bubbles) cannot drag the whole loop down
    through the anti-clip in write()."""
    c = 10 ** (ceiling_db / 20)
    for _ in range(6):
        x = to_rms(x, rms_target)
        x = np.tanh(x / c) * c
    return x


def env(n, a, d, sustain=0.0):
    e = np.ones(n)
    na, nd = int(a * SR), int(d * SR)
    e[:na] = np.linspace(0, 1, na) if na else 1
    if nd:
        e[-nd:] *= np.linspace(1, sustain, nd)
    return e


def write(cat, name, x, loop, note):
    d = os.path.join(OUT, cat)
    os.makedirs(d, exist_ok=True)
    peak = np.abs(x).max()
    if peak > 0.98:                      # never clip
        x = x * (0.98 / peak)
    p = os.path.join(d, name + ".wav")
    sf.write(p, x.astype(np.float32), SR, subtype="PCM_16")
    MANIFEST.append(dict(category=cat, name=name, file=p, loop=loop, note=note,
                         seconds=round(len(x) / SR, 3), channels=1 if x.ndim == 1 else x.shape[1],
                         rms_db=round(rms_db(x), 1), peak_db=round(20 * np.log10(np.abs(x).max() + 1e-12), 1)))
    print("%-12s %-22s %5.1fs  rms %6.1f dB  peak %6.1f dB  %s" % (
        cat, name, len(x) / SR, rms_db(x), 20 * np.log10(np.abs(x).max() + 1e-12), "loop" if loop else ""))


def kenney(rel):
    path = os.path.join(KEN, rel)
    raw = subprocess.check_output(["ffmpeg", "-v", "error", "-i", path, "-f", "f32le", "-ac", "1",
                                   "-ar", str(SR), "-"])
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64)


def stereo_from(fn, n):
    return np.stack([fn(n), fn(n)], axis=1)


# ------------------------------------------------------------- ambience beds
def amb_surface_wind():
    sec, xf = 60.0, 3.0
    t = t_axis(sec + xf)
    n = len(t)
    gust = lfo(t, [0.031, 0.067, 0.113], 0.3, 1.0)
    chans = []
    for _ in range(2):
        b = brown(n)
        dark, light = lp(b, 260), lp(b, 1000)
        w = dark * (1.15 - gust) + light * gust
        whistle = bp(RNG.standard_normal(n), 1300, 1750, 2) * gust ** 3 * 0.05
        chans.append(lp(w + whistle, 2400))
    x = np.stack(chans, axis=1) * gust[:, None]
    return to_rms(seamless(x, xf), -26)


def amb_habitat():
    sec, xf = 60.0, 3.0
    t = t_axis(sec + xf)
    n = len(t)
    breathe = 1 + 0.12 * np.sin(2 * np.pi * (1 / 12.5) * t)
    air = np.stack([bp(pink(n), 70, 2800), bp(pink(n), 70, 2800)], axis=1)
    air = to_rms(air * breathe[:, None], -31)
    hum = sum(a * np.sin(2 * np.pi * f * t) for f, a in ((55, 1.0), (110, 0.55), (165, 0.25), (220, 0.12)))
    hum = to_rms(hum * (1 + 0.03 * np.sin(2 * np.pi * 0.25 * t)), -41)
    x = air + hum[:, None]
    # structural ticks: sparse, resonant, panned
    tk = 4.0
    while tk < sec - 1:
        f = RNG.uniform(260, 900)
        burst = np.zeros(int(0.5 * SR))
        burst[0] = 1.0
        b, a = signal.iirpeak(f, 18, fs=SR)
        ring = signal.lfilter(b, a, burst) * np.exp(-np.arange(len(burst)) / (0.09 * SR))
        ring = to_peak(ring, -30)
        pan = RNG.uniform(0.15, 0.85)
        i = int(tk * SR)
        x[i:i + len(ring), 0] += ring * (1 - pan)
        x[i:i + len(ring), 1] += ring * pan
        tk += RNG.uniform(6.0, 15.0)
    return seamless(x, xf)


def amb_storm():
    sec, xf = 60.0, 3.0
    t = t_axis(sec + xf)
    n = len(t)
    gust = lfo(t, [0.05, 0.13, 0.21], 0.25, 1.0)
    chans = []
    for _ in range(2):
        b = brown(n)
        roar = lp(b, 300) * 0.8 + lp(b, 1600) * gust
        sand = hp(RNG.standard_normal(n), 2200) * gust ** 2 * 0.05
        chans.append(lp(roar + sand, 5000))
    x = np.stack(chans, axis=1) * (0.4 + 0.6 * gust[:, None])
    return to_rms(seamless(x, xf), -21)


# ------------------------------------------------------------- machine loops
def loop_len(sec):
    return t_axis(sec)


def mach_drill():
    sec, xf = 8.0, 0.5
    t = t_axis(sec + xf)
    n = len(t)
    f0 = 95.0
    vib = 1 + 0.004 * np.sin(2 * np.pi * 3.0 * t)
    motor = sum(a * np.sin(2 * np.pi * f0 * k * np.cumsum(vib) / SR) for k, a in ((1, 1), (2, .5), (3, .3), (5, .12)))
    auger = np.abs(np.sin(2 * np.pi * 1.5 * t)) ** 2
    grind = bp(RNG.standard_normal(n), 280, 950) * (0.35 + auger)
    thump = np.zeros(n)
    for s in np.arange(0, sec + xf, 0.5):
        i = int(s * SR)
        k = min(int(0.12 * SR), n - i)
        thump[i:i + k] += lp(RNG.standard_normal(k), 180) * np.exp(-np.arange(k) / (0.03 * SR)) * 4
    x = to_rms(motor, -26) + to_rms(grind, -24) + to_rms(thump, -27)
    return limit(seamless(x, xf), -20)


def mach_borer():
    sec, xf = 8.0, 0.5
    t = t_axis(sec + xf)
    n = len(t)
    rumble = lp(brown(n), 150) * (0.75 + 0.25 * np.abs(np.sin(2 * np.pi * 4.0 * t)))
    scrape = bp(RNG.standard_normal(n), 450, 1500) * lfo(t, [0.25, 0.5], 0.2, 1.0)
    x = to_rms(rumble, -21) + to_rms(scrape, -29)
    hits = [kenney("kenney_impact-sounds/Audio/impactMining_00%d.ogg" % i) for i in range(5)]
    s = 0.3
    while s < sec:
        h = to_peak(hits[RNG.integers(0, 5)], -14) * RNG.uniform(0.6, 1.0)
        i = int(s * SR)
        x[i:i + len(h)] += h[:max(0, min(len(h), n - i))]
        s += RNG.uniform(1.2, 2.0)
    return limit(seamless(x, xf), -19)


def mach_forge():
    sec, xf = 8.0, 0.5
    t = t_axis(sec + xf)
    n = len(t)
    flick = lfo(t, [0.27, 0.61], 0.7, 1.0)
    roar = lp(brown(n), 380) + 0.35 * bp(brown(n), 550, 1200)
    hum = np.sin(2 * np.pi * 60 * t) + 0.4 * np.sin(2 * np.pi * 120 * t)
    crackle = np.zeros(n)
    idx = RNG.choice(n, size=int(14 * (sec + xf)), replace=False)
    crackle[idx] = RNG.uniform(-1, 1, len(idx))
    crackle = hp(crackle, 1800) * 6
    x = to_rms(roar * flick, -21) + to_rms(hum, -36) + to_rms(crackle, -33)
    return limit(seamless(x, xf), -19)


def mach_electrolyzer():
    sec, xf = 8.0, 0.5
    t = t_axis(sec + xf)
    n = len(t)
    buzz = np.tanh(3 * np.sin(2 * np.pi * 100 * t)) * 0.6 + 0.4 * np.sin(2 * np.pi * 200 * t)
    bubbles = np.zeros(n)
    for _ in range(int(9 * (sec + xf))):
        i = RNG.integers(0, n - int(0.08 * SR))
        k = int(RNG.uniform(0.025, 0.06) * SR)
        f = RNG.uniform(380, 950)
        tt = np.arange(k) / SR
        chirp = np.sin(2 * np.pi * (f * tt + 0.5 * f * 6 * tt ** 2)) * np.exp(-tt / 0.015)
        bubbles[i:i + k] += chirp * RNG.uniform(0.3, 1.0)
    liquid = bp(RNG.standard_normal(n), 200, 900) * lfo(t, [0.4, 0.9], 0.4, 1.0)
    x = to_rms(buzz, -33) + to_rms(bubbles, -24) + to_rms(liquid, -31)
    return limit(seamless(x, xf), -21)


def mach_fan():
    sec, xf = 8.0, 0.5
    t = t_axis(sec + xf)
    n = len(t)
    wob = 1 + 0.06 * np.sin(2 * np.pi * 0.5 * t)
    whoosh = bp(pink(n), 140, 3800) * wob
    blade = np.sin(2 * np.pi * 96 * t) + 0.45 * np.sin(2 * np.pi * 192 * t) + 0.2 * np.sin(2 * np.pi * 288 * t)
    x = to_rms(whoosh, -21) + to_rms(blade, -31)
    return limit(seamless(x, xf), -20)


def mach_pump():
    sec, xf = 8.0, 0.5
    t = t_axis(sec + xf)
    n = len(t)
    hum = np.sin(2 * np.pi * 75 * t) + 0.3 * np.sin(2 * np.pi * 150 * t)
    stroke = np.zeros(n)
    slosh = np.zeros(n)
    for s in np.arange(0, sec + xf, 2.0):
        i = int(s * SR)
        k = min(int(0.6 * SR), n - i)
        tt = np.arange(k) / SR
        stroke[i:i + k] += np.sin(2 * np.pi * 58 * tt) * np.exp(-tt / 0.09)
        k2 = min(int(1.4 * SR), n - i)
        sw = np.sin(np.pi * np.arange(k2) / k2) ** 2
        slosh[i:i + k2] += bp(RNG.standard_normal(k2), 180, 700) * sw
    x = to_rms(hum, -30) + to_rms(stroke, -22) + to_rms(slosh, -27)
    return limit(seamless(x, xf), -20)


def mach_hum():
    sec, xf = 8.0, 0.5
    t = t_axis(sec + xf)
    beat = 1 + 0.08 * np.sin(2 * np.pi * 0.75 * t)
    x = (np.tanh(2.2 * np.sin(2 * np.pi * 100 * t)) + 0.35 * np.sin(2 * np.pi * 300 * t)) * beat
    x = x + 0.02 * hp(RNG.standard_normal(len(t)), 3000)
    return limit(seamless(x, xf), -24)


def kenney_loop(rel, xf, db, cut_hz=None):
    x = kenney(rel)
    if cut_hz:
        x = lp(x, cut_hz)
    return to_rms(seamless(x, xf), db)


# ------------------------------------------------------------ one-shots
def bell(freq, sec, partials=((1, 1.0, 1.0), (2.0, 0.5, 0.6), (2.76, 0.35, 0.45), (5.4, 0.15, 0.25))):
    t = t_axis(sec)
    return sum(a * np.sin(2 * np.pi * freq * r * t) * np.exp(-t / (d * sec * 0.5)) for r, a, d in partials)


def elev_chime():
    a = bell(659.25, 1.4)
    b = bell(554.37, 1.8)
    x = np.zeros(int(2.2 * SR))
    x[:len(a)] += a
    i = int(0.32 * SR)
    x[i:i + len(b)] += b
    return to_peak(lp(x, 6000), -6)


def evt_milestone():
    sec = 3.2
    t = t_axis(sec)
    chord = (220.0, 277.18, 329.63, 493.88)                  # A major add9, warm
    pad = sum(np.sin(2 * np.pi * f * t) + 0.3 * np.sin(2 * np.pi * 2 * f * t + 0.3) for f in chord)
    pad *= env(len(t), 0.45, 1.6) * (1 + 0.04 * np.sin(2 * np.pi * 5 * t))
    x = to_rms(lp(pad, 3500), -20)
    for k, f in enumerate((659.25, 880.0, 1108.73)):         # bell arpeggio on top
        b = bell(f, 1.6)
        i = int((0.35 + 0.16 * k) * SR)
        x[i:i + len(b)] += to_peak(b, -16)
    return to_peak(x, -4)


def evt_alert():
    seg = []
    for f in (880.0, 659.25, 880.0, 659.25):
        t = t_axis(0.15)
        tone = signal.square(2 * np.pi * f * t) * env(len(t), 0.005, 0.04, 0.6)
        seg.append(lp(tone, 2600))
        seg.append(np.zeros(int(0.04 * SR)))
    return to_peak(np.concatenate(seg), -11)


def kenney_shot(rel, db, fade_out=None, max_sec=None):
    x = kenney(rel)
    if max_sec:
        x = x[:int(max_sec * SR)]
    if fade_out:
        k = int(fade_out * SR)
        x[-k:] *= np.linspace(1, 0, k)
    return to_peak(x, db)


# ------------------------------------------------------------------ build
write("Ambience", "amb_surface_wind", amb_surface_wind(), True, "Mars surface: muffled thin-air wind (synth)")
write("Ambience", "amb_habitat", amb_habitat(), True, "habitat interior: air handlers, mains hum, structural ticks (synth)")
write("Ambience", "amb_storm", amb_storm(), True, "dust storm layer (synth)")

write("Machines", "mach_drill", mach_drill(), True, "IceDrill (synth)")
write("Machines", "mach_borer", mach_borer(), True, "Borer: rumble + scrape + Kenney impactMining hits")
write("Machines", "mach_forge", mach_forge(), True, "Forge: furnace roar + crackle (synth)")
write("Machines", "mach_electrolyzer", mach_electrolyzer(), True, "Electrolyzer: buzz + bubbles (synth)")
write("Machines", "mach_fan", mach_fan(), True, "AirFilter / HumidityRegulator fans (synth)")
write("Machines", "mach_pump", mach_pump(), True, "WaterPlant pump strokes (synth)")
write("Machines", "mach_hum", mach_hum(), True, "BatteryBank / power electronics hum (synth)")
write("Machines", "mach_computer", kenney_loop("kenney_sci-fi-sounds/Audio/computerNoise_002.ogg", 0.4, -24),
      True, "ComputeModule chatter (Kenney computerNoise_002)")
write("Machines", "mach_lander", kenney_loop("kenney_sci-fi-sounds/Audio/engineCircular_002.ogg", 0.4, -24, 1200),
      True, "Lander idle reactor hum (Kenney engineCircular_002, low-passed)")

write("Elevator", "elev_door_open", kenney_shot("kenney_sci-fi-sounds/Audio/doorOpen_001.ogg", -8), False, "Kenney doorOpen_001")
write("Elevator", "elev_door_close", kenney_shot("kenney_sci-fi-sounds/Audio/doorClose_001.ogg", -8), False, "Kenney doorClose_001")
write("Elevator", "elev_chime", elev_chime(), False, "two-tone arrival chime (synth)")

for name, rel, db in (("ui_click", "kenney_interface-sounds/Audio/select_001.ogg", -12),
                      ("ui_toggle", "kenney_interface-sounds/Audio/toggle_002.ogg", -12),
                      ("ui_confirm", "kenney_interface-sounds/Audio/confirmation_001.ogg", -9),
                      ("ui_error", "kenney_interface-sounds/Audio/error_007.ogg", -9),
                      ("ui_card_open", "kenney_interface-sounds/Audio/open_002.ogg", -10),
                      ("ui_card_close", "kenney_interface-sounds/Audio/close_002.ogg", -12)):
    write("UI", name, kenney_shot(rel, db), False, "Kenney " + os.path.basename(rel))

write("Events", "evt_notify", kenney_shot("kenney_interface-sounds/Audio/bong_001.ogg", -8), False, "Kenney bong_001 (toast)")
write("Events", "evt_milestone", evt_milestone(), False, "golden card: warm chord + bells (synth)")
write("Events", "evt_alert", evt_alert(), False, "alert: two-tone warning (synth)")
write("Events", "evt_build_done", kenney_shot("kenney_interface-sounds/Audio/confirmation_004.ogg", -8), False, "Kenney confirmation_004")
write("Events", "evt_arrival", kenney_shot("kenney_sci-fi-sounds/Audio/thrusterFire_001.ogg", -8, fade_out=1.2, max_sec=3.2),
      False, "crew lander touchdown (Kenney thrusterFire_001, trimmed)")

os.makedirs(OUT, exist_ok=True)
json.dump(MANIFEST, open(os.path.join(OUT, "manifest.json"), "w"), indent=1)
print("wrote %d sounds + manifest.json to %s" % (len(MANIFEST), OUT))
