#!/usr/bin/env python3
"""KAUPUNKI sound pack generator (task T07).

Synthesises every effect and ambient loop of DESIGN section 10 into sfx/<name>.wav:
PCM mono 22050 Hz 16-bit. Effects are peak-normalised to -3 dBFS with 4 ms fades;
ambient loops are seamless (tail crossfaded into the head, loop-periodic LFOs/tones)
and normalised to -6 dBFS. Relative loudness is set by the base volumes in src/snd.cpp.
Python 3 standard library only. Deterministic: every sound uses its own random.Random
seeded from a CRC of its name, so reruns give byte-identical files.

DSP helpers adapted from SIROCCO's tools/make_sfx.py (../sandline, task T04).

Usage:  python3 tools/make_sfx.py [--out DIR] [--report] [--only NAME]
        --report prints peak/RMS per file and the loop seam checks; exit code 1 if a check fails.
"""
import math
import os
import random
import struct
import sys
import wave
import zlib

SR = 22050
SFX_PEAK_DB = -3.0
AMB_PEAK_DB = -6.0
FADE_S = 0.004
TWO_PI = 2.0 * math.pi


# --------------------------------------------------------------------------
# DSP helpers (lists of floats, roughly -1..1)
# --------------------------------------------------------------------------

def ns(sec):
    return int(round(sec * SR))


def noise(rng, sec):
    return [rng.uniform(-1.0, 1.0) for _ in range(ns(sec))]


def tone(freq, sec, shape="sine", phase=0.0):
    """freq may be a number or a function t->Hz (sweeps)."""
    n = ns(sec)
    out = [0.0] * n
    ph = phase
    f = freq if callable(freq) else (lambda t, c=freq: c)
    for i in range(n):
        p = ph - math.floor(ph)
        if shape == "sine":
            v = math.sin(TWO_PI * p)
        elif shape == "square":
            v = 1.0 if p < 0.5 else -1.0
        elif shape == "saw":
            v = 2.0 * p - 1.0
        elif shape == "tri":
            v = 4.0 * p - 1.0 if p < 0.5 else 3.0 - 4.0 * p
        else:
            raise ValueError(shape)
        out[i] = v
        ph += f(i / SR) / SR
    return out


def env_exp(sec, tau, attack=0.0):
    n = ns(sec)
    a = ns(attack)
    out = [0.0] * n
    for i in range(n):
        e = math.exp(-(i / SR) / tau)
        if i < a:
            e *= i / a
        out[i] = e
    return out


def env_adsr(sec, a, d, s_level, r):
    n = ns(sec)
    out = [0.0] * n
    na, nd, nr = ns(a), ns(d), ns(r)
    for i in range(n):
        if i < na:
            v = i / max(1, na)
        elif i < na + nd:
            v = 1.0 - (1.0 - s_level) * (i - na) / max(1, nd)
        else:
            v = s_level
        rem = n - i
        if rem < nr:
            v *= rem / max(1, nr)
        out[i] = v
    return out


def env_bump(sec, centre, width):
    """Smooth raised-cosine bump (0..1..0) centred at `centre`, total width `width`."""
    n = ns(sec)
    out = [0.0] * n
    for i in range(n):
        u = (i / SR - centre) / (width * 0.5)
        if -1.0 < u < 1.0:
            out[i] = 0.5 + 0.5 * math.cos(math.pi * u)
    return out


def mul(a, b):
    n = min(len(a), len(b))
    return [a[i] * b[i] for i in range(n)]


def mix(length_sec, *parts):
    """parts: (signal, offset_sec, gain). Result length = length_sec."""
    n = ns(length_sec)
    out = [0.0] * n
    for sig, off, g in parts:
        o = ns(off)
        for i, v in enumerate(sig):
            j = o + i
            if j >= n:
                break
            if j >= 0:
                out[j] += v * g
    return out


