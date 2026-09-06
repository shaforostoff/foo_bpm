# -*- coding: utf-8 -*-
"""Metrical analysis: beat period, meter, and a bar-synchronous rhythm pattern.

The pieces here are what both deliverables stand on - the BPM estimate needs the
beat period and the metrical level, the rhythm classifier needs the bar pattern.
"""
import numpy as np
import tempo as TP

FPS = TP.FPS
BEAT_BPM_MIN, BEAT_BPM_MAX = 70.0, 280.0
METERS = (2, 3, 4, 6)
NBIN = 24                       # bar-pattern bins; divisible by 2, 3, 4, 6, 8


def _interp(r, lag):
    if lag < 0 or lag + 1 >= len(r):
        return 0.0
    i = int(lag); f = lag - i
    return float(r[i] * (1.0 - f) + r[i + 1] * f)


def _refine(r, lag, tol=0.03):
    """Parabolic refinement of the ACF peak nearest `lag` - this is where the
    sub-BPM precision comes from."""
    lo = max(1, int(np.floor(lag * (1 - tol))))
    hi = min(len(r) - 2, int(np.ceil(lag * (1 + tol))))
    if hi <= lo:
        return lag, _interp(r, lag)
    j = lo + int(np.argmax(r[lo:hi + 1]))
    if 1 <= j < len(r) - 1:
        a, b, c = float(r[j - 1]), float(r[j]), float(r[j + 1])
        d = a - 2 * b + c
        if abs(d) > 1e-12:
            off = 0.5 * (a - c) / d
            if -1.0 < off < 1.0:
                return j + off, b - 0.25 * (a - c) * off
    return float(j), float(r[j])


def metrical_grid(r):
    """Search (beat period, meter) jointly.

    A pulse at tau shows up in the ACF at every multiple of tau; the meter is
    settled by whether the bar-level peak sits at 3*tau or 4*tau. Scoring the
    two together stops a strong bar peak from being mistaken for the beat.
    """
    lag_lo = TP.bpm_to_lag(BEAT_BPM_MAX)
    lag_hi = min(TP.bpm_to_lag(BEAT_BPM_MIN), (len(r) - 2) / 8.0)
    best = None
    cand = np.arange(lag_lo, lag_hi, 0.25)
    for lag in cand:
        base = [_interp(r, lag * k) for k in (1, 2, 3, 4)]
        for m in METERS:
            bar = lag * m
            if bar * 2 + 1 >= len(r):
                continue
            # on-grid support: beat, bar, two bars, and the half-bar when even
            grid = [(_interp(r, lag), 1.0), (_interp(r, bar), 1.0), (_interp(r, 2 * bar), 0.6)]
            if m % 2 == 0:
                grid.append((_interp(r, bar / 2), 0.7))
            if m % 3 == 0:
                grid.append((_interp(r, bar / 3), 0.7))
            num = sum(v * w for v, w in grid)
            den = sum(w for _, w in grid)
            score = num / den
            # penalise a grid that leaves a strong periodicity unexplained
            off = max(_interp(r, lag * 1.5), _interp(r, lag * 2.5)) if m != 3 else _interp(r, lag * 4.0 / 3.0)
            score -= 0.25 * max(0.0, off - score)
            if best is None or score > best[0]:
                best = (score, lag, m, base)
    if best is None:
        return dict(beat_lag=0.0, meter=4, score=0.0)
    score, lag, m, _ = best
    lag, peak = _refine(r, lag)
    return dict(beat_lag=float(lag), meter=int(m), score=float(score), beat_acf=float(peak))


def bar_pattern(odf, bar_lag, nbin=NBIN):
    """Average one bar of per-band onset energy, rotated so the strongest
    low-band accent sits in bin 0 (makes it phase invariant)."""
    nb, T = odf.shape
    if bar_lag < 4 or T < bar_lag * 3:
        return np.zeros((nb, nbin), dtype=np.float32)
    idx = np.arange(T)
    pos = (idx % bar_lag) / bar_lag * nbin
    b0 = np.floor(pos).astype(np.int32) % nbin
    frac = pos - np.floor(pos)
    b1 = (b0 + 1) % nbin
    out = np.zeros((nb, nbin), dtype=np.float64)
    cnt = np.zeros(nbin, dtype=np.float64)
    np.add.at(cnt, b0, 1 - frac); np.add.at(cnt, b1, frac)
    cnt[cnt < 1e-9] = 1e-9
    for b in range(nb):
        v = odf[b].astype(np.float64)
        np.add.at(out[b], b0, v * (1 - frac))
        np.add.at(out[b], b1, v * frac)
        out[b] /= cnt
    low = out[0] + out[1]
    shift = int(np.argmax(low))
    out = np.roll(out, -shift, axis=1)
    for b in range(nb):
        m = out[b].mean()
        out[b] = out[b] / m - 1.0 if m > 1e-9 else 0.0
    return out.astype(np.float32)


