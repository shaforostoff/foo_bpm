# -*- coding: utf-8 -*-
"""Honest end-to-end evaluation: rhythm classification and BPM.

Class predictions are cross-validated with the recordings grouped, so the same
performance never appears in both the training and the test half - the
collections overlap heavily, and several tracks exist as a shellac transfer, a
declicked copy and a retuned copy of the same side.
"""
import os, sys, json, collections
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import config
import tango_labels as T
from build_features import base_key
from sklearn.ensemble import HistGradientBoostingClassifier
from sklearn.model_selection import GroupKFold

CLS = T.CLASSES


def load():
    d = np.load(config.FEATURES, allow_pickle=True)
    X = np.nan_to_num(d['X'].astype(np.float64), nan=0.0, posinf=0.0, neginf=0.0)
    paths = [str(p) for p in d['paths']]
    recs = {r['path']: r for r in T.load()}
    y = np.array([CLS.index(T.label(recs[p])[0]) for p in paths])
    grp = np.array([base_key(p) for p in paths])
    root = np.array([recs[p]['root'] for p in paths])
    hand = np.array([T.hand_tapped(recs[p]) or 0.0 for p in paths])
    return X, y, grp, root, hand, paths


def confusion(title, y, pred, mask=None):
    if mask is not None:
        y, pred = y[mask], pred[mask]
    print(f'\n--- {title}   n={len(y)}   accuracy={100*(y==pred).mean():.2f}% ---')
    print(f'{"actual":10s}' + ''.join(f'{c[:7]:>9s}' for c in CLS) + '   recall')
    for i, c in enumerate(CLS):
        row = [int(((y == i) & (pred == j)).sum()) for j in range(len(CLS))]
        tot = sum(row)
        print(f'{c:10s}' + ''.join(f'{v:9d}' for v in row)
              + f'  {100*row[i]/tot if tot else 0:6.1f}%')
    prec = []
    for j in range(len(CLS)):
        pj = int((pred == j).sum())
        prec.append(100 * int(((y == j) & (pred == j)).sum()) / pj if pj else 0.0)
    print(f'{"precision":10s}' + ''.join(f'{p:8.1f}%' for p in prec))
    print(f'{"":10s}balanced accuracy '
          f'{100*np.mean([(pred[y==i]==i).mean() for i in range(len(CLS)) if (y==i).any()]):.2f}%')


def bpm_table(title, pairs):
    if not pairs:
        return
    est = np.array([a for a, b in pairs], float)
    hand = np.array([b for a, b in pairs], float)
    err = np.abs(np.round(est) - hand)
    print(f'{title:26s} n={len(err):5d}   exact={100*np.mean(err<0.5):5.1f}%'
          f'   <=1={100*np.mean(err<=1):5.1f}%   <=2={100*np.mean(err<=2):5.1f}%'
          f'   <=3={100*np.mean(err<=3):5.1f}%   <=5={100*np.mean(err<=5):5.1f}%'
          f'   right level={100*np.mean(err<=0.06*hand):5.1f}%')


def main():
    X, y, grp, root, hand, paths = load()
    print('tracks', X.shape[0], 'features', X.shape[1],
          dict(collections.Counter(CLS[i] for i in y)),
          'groups', len(set(grp)))

    pred = np.zeros(len(y), dtype=int)
    for tr, te in GroupKFold(n_splits=5).split(X, y, groups=grp):
        m = HistGradientBoostingClassifier(max_iter=150, learning_rate=0.12, max_leaf_nodes=15,
                                           l2_regularization=1.0, class_weight='balanced',
                                           early_stopping=False, random_state=0)
        m.fit(X[tr], y[tr])
        pred[te] = m.predict(X[te])

    confusion('rhythm, all collections', y, pred)
    # Collections made up entirely of tango-era transfers, where recording
    # quality is uniform and so cannot be doing the classifier's work for it.
    era = np.array([os.path.basename(r.rstrip('/' + os.sep)).lower().startswith('tango')
                    or 'tangospanish' in os.path.basename(r.rstrip('/' + os.sep)).lower()
                    for r in root])
    confusion('rhythm, tango-era collections only (same recording quality throughout)', y, pred, era)

    hyp = json.load(open(config.BPM_HYPOTHESES, encoding='utf-8'))
    print('\n=== BPM against the hand tapping ===')
    for label, chooser in (('predicted rhythm', lambda i: CLS[pred[i]]),
                           ('true rhythm', lambda i: CLS[y[i]])):
        print(f'\n  using the {label}:')
        per = collections.defaultdict(list)
        every = []
        for i, p in enumerate(paths):
            if hand[i] <= 0 or p not in hyp:
                continue
            est = hyp[p]['bpm_hyp'][chooser(i)]
            per[CLS[y[i]]].append((est, hand[i]))
            every.append((est, hand[i]))
        for c in CLS:
            bpm_table('    ' + c, per[c])
        bpm_table('    ALL', every)

    print('\n  for reference, with no rhythm classifier at all:')
    every = [(hyp[p]['bpm_hyp']['tango'], hand[i])
             for i, p in enumerate(paths) if hand[i] > 0 and p in hyp]
    bpm_table('    always assume tango', every)


if __name__ == '__main__':
    main()