class Biquad:
    """RBJ cookbook biquad."""

    def __init__(self, kind, f0, q=0.707):
        w0 = TWO_PI * min(f0, SR * 0.45) / SR
        cw, sw = math.cos(w0), math.sin(w0)
        alpha = sw / (2.0 * q)
        if kind == "lp":
            b0, b1, b2 = (1 - cw) / 2, 1 - cw, (1 - cw) / 2
        elif kind == "hp":
            b0, b1, b2 = (1 + cw) / 2, -(1 + cw), (1 + cw) / 2
        elif kind == "bp":  # constant 0 dB peak gain
            b0, b1, b2 = alpha, 0.0, -alpha
        else:
            raise ValueError(kind)
        a0, a1, a2 = 1 + alpha, -2 * cw, 1 - alpha
        self.b0, self.b1, self.b2 = b0 / a0, b1 / a0, b2 / a0
        self.a1, self.a2 = a1 / a0, a2 / a0

    def run(self, x):
        b0, b1, b2, a1, a2 = self.b0, self.b1, self.b2, self.a1, self.a2
        x1 = x2 = y1 = y2 = 0.0
        out = [0.0] * len(x)
        for i, v in enumerate(x):
            y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
            x2, x1, y2, y1 = x1, v, y1, y
            out[i] = y
        return out


def lp(x, f, q=0.707):
    return Biquad("lp", f, q).run(x)


def hp(x, f, q=0.707):
    return Biquad("hp", f, q).run(x)


def bp(x, f, q=1.0):
    return Biquad("bp", f, q).run(x)


def sweep_bp(x, f_of_t, q):
    """Band-pass with time-varying centre (state-variable filter)."""
    out = [0.0] * len(x)
    low = band = 0.0
    damp = 1.0 / q
    for i, v in enumerate(x):
        f = min(f_of_t(i / SR), SR * 0.2)
        fc = 2.0 * math.sin(math.pi * f / SR)
        high = v - low - damp * band
        band += fc * high
        low += fc * band
        out[i] = band
    return out


def sweep_lp(x, f_of_t):
    """One-pole low-pass with time-varying cutoff."""
    out = [0.0] * len(x)
    y = 0.0
    for i, v in enumerate(x):
        a = 1.0 - math.exp(-TWO_PI * f_of_t(i / SR) / SR)
        y += a * (v - y)
        out[i] = y
    return out


def drive(x, amount):
    k = math.tanh(amount)
    return [math.tanh(v * amount) / k for v in x]


def comb_reverb(x, length_sec, delays, fb, damp_hz, wet):
    """Small Schroeder-ish comb bank: a little room so dry synth sounds less clinical."""
    n = ns(length_sec)
    src = x + [0.0] * (n - len(x)) if len(x) < n else x[:n]
    acc = [0.0] * n
    a = 1.0 - math.exp(-TWO_PI * damp_hz / SR)
    for dsec in delays:
        d = ns(dsec)
        buf = [0.0] * n
        y = 0.0
        for i in range(n):
            fbv = buf[i - d] if i >= d else 0.0
            y += a * (fbv - y)
            buf[i] = src[i] + fb * y
            acc[i] += buf[i - d] if i >= d else 0.0
    w = wet / len(delays)
    return [src[i] + acc[i] * w for i in range(n)]


def modes(fdg, sec, rng=None):
    """Sum of exponentially decaying sines (struck objects). fdg = [(freq, tau, gain)]."""
    n = ns(sec)
    out = [0.0] * n
    for f, tau, g in fdg:
        ph = rng.random() if rng else 0.0
        k = math.exp(-1.0 / (tau * SR))
        e = g
        w = TWO_PI * f / SR
        for i in range(n):
            out[i] += e * math.sin(w * i + TWO_PI * ph)
            e *= k
            if e < 1e-5:
                break
    return out


def click(sec, f, tau, rng, q=2.0):
    return mul(bp(noise(rng, sec), f, q), env_exp(sec, tau))


def thump(sec, f0, f1, tau, attack=0.002):
    """Low sine kick sweeping f0 -> f1."""
    return mul(tone(lambda t: f1 + (f0 - f1) * math.exp(-t / 0.03), sec), env_exp(sec, tau, attack=attack))


def knock(rng, sec, f, tau, g_click=0.4):
    """Wooden knock: a few inharmonic modes + a filtered click."""
    m = modes([(f, tau, 1.0), (f * 2.31, tau * 0.6, 0.45), (f * 3.9, tau * 0.35, 0.2)], sec, rng)
    return mix(sec, (m, 0, 1.0), (click(min(sec, 0.02), f * 4, 0.003, rng), 0, g_click))


