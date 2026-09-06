# -*- coding: utf-8 -*-
"""Tempo estimation from a cached band ODF.

Everything here is written so it can be ported to C++ without a maths library:
band mixing, local-mean subtraction, windowed autocorrelation, comb scoring.
"""
import numpy as np

FPS = 22050.0 / 256.0


def mix(odf, weights=None):
    """Sum the bands after per-band variance normalisation.

    Shellac hiss lives in the top band and dominates a raw broadband flux, so
    every band is put on the same footing before summing.
    """
    o = odf.astype(np.float32)
    sd = o.std(axis=1, keepdims=True)
    sd[sd < 1e-9] = 1e-9
    o = o / sd
    if weights is not None:
        o = o * np.asarray(weights, dtype=np.float32)[:, None]
    return o.sum(axis=0)


def novelty(x, smooth=3, local_mean_sec=0.6):
    """Smooth, subtract a local mean, half-wave rectify."""
    if smooth > 1:
        k = np.ones(smooth, dtype=np.float32) / smooth
        x = np.convolve(x, k, mode='same')
    w = max(3, int(round(local_mean_sec * FPS)) | 1)
    k = np.ones(w, dtype=np.float32) / w
    lm = np.convolve(x, k, mode='same')
    y = x - lm
    np.maximum(y, 0.0, out=y)
    s = y.std()
    return y / s if s > 1e-9 else y


def acf_windowed(y, win_sec=12.0, hop_sec=3.0, max_lag_sec=2.2):
    """Unbiased, per-window normalised autocorrelation, stacked over the track.

    Returns (n_windows, max_lag) — aggregating these across windows is what
    makes a passage that briefly slows down stop dragging the estimate.
    """
    W = int(round(win_sec * FPS))
    H = int(round(hop_sec * FPS))
    L = int(round(max_lag_sec * FPS))
    if len(y) < W:
        W = len(y)
    if W <= L + 8:
        L = max(8, W - 8)
    nfft = 1 << int(np.ceil(np.log2(2 * W)))
    starts = list(range(0, max(1, len(y) - W + 1), H)) or [0]
    out = np.zeros((len(starts), L), dtype=np.float32)
    for i, s in enumerate(starts):
        seg = y[s:s + W].astype(np.float64)
        if seg.std() < 1e-9:
            continue
        seg = seg - seg.mean()
        F = np.fft.rfft(seg, nfft)
        r = np.fft.irfft(F * np.conj(F), nfft)[:L]
        # unbiased: each lag averages over a different number of products
        r = r / np.maximum(W - np.arange(L), 1)
        if r[0] > 1e-12:
            r = r / r[0]
        out[i] = r
    return out


def aggregate(acf, mode='median'):
    if acf.shape[0] == 0:
        return np.zeros(1, dtype=np.float32)
    return np.median(acf, axis=0) if mode == 'median' else acf.mean(axis=0)


def bpm_to_lag(bpm):
    return 60.0 * FPS / bpm


def lag_to_bpm(lag):
    return 60.0 * FPS / lag


def comb_score(r, bpms, harmonics=(1, 2, 3, 4), weights=(1.0, 0.5, 0.33, 0.25)):
    """Score each candidate pulse rate by how much its whole-number multiples line up.

    A pulse at tempo T also puts energy at 2T, 3T, 4T of its period, so summing
    r(k*lag) rewards a real pulse over an accidental peak.
    """
    L = len(r)
    sc = np.zeros(len(bpms), dtype=np.float64)
    for j, bpm in enumerate(bpms):
        lag = bpm_to_lag(bpm)
        tot = wsum = 0.0
        for k, w in zip(harmonics, weights):
            x = k * lag
            i0 = int(np.floor(x))
            if i0 + 1 >= L:
                break
            f = x - i0
            tot += w * (r[i0] * (1 - f) + r[i0 + 1] * f)
            wsum += w
        sc[j] = tot / wsum if wsum > 0 else 0.0
    return sc
