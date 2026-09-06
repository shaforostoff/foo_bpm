# -*- coding: utf-8 -*-
"""Turn an ODF into the BPM the user would have tapped."""
import numpy as np
import tempo as TP, features as F

# Log-normal priors fitted to the hand taps, per rhythm class. They are used
# ONLY to choose the metrical level - the value itself comes from the audio.
# (centre BPM, log sigma, weight given to autocorrelation support).
# Tango, vals and milonga have tight priors, so the level is decided by the
# prior and the support weight barely matters. 'other' has no meaningful tempo
# prior - the collection ranges from bossa to disco - so there the audio is
# trusted far more and the prior only breaks ties.
PRIOR = {
    'tango':   (125.5, 0.075, 1.6),
    'vals':    ( 68.5, 0.090, 1.6),
    'milonga': ( 52.5, 0.101, 1.6),
    # one hand-tapped foxtrot in the whole collection, so this is a placeholder:
    # wide, and weighted so the autocorrelation decides.
    'swing':   (130.0, 0.400, 6.0),
    'other':   (110.0, 0.450, 6.0),
}
# Levels the tapped rate can sit on, relative to the detected beat period.
# Offering a duple rhythm a /3 level is what made slow milongas land on beat/3
# instead of beat/4, so the sets are kept apart. Two beats is not a metrical
# level of a 3/4 bar and is not offered: leaving it in was enough to take a
# Peruvian vals at 56 to the bar and report the two-beat rate instead.
LEVELS_DUPLE  = [1/8, 1/4, 1/2, 1.0, 2.0, 4.0]
LEVELS_TRIPLE = [1/6, 1/3, 1/2, 2/3, 1.0, 3/2, 3.0]


def levels_for(cls, meter):
    """The three tango rhythms state their own metre, whatever the grid search
    made of it. 'other' - chacarera through disco - states nothing, so there the
    detected metre decides, which keeps a duple piece off the two-thirds level.
    """
    if cls == 'vals':
        return LEVELS_TRIPLE
    if cls in ('tango', 'milonga', 'swing'):
        return LEVELS_DUPLE
    return LEVELS_TRIPLE if meter in (3, 6) else LEVELS_DUPLE

# Taps run marginally ahead of the measured pulse across the whole collection;
# this is an empirical calibration, not a physical correction.
TAP_BIAS = 0.5

HARM_W = [1.0, 0.9, 0.6, 0.8, 0.4, 0.5, 0.3, 0.4]   # weights for k = 1..8


def refine_period(r, lag0, span=0.06, steps=241):
    """Sharpen the beat period using every harmonic of it at once.

    A single ACF peak is a couple of frames wide; the multiples of the period
    are not, so fitting the whole comb pins the period far more tightly.
    """
    if lag0 <= 0:
        return lag0
    best, bl = -1e18, lag0
    for lag in np.linspace(lag0 * (1 - span), lag0 * (1 + span), steps):
        s = w = 0.0
        for k, wk in enumerate(HARM_W, start=1):
            x = k * lag
            if x + 1 >= len(r):
                break
            i = int(x); f = x - i
            s += wk * (r[i] * (1 - f) + r[i + 1] * f)
            w += wk
        if w > 0 and s / w > best:
            best, bl = s / w, lag
    return float(bl)


def predict(odf, cls, r=None, grid=None):
    if r is None:
        y = TP.novelty(TP.mix(odf))
        r = TP.aggregate(TP.acf_windowed(y, max_lag_sec=5.0))
    if grid is None:
        grid = F.metrical_grid(r)
    lag = grid['beat_lag']
    if lag <= 0:
        return 0.0, {}
    lag = refine_period(r, lag)
    mu, sig, sup_w = PRIOR.get(cls, PRIOR['other'])
    best, out, dbg = -1e18, 0.0, {}
    for k in levels_for(cls, int(grid.get('meter', 4))):
        L = lag * k
        bpm = TP.lag_to_bpm(L)
        if not (25.0 <= bpm <= 320.0):
            continue
        sup = F._interp(r, L) if L + 1 < len(r) else 0.0
        # support of the level itself, plus a log-normal tempo prior
        lp = -0.5 * ((np.log(bpm) - np.log(mu)) / sig) ** 2
        sc = sup_w * sup + lp
        dbg[k] = (bpm, sup, lp, sc)
        if sc > best:
            best, out = sc, bpm
    return (out + TAP_BIAS if out > 0 else 0.0), dbg