def bell(freq, sec, tau, rng, bright=1.0):
    """Struck bell: church-bell-ish partial ratios, higher partials decay faster."""
    p = [(1.0, 1.0, 1.0), (2.0, 0.7, 0.55), (2.76, 0.5, 0.4 * bright), (4.07, 0.35, 0.22 * bright),
         (5.4, 0.25, 0.12 * bright)]
    return modes([(freq * r, tau * d, g) for r, d, g in p], sec, rng)


def brass(freq, sec, vib=True):
    f = (lambda t: freq * (1 + 0.006 * math.sin(TWO_PI * 5.2 * t) * min(1, t / 0.2))) if vib else freq
    s = mix(sec, (tone(f, sec, "saw"), 0, 0.7), (tone(f, sec, "square"), 0, 0.2), (tone(f, sec, "sine"), 0, 0.4))
    s = sweep_lp(s, lambda t: 700 + 1800 * min(1.0, t / 0.06) * math.exp(-t / 0.5))
    s = lp(s, 3000)
    return mul(s, env_adsr(sec, 0.03, 0.1, 0.75, 0.08))


def finish_sfx(x, sec):
    """Trim/pad to length, remove DC, fade in/out, peak-normalise."""
    n = ns(sec)
    x = (x + [0.0] * n)[:n]
    x = hp(x, 30.0)
    f = ns(FADE_S)
    for i in range(min(f, n)):
        x[i] *= i / f
        x[n - 1 - i] *= i / f
    return normalise(x, SFX_PEAK_DB)


def normalise(x, db):
    pk = max(abs(v) for v in x) or 1.0
    g = 10 ** (db / 20.0) / pk
    return [v * g for v in x]


# --------------------------------------------------------------------------
# Effects. Each returns (samples, duration_sec).
# --------------------------------------------------------------------------

def s_click(rng):
    """UI click: soft, short tick of a bakelite switch."""
    sec = 0.06
    t = mul(tone(1250, sec), env_exp(sec, 0.006, attack=0.0005))
    return mix(sec, (t, 0, 0.7), (click(0.02, 3200, 0.0025, rng), 0, 0.6)), sec


def s_key(rng):
    """Bakelite tool key: a heavier clunk with a low body and a latch click."""
    sec = 0.16
    body = thump(sec, 240, 140, 0.025)
    wood = knock(rng, sec, 430, 0.03, 0.6)
    latch = click(0.02, 2400, 0.003, rng)
    return mix(sec, (body, 0, 0.8), (wood, 0, 0.6), (latch, 0.012, 0.5)), sec


def s_panel_open(rng):
    """Panel open: paper/wooden slide rising, ends with a light tap."""
    sec = 0.24
    sl = sweep_bp(noise(rng, 0.16), lambda t: 700 + 1600 * (t / 0.16), 3.0)
    sl = mul(sl, env_adsr(0.16, 0.04, 0.0, 1.0, 0.08))
    sl = lp(sl, 3500)
    tap = knock(rng, 0.08, 760, 0.018, 0.3)
    return mix(sec, (sl, 0, 0.9), (tap, 0.15, 0.6)), sec


def s_panel_close(rng):
    """Panel close: slide falling, ends with a soft lower thud."""
    sec = 0.22
    sl = sweep_bp(noise(rng, 0.14), lambda t: 1900 - 1300 * (t / 0.14), 3.0)
    sl = mul(sl, env_adsr(0.14, 0.02, 0.0, 1.0, 0.07))
    sl = lp(sl, 3000)
    tap = mix(0.09, (knock(rng, 0.09, 380, 0.025, 0.2), 0, 0.8), (thump(0.09, 160, 90, 0.025), 0, 0.6))
    return mix(sec, (sl, 0, 0.8), (tap, 0.12, 0.8)), sec