# Lags sampled relative to the beat period; these ratios are what separate a
# 3/4 bar from a 4/4 one, and a habanera from a walking marcato.
REL_LAGS = [0.25, 1/3, 0.5, 2/3, 0.75, 1.0, 1.25, 4/3, 1.5, 5/3, 2.0,
            7/3, 2.5, 8/3, 3.0, 10/3, 3.5, 4.0, 4.5, 5.0, 6.0, 8.0, 9.0, 12.0]


NB = 6          # ODF bands
NBT = 8         # bins when folding a single beat
NGRP = 3        # band groups (low/mid/high) for the fixed folds
FIXED_FOLDS = [(2, 12), (3, 12), (4, 16)]


def _feature_names():
    """Canonical order. The C++ port builds the same vector in the same order,
    so the trained weights can be pasted straight across."""
    n = ['log_beat', 'meter', 'grid_score', 'beat_acf']
    n += [f'acf_r{i}' for i in range(len(REL_LAGS))]
    n += ['m3_vs_m4', 'm3_vs_m2', 'm6_vs_m4']
    n += [f'bandshare{b}' for b in range(NB)]
    n += [f'bandcv{b}' for b in range(NB)]
    n += ['acf_peak', 'odf_kurt', 'odf_skew']
    n += [f'bp{b}_{k}' for b in range(NB) for k in range(NBIN)]
    n += [f'bt{b}_{k}' for b in range(NB) for k in range(NBT)]
    # Folds at fixed multiples of the beat, so the pattern does not depend on
    # the meter search having picked the right bar length.
    for mult, bins in FIXED_FOLDS:
        n += [f'fx{mult}_{b}_{k}' for b in range(NGRP) for k in range(bins)]
    return n


FEATURE_NAMES = _feature_names()


def extract(odf):
    """Feature vector for one track, in FEATURE_NAMES order."""
    y = TP.novelty(TP.mix(odf))
    r = TP.aggregate(TP.acf_windowed(y, max_lag_sec=5.0))
    g = metrical_grid(r)
    lag = g['beat_lag']
    beat_bpm = TP.lag_to_bpm(lag) if lag > 0 else 1.0
    v = [float(np.log(max(beat_bpm, 1.0))), float(g['meter']), float(g['score']),
         float(g.get('beat_acf', 0.0))]
    v += [(_interp(r, lag * m) if lag > 0 else 0.0) for m in REL_LAGS]
    a2, a3, a4, a6 = (_interp(r, lag * k) for k in (2, 3, 4, 6))
    v += [a3 - a4, a3 - a2, a6 - a4]
    e = odf.astype(np.float64).mean(axis=1)
    tot = e.sum() + 1e-12
    v += [float(x / tot) for x in e]
    sd = odf.astype(np.float64).std(axis=1)
    v += [float(sd[b] / (e[b] + 1e-9)) for b in range(odf.shape[0])]
    v += [float(np.max(r[8:]) if len(r) > 8 else 0.0),
          float(((y - y.mean()) ** 4).mean() / (y.var() ** 2 + 1e-12)),
          float(((y - y.mean()) ** 3).mean() / (y.std() ** 3 + 1e-12))]
    bp = bar_pattern(odf, lag * g['meter'], NBIN)
    v += [float(x) for x in bp.reshape(-1)]
    bt = bar_pattern(odf, lag, nbin=NBT)
    v += [float(x) for x in bt.reshape(-1)]
    grouped = np.stack([odf[0] + odf[1], odf[2] + odf[3], odf[4] + odf[5]])
    for mult, bins in FIXED_FOLDS:
        fx = bar_pattern(grouped, lag * mult, nbin=bins)
        v += [float(x) for x in fx.reshape(-1)]
    return np.array(v, dtype=np.float32), r, g
