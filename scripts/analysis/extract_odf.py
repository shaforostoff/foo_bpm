# -*- coding: utf-8 -*-
"""Extract and cache the band ODF for every track. Restartable."""
import sys, os, json, hashlib, traceback
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from concurrent.futures import ProcessPoolExecutor, as_completed
import odf as ODF
import tango_labels as T

import config
CACHE = config.CACHE
FFMPEG = config.FFMPEG


def cpath(p):
    h = hashlib.sha1(p.encode('utf-8')).hexdigest()
    return os.path.join(CACHE, h[:2], h + '.npy')


def work(p):
    out = cpath(p)
    if os.path.exists(out):
        return (p, 'cached', None)
    try:
        o, dur = ODF.band_odf(p, FFMPEG)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        tmp = out + '.tmp.npy'   # np.save appends .npy unless present
        np.save(tmp, o.astype(np.float16))
        os.replace(tmp, out)
        return (p, 'ok', round(dur, 2))
    except Exception as e:
        return (p, 'err', str(e)[:200])


def main():
    config.ensure_work()
    recs = T.load()
    paths = [r['path'] for r in recs]
    if len(sys.argv) > 2 and sys.argv[1] == '--limit':
        paths = paths[:int(sys.argv[2])]
    os.makedirs(CACHE, exist_ok=True)
    errs, ok, cached = [], 0, 0
    with ProcessPoolExecutor(max_workers=12) as ex:
        futs = [ex.submit(work, p) for p in paths]
        for i, f in enumerate(as_completed(futs)):
            p, st, info = f.result()
            if st == 'err':
                errs.append((p, info))
            elif st == 'ok':
                ok += 1
            else:
                cached += 1
            if (i + 1) % 250 == 0:
                print(f'{i+1}/{len(paths)} ok={ok} cached={cached} err={len(errs)}', flush=True)
    print(f'DONE ok={ok} cached={cached} err={len(errs)}')
    with open(os.path.join(config.WORK, 'extract_errors.txt'), 'w', encoding='utf-8') as fh:
        for p, e in errs:
            fh.write(f'{p}\t{e}\n')
    for p, e in errs[:15]:
        print('ERR', p, e)


if __name__ == '__main__':
    main()