def s_build(rng):
    """Construction thud: timber dropped + two hammer knocks + a little dust."""
    sec = 0.6
    th = thump(0.4, 110, 55, 0.11, attack=0.003)
    wood = knock(rng, 0.3, 210, 0.06, 0.5)
    dust = mul(lp(noise(rng, 0.35), 900), env_exp(0.35, 0.08, attack=0.01))
    h1 = knock(rng, 0.12, 620, 0.025, 0.7)
    h2 = knock(rng, 0.12, 640, 0.022, 0.7)
    x = mix(sec, (th, 0, 1.0), (wood, 0, 0.7), (dust, 0, 0.35), (h1, 0.26, 0.45), (h2, 0.40, 0.35))
    return comb_reverb(x, sec, [0.043, 0.061, 0.089], 0.35, 1500, 0.4), sec


def gravel(rng, sec, density, lo, hi, grain_tau):
    """Random short band-passed grains: crunchy granular texture."""
    out = [0.0] * ns(sec)
    n = int(density * sec)
    for _ in range(n):
        t = rng.uniform(0, sec - 0.02)
        f = rng.uniform(lo, hi)
        g = rng.uniform(0.3, 1.0)
        gr = click(0.015, f, grain_tau, rng, q=3.0)
        o = ns(t)
        for i, v in enumerate(gr):
            if o + i < len(out):
                out[o + i] += v * g
    return out


def s_road(rng):
    """Road laying: two shovels of gravel (crunch clusters) over a soft spread hiss."""
    sec = 0.55
    c1 = mul(gravel(rng, 0.25, 260, 1200, 4200, 0.003), env_adsr(0.25, 0.01, 0.1, 0.6, 0.12))
    c2 = mul(gravel(rng, 0.25, 220, 1000, 3800, 0.003), env_adsr(0.25, 0.01, 0.1, 0.5, 0.12))
    bed = mul(lp(noise(rng, sec), 700), env_adsr(sec, 0.02, 0.1, 0.5, 0.2))
    th = thump(0.1, 150, 90, 0.02)
    x = mix(sec, (c1, 0.0, 0.9), (c2, 0.25, 0.75), (bed, 0, 0.35), (th, 0, 0.5), (th, 0.25, 0.4))
    return lp(x, 5000), sec


def s_bulldoze(rng):
    """Bulldozer/demolish: a chugging diesel pulse, then crumbling masonry and falling planks."""
    sec = 1.0
    n = ns(sec)
    eng = [0.0] * n
    for i in range(n):  # 18 Hz firing pulses, slightly uneven
        t = i / SR
        ph = (t * 18.0 + 0.08 * math.sin(TWO_PI * 3.1 * t)) % 1.0
        eng[i] = math.exp(-ph * 9.0)
    eng = mix(sec, (lp([e * rng.uniform(0.6, 1.0) for e in eng], 260), 0, 1.0), (tone(54, sec, "tri"), 0, 0.25))
    eng = mul(eng, env_adsr(sec, 0.05, 0.1, 0.7, 0.35))
    crum = mul(lp(gravel(rng, 0.6, 180, 300, 1800, 0.006), 2500), env_adsr(0.6, 0.03, 0.2, 0.6, 0.3))
    fall = mix(0.6, (knock(rng, 0.15, 260, 0.04, 0.4), 0.0, 0.8), (knock(rng, 0.15, 330, 0.035, 0.4), 0.14, 0.6),
               (knock(rng, 0.15, 210, 0.05, 0.4), 0.3, 0.5), (thump(0.3, 90, 50, 0.08), 0.0, 0.8))
    x = mix(sec, (eng, 0, 0.7), (crum, 0.25, 0.8), (fall, 0.28, 0.8))
    return x, sec


def s_cash(rng):
    """Cash register: key clack, drawer roll, and the bell."""
    sec = 0.8
    clack = mix(0.06, (click(0.03, 1800, 0.004, rng), 0, 1.0), (knock(rng, 0.06, 520, 0.015, 0.3), 0, 0.8))
    drawer = mul(lp(gravel(rng, 0.18, 300, 500, 1600, 0.004), 2000), env_adsr(0.18, 0.02, 0.0, 1.0, 0.06))
    ding = bell(2093.0, 0.6, 0.35, rng, 0.6)
    thud = knock(rng, 0.08, 300, 0.03, 0.3)
    x = mix(sec, (clack, 0, 0.7), (drawer, 0.05, 0.4), (ding, 0.08, 0.8), (thud, 0.23, 0.5))
    return lp(x, 6000), sec


