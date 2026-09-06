# -*- coding: utf-8 -*-
"""Decode a track and reduce it to a multi-band onset detection function.

Kept deliberately simple so it can be reimplemented verbatim in the C++
component: mono downmix, one STFT, log-compressed half-wave-rectified
spectral flux per band.
"""
import numpy as np, subprocess, os

SR         = 22050
N_FFT      = 1024
HOP        = 256
FPS        = SR / HOP                      # 86.1328125 frames per second
BAND_EDGES = [60, 200, 400, 800, 1600, 3200, 8000]   # Hz -> 6 bands
N_BANDS    = len(BAND_EDGES) - 1
GAMMA      = 100.0

_WIN = np.hanning(N_FFT).astype(np.float32)


def decode(path, ffmpeg):
    p = subprocess.run([ffmpeg, '-v', 'error', '-i', path, '-ac', '1', '-ar', str(SR),
                        '-f', 'f32le', '-'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != 0 or len(p.stdout) < 4 * SR:
        raise RuntimeError((p.stderr[-400:].decode('utf-8', 'replace') or 'short/empty decode'))
    x = np.frombuffer(p.stdout, dtype='<f4').astype(np.float32)   # frombuffer is read-only
    return np.nan_to_num(x, copy=False)


def stft_mag(x):
    n = 1 + (len(x) - N_FFT) // HOP
    if n < 8:
        raise RuntimeError('too short')
    frames = np.lib.stride_tricks.as_strided(
        x, shape=(n, N_FFT), strides=(x.strides[0] * HOP, x.strides[0]), writeable=False)
    out = np.empty((n, N_FFT // 2 + 1), dtype=np.float32)
    step = 4096
    for i in range(0, n, step):
        blk = frames[i:i + step] * _WIN
        out[i:i + step] = np.abs(np.fft.rfft(blk, axis=1)).astype(np.float32)
    return out


def band_odf(path, ffmpeg):
    x = decode(path, ffmpeg)
    rms = float(np.sqrt(np.mean(x.astype(np.float64) ** 2)) + 1e-12)
    x = (x / rms).astype(np.float32)                 # loudness-invariant
    S = stft_mag(x)
    S = np.log1p(GAMMA * S / N_FFT)                  # perceptual compression
    freqs = np.fft.rfftfreq(N_FFT, 1.0 / SR)
    odf = np.empty((N_BANDS, S.shape[0] - 1), dtype=np.float32)
    for b in range(N_BANDS):
        sel = (freqs >= BAND_EDGES[b]) & (freqs < BAND_EDGES[b + 1])
        d = np.diff(S[:, sel], axis=0)
        odf[b] = np.maximum(d, 0.0).sum(axis=1)
    return odf, len(x) / SR
