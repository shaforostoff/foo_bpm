# -*- coding: utf-8 -*-
"""Compute the metrical feature vector for every cached track -> features.npz"""
import os, sys, json, re
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from concurrent.futures import ProcessPoolExecutor
import config
import dataset as D, features as F, tempo as TP, predict_bpm as P


def base_key(p):
    """Group id: same performance regardless of collection, transfer or retune."""
    b = os.path.splitext(os.path.basename(p))[0]
    b = re.sub(r'\s*\([^)]*\)\s*', ' ', b)
    b = b.lower().translate(str.maketrans('áàâäãéèêëíìîïóòôöõúùûüñç', 'aaaaaeeeeiiiiooooouuuunc'))
    return re.sub(r'[^a-z0-9]+', ' ', b).strip()


def work(path):
    o = D.load_odf(path)
    if o is None or o.shape[1] < int(25 * TP.FPS):
        return path, None, None
    try:
        f, r, g = F.extract(o)
        # BPM under each class hypothesis, so evaluation can mix and match later
        bp = {c: P.predict(o, c, r, g)[0] for c in ['tango', 'vals', 'milonga', 'other']}
        return path, f, bp
    except Exception as e:
        return path, None, str(e)[:120]


def main():
    recs = D.records(need_bpm=False, need_cache=True)
    paths = [r['path'] for r in recs]
    print('tracks with cached ODF:', len(paths), flush=True)
    feats, bpms, keep = {}, {}, []
    with ProcessPoolExecutor(max_workers=12) as ex:
        for i, (p, f, bp) in enumerate(ex.map(work, paths, chunksize=16)):
            if f is not None:
                feats[p] = f; bpms[p] = bp; keep.append(p)
            if (i + 1) % 1000 == 0:
                print(i + 1, flush=True)
    names = F.FEATURE_NAMES
    X = np.array([feats[p] for p in keep], dtype=np.float32)
    meta = {p: dict(bpm_hyp=bpms[p]) for p in keep}
    np.savez_compressed(config.FEATURES, X=X, names=np.array(names), paths=np.array(keep))
    with open(config.BPM_HYPOTHESES, 'w', encoding='utf-8') as fh:
        json.dump(meta, fh)
    print('saved', X.shape)


if __name__ == '__main__':
    main()