def s_chime(rng):
    """Ticker chime: two soft glockenspiel notes (E6, C6)."""
    sec = 0.9
    def glock(f):
        return modes([(f, 0.32, 1.0), (f * 2.76, 0.08, 0.18), (f * 5.4, 0.03, 0.06)], 0.7, rng)
    x = mix(sec, (glock(1318.5), 0, 0.8), (glock(1046.5), 0.16, 0.8))
    return comb_reverb(x, sec, [0.051, 0.073], 0.3, 2500, 0.3), sec


def s_warn(rng):
    """Warning bell: two strokes of a hand bell, gentle but attention-getting."""
    sec = 1.0
    b = bell(880.0, 0.8, 0.45, rng, 0.8)
    x = mix(sec, (b, 0, 0.9), (b, 0.28, 0.8))
    return lp(x, 5000), sec


def s_type(rng):
    """Typewriter burst: 8 key strikes with uneven rhythm, then the carriage bell."""
    sec = 1.05
    parts = []
    t = 0.0
    for k in range(8):
        strike = mix(0.06, (click(0.03, rng.uniform(2200, 3000), 0.0035, rng), 0, 1.0),
                     (modes([(rng.uniform(280, 330), 0.012, 0.8), (rng.uniform(690, 760), 0.008, 0.4)], 0.06, rng), 0, 0.9),
                     (click(0.02, 900, 0.004, rng), 0.012, 0.4))
        parts.append((strike, t, rng.uniform(0.6, 0.9)))
        t += rng.uniform(0.065, 0.11)
    parts.append((modes([(2350, 0.25, 1.0), (2350 * 2.7, 0.06, 0.15)], 0.3, rng), t + 0.02, 0.35))
    x = mix(sec, *parts)
    return lp(x, 6500), sec


def s_stamp(rng):
    """Rubber stamp: a short ink-pad pat, then a dull firm thud onto paper on a wooden desk."""
    sec = 0.4
    pat = mul(lp(noise(rng, 0.04), 1200), env_exp(0.04, 0.008))
    th = thump(0.25, 170, 85, 0.045)
    paper = mul(lp(noise(rng, 0.08), 1800), env_exp(0.08, 0.015))
    desk = knock(rng, 0.2, 260, 0.04, 0.3)
    x = mix(sec, (pat, 0, 0.35), (th, 0.12, 1.0), (paper, 0.12, 0.55), (desk, 0.12, 0.5))
    return x, sec


def s_firebell(rng):
    """Fire bell: rapid clapper strokes of a station alarm bell (softened, lowpassed)."""
    sec = 1.5
    n_strokes = 18
    stroke = bell(740.0, 0.6, 0.3, rng, 0.7)
    parts = []
    for k in range(n_strokes):
        t = k / 13.0
        parts.append((stroke, t, 0.55 + 0.1 * (k % 2)))
    x = mix(sec, *parts)
    x = mul(x, env_adsr(sec, 0.01, 0.0, 1.0, 0.3))
    return lp(x, 3800), sec


def s_paper(rng):
    """Newspaper rustle: crinkle grains in two gestures plus a page-flap whoosh."""
    sec = 0.65
    cr1 = mul(hp(gravel(rng, 0.25, 500, 2000, 6000, 0.0015), 1500), env_bump(0.25, 0.12, 0.25))
    cr2 = mul(hp(gravel(rng, 0.25, 400, 1800, 5500, 0.0015), 1500), env_bump(0.25, 0.12, 0.25))
    flap = mul(sweep_bp(noise(rng, 0.2), lambda t: 500 + 1500 * (t / 0.2), 1.5), env_bump(0.2, 0.1, 0.2))
    th = thump(0.06, 120, 80, 0.015)
    x = mix(sec, (cr1, 0.0, 0.7), (flap, 0.18, 0.9), (th, 0.32, 0.25), (cr2, 0.36, 0.55))
    return lp(x, 7000), sec


def s_fanfare(rng):
    """Milestone fanfare: short brass flourish G4-C5-E5 then a held G5 over a C chord."""
    sec = 1.5
    notes = [(392.0, 0.0, 0.16), (523.25, 0.15, 0.16), (659.25, 0.30, 0.16)]
    parts = [(brass(f, d, vib=False), t, 0.55) for f, t, d in notes]
    parts.append((brass(783.99, 1.0), 0.45, 0.55))
    parts.append((brass(523.25, 1.0), 0.45, 0.30))
    parts.append((brass(329.63, 1.0), 0.45, 0.25))
    x = mix(sec, *parts)
    return comb_reverb(x, sec, [0.047, 0.067, 0.097], 0.4, 2000, 0.35), sec


def s_deny(rng):
    """Error buzz: two short soft low buzzes (not harsh)."""
    sec = 0.34
    def bz(d):
        b = mix(d, (tone(146.8, d, "square"), 0, 0.5), (tone(147.8, d, "saw"), 0, 0.4))
        return mul(lp(b, 900), env_adsr(d, 0.006, 0.02, 0.8, 0.025))
    x = mix(sec, (bz(0.12), 0, 1.0), (bz(0.14), 0.17, 1.0))
    return x, sec


# --------------------------------------------------------------------------
# Ambient loops. Each recipe returns (samples of LEN + XF seconds, LEN, XF);
# make_loop() crossfades the tail into the head so sample[L-1] -> sample[0] is continuous.
# LFOs and steady tones use whole cycles per loop (period-compatible).
# --------------------------------------------------------------------------

PRE = 0.5  # pre-roll discarded (filter start-up transients)


def make_loop(x, loop_sec, xf_sec):
    L, X = ns(loop_sec), ns(xf_sec)
    assert len(x) >= L + X
    y = x[:L]
    for i in range(X):
        u = i / X
        a, b = math.sin(0.5 * math.pi * u), math.cos(0.5 * math.pi * u)  # equal power (uncorrelated noise)
        y[i] = x[i] * a + x[L + i] * b
    return y


def chirp_tweet(rng, f0, f1, dur, wob=0.0, wobf=30.0):
    def f(t):
        return f0 + (f1 - f0) * (t / dur) + wob * math.sin(TWO_PI * wobf * t)
    return mul(tone(f, dur), env_adsr(dur, dur * 0.2, 0.0, 1.0, dur * 0.5))


def amb_birds(rng):
    """Small town: soft wind in the trees and a few songbirds."""
    L, X = 8.0, 1.0
    tot = PRE + L + X
    n = ns(tot)
    wind = lp(noise(rng, tot), 350)
    leaves = hp(lp(noise(rng, tot), 3500), 1200)
    w = [0.0] * n
    for i in range(n):
        t = i / SR - PRE
        m = 0.65 + 0.25 * math.sin(TWO_PI * t / L) + 0.1 * math.sin(TWO_PI * 3 * t / L + 1.0)
        w[i] = wind[i] * 1.6 * m + leaves[i] * 0.10 * m * m
    birds = []
    t = PRE + 0.3
    while t < tot - 0.6:
        kind = rng.random()
        if kind < 0.4:  # finch: 3-5 quick rising tweets
            for k in range(rng.randint(3, 5)):
                f0 = rng.uniform(3200, 3800)
                birds.append((chirp_tweet(rng, f0, f0 + 900, 0.05, 150, 40), t + k * 0.085, 0.14))
            t += rng.uniform(0.9, 1.6)
        elif kind < 0.75:  # blackbird-ish warble
            f0 = rng.uniform(1700, 2200)
            birds.append((chirp_tweet(rng, f0, f0 * 1.25, 0.18, 120, 18), t, 0.12))
            birds.append((chirp_tweet(rng, f0 * 1.2, f0 * 0.95, 0.22, 90, 14), t + 0.2, 0.10))
            t += rng.uniform(1.0, 1.8)
        else:  # tsip
            birds.append((chirp_tweet(rng, 5200, 3800, 0.04), t, 0.08))
            birds.append((chirp_tweet(rng, 5000, 3700, 0.04), t + 0.12, 0.07))
            t += rng.uniform(0.6, 1.2)
    b = mix(tot, *birds)
    b = lp(b, 6000)
    b = comb_reverb(b, tot, [0.061, 0.083, 0.117], 0.3, 3000, 0.35)
    x = [w[i] + b[i] for i in range(n)]
    x = hp(x, 40)
    return make_loop(x[ns(PRE):], L, X), L


def amb_traffic(rng):
    """Distant traffic hum: low rumble and a few soft car passes (lowpassed by distance)."""
    L, X = 6.0, 1.0
    tot = PRE + L + X
    n = ns(tot)
    rum = lp(lp(noise(rng, tot), 180), 180)
    mid = lp(noise(rng, tot), 700)
    base = [0.0] * n
    for i in range(n):
        t = i / SR - PRE
        m = 0.8 + 0.2 * math.sin(TWO_PI * 2 * t / L)
        base[i] = rum[i] * 2.2 * m + mid[i] * 0.12
    passes = []
    t = PRE + 0.2
    while t < tot - 0.5:
        d = rng.uniform(2.0, 3.0)
        fc = rng.uniform(450, 700)
        sw = sweep_bp(noise(rng, d), lambda u, fc=fc, d=d: fc * (1.25 - 0.5 * u / d), 1.2)
        eng_f = rng.uniform(80, 110)
        eng = tone(lambda u, e=eng_f, d=d: e * (1.04 - 0.08 * u / d), d, "tri")
        p = mix(d, (sw, 0, 1.0), (lp(eng, 400), 0, 0.15))
        passes.append((mul(p, env_bump(d, d * 0.5, d)), t, rng.uniform(0.35, 0.55)))
        t += rng.uniform(1.0, 1.9)
    p = mix(tot, *passes)
    x = [base[i] + p[i] for i in range(n)]
    x = hp(lp(x, 1500), 35)
    return make_loop(x[ns(PRE):], L, X), L


def amb_industry(rng):
    """Distant sawmill/works: motor hum, a band-saw whine swelling with each cut, soft clanks."""
    L, X = 6.0, 0.8
    tot = PRE + L + X
    n = ns(tot)
    # steady tones: whole cycles per loop (f * L integer)
    hum = mix(tot, (tone(50.0, tot), 0, 0.5), (tone(100.0, tot), 0, 0.35), (tone(150.0, tot), 0, 0.15))
    cut = [0.0] * n
    for i in range(n):  # two cuts per loop, smooth envelope (period L/2)
        t = i / SR - PRE
        c = 0.5 - 0.5 * math.cos(TWO_PI * 2 * t / L)
        cut[i] = c ** 3
    whine = tone(lambda t: 1180.0 - 120.0 * (0.5 - 0.5 * math.cos(TWO_PI * 2 * (t - PRE) / L)) ** 3, tot)
    whine2 = tone(lambda t: 2360.0 - 240.0 * (0.5 - 0.5 * math.cos(TWO_PI * 2 * (t - PRE) / L)) ** 3, tot)
    chips = bp(noise(rng, tot), 1800, 1.5)
    bed = lp(noise(rng, tot), 400)
    x = [0.0] * n
    for i in range(n):
        c = cut[i]
        x[i] = (hum[i] * 0.55 + bed[i] * 0.9 + whine[i] * (0.03 + 0.20 * c) + whine2[i] * 0.06 * c
                + chips[i] * 0.12 * c)
    clanks = []
    for k in range(int(tot / 1.5) + 1):
        t = PRE + 0.4 + k * 1.5
        clanks.append((mix(0.4, (knock(rng, 0.4, 320, 0.06, 0.2), 0, 1.0), (thump(0.2, 120, 70, 0.04), 0, 0.6)), t, 0.18))
    cl = comb_reverb(mix(tot, *clanks), tot, [0.071, 0.103, 0.137], 0.45, 1200, 0.6)
    x = [x[i] + cl[i] for i in range(n)]
    x = hp(lp(x, 2600), 35)
    return make_loop(x[ns(PRE):], L, X), L


# Table order = SND_* / AMB_* enums in src/snd.h (file names are listed in src/snd.cpp too).
SOUNDS = [
    ("click", s_click), ("key", s_key), ("panel_open", s_panel_open), ("panel_close", s_panel_close),
    ("build", s_build), ("road", s_road), ("bulldoze", s_bulldoze), ("cash", s_cash), ("chime", s_chime),
    ("warn", s_warn), ("type", s_type), ("stamp", s_stamp), ("firebell", s_firebell), ("paper", s_paper),
    ("fanfare", s_fanfare), ("deny", s_deny),
]
LOOPS = [("amb_birds", amb_birds), ("amb_traffic", amb_traffic), ("amb_industry", amb_industry)]


def to_pcm(x):
    data = bytearray()
    clipped = 0
    for v in x:
        s = int(round(v * 32767.0))
        if s > 32767 or s < -32768:
            clipped += 1
            s = 32767 if s > 32767 else -32768
        data += struct.pack("<h", s)
    return bytes(data), clipped


def write_wav(path, pcm):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm)


def read_back(path):
    with wave.open(path, "rb") as w:
        assert w.getnchannels() == 1 and w.getsampwidth() == 2 and w.getframerate() == SR
        raw = w.readframes(w.getnframes())
    return list(struct.unpack("<%dh" % (len(raw) // 2), raw))


def db(a):
    return 20 * math.log10(a) if a > 0 else -999.0


def seam_check(s):
    """Loop seam: the jump s[-1] -> s[0] compared with typical sample-to-sample steps,
    and short-window RMS just before vs just after the seam."""
    diffs = sorted(abs(s[i + 1] - s[i]) for i in range(len(s) - 1))
    med, p99 = diffs[len(diffs) // 2], diffs[int(len(diffs) * 0.99)]
    jump = abs(s[0] - s[-1])
    w = ns(0.1)
    rms = lambda seg: math.sqrt(sum(v * v for v in seg) / len(seg))
    r_end, r_head = rms(s[-w:]), rms(s[:w])
    ratio = db(max(r_end, 1) / max(r_head, 1))
    ok = jump <= max(p99, 1) and abs(ratio) < 3.0
    return ok, jump, med, p99, ratio


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "sfx")
    report = "--report" in sys.argv
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None
    if "--out" in sys.argv:
        out = sys.argv[sys.argv.index("--out") + 1]
    os.makedirs(out, exist_ok=True)
    total, fails = 0, 0
    print("%-13s %6s %8s %7s %7s %7s  %s" % ("name", "sec", "bytes", "peakdB", "rmsdB", "clip", "loop seam"))
    for name, fn in SOUNDS + LOOPS:
        if only and name != only:
            continue
        rng = random.Random(zlib.crc32(name.encode("ascii")))
        x, sec = fn(rng)
        is_loop = name.startswith("amb_")
        x = normalise(x, AMB_PEAK_DB) if is_loop else finish_sfx(x, sec)
        pcm, clipped = to_pcm(x)
        path = os.path.join(out, name + ".wav")
        write_wav(path, pcm)
        size = os.path.getsize(path)
        total += size
        s = read_back(path)  # verify what was actually written
        pk = max(abs(v) for v in s) / 32768.0
        rms = math.sqrt(sum(v * v for v in s) / len(s)) / 32768.0
        hard = sum(1 for v in s if v >= 32767 or v <= -32768)
        seam = ""
        ok = clipped == 0 and hard == 0 and pk > 0.1
        if is_loop:
            sok, jump, med, p99, ratio = seam_check(s)
            seam = "%s jump %d (median step %d, p99 %d), rms end/head %+.1f dB" % (
                "ok  " if sok else "FAIL", jump, med, p99, ratio)
            ok = ok and sok
        else:
            # one-shots must start and end silent (no click when a voice starts/stops)
            ok = ok and abs(s[0]) < 64 and abs(s[-1]) < 64
        if not ok:
            fails += 1
        print("%-13s %6.3f %8d %7.2f %7.2f %7d  %s%s" % (name, len(s) / SR, size, db(pk), db(rms), clipped + hard,
                                                        seam, "" if ok else "  <-- CHECK FAILED"))
    print("%d bytes total -> %s; %d check failures" % (total, out, fails))
    if report and fails:
        sys.exit(1)


if __name__ == "__main__":
    main()
