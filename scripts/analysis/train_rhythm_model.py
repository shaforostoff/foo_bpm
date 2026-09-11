# -*- coding: utf-8 -*-
"""Fit the shipping rhythm classifier and emit it as a C++ header.

The tree walk is re-implemented here in plain Python first and checked against
sklearn's own decision_function, so that what the header encodes is known to be
the same model before any of it reaches C++.
"""
import os, sys, collections
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import config
import tango_labels as T, features as F
from sklearn.ensemble import HistGradientBoostingClassifier

CLS = T.CLASSES
OUT = config.MODEL_HEADER


def walk(trees_flat, offsets, targets, baseline, x, n_cls):
    s = list(baseline)
    for t in range(len(offsets)):
        n = offsets[t]
        i = 0
        while trees_flat[n + i][0] >= 0:
            feat, left, right, thr, val = trees_flat[n + i]
            i = left if x[feat] <= thr else right
        s[targets[t]] += trees_flat[n + i][4]
    return s


def main():
    d = np.load(config.FEATURES, allow_pickle=True)
    X = np.nan_to_num(d['X'].astype(np.float64), nan=0.0, posinf=0.0, neginf=0.0)
    paths = list(d['paths'])
    recs = {r['path']: r for r in T.load()}
    y = np.array([CLS.index(T.label(recs[p])[0]) for p in paths])
    print('fitting on', X.shape, dict(collections.Counter(CLS[i] for i in y)))
    assert X.shape[1] == len(F.FEATURE_NAMES)

    clf = HistGradientBoostingClassifier(max_iter=150, learning_rate=0.12, max_leaf_nodes=15,
                                         l2_regularization=1.0, class_weight='balanced',
                                         early_stopping=False, random_state=0)
    clf.fit(X, y)
    print('train accuracy', (clf.predict(X) == y).mean())

    baseline = np.ravel(np.asarray(clf._baseline_prediction, dtype=np.float64))
    n_cls = len(CLS)
    if baseline.size == 1:
        baseline = np.repeat(baseline, n_cls)
    assert baseline.size == n_cls, baseline.shape

    flat, offsets, targets = [], [], []
    for it in clf._predictors:
        for k, pred in enumerate(it):
            nodes = pred.nodes
            offsets.append(len(flat))
            targets.append(k)
            for nd in nodes:
                if nd['is_leaf']:
                    flat.append((-1, 0, 0, 0.0, float(nd['value'])))
                else:
                    flat.append((int(nd['feature_idx']), int(nd['left']), int(nd['right']),
                                 float(nd['num_threshold']), 0.0))
    print(f'trees={len(offsets)} nodes={len(flat)}')

    # Parity check against sklearn itself.
    idx = np.random.RandomState(0).choice(len(X), 400, replace=False)
    ref = clf.decision_function(X[idx])
    if ref.ndim == 1:
        ref = np.column_stack([-ref, ref])
    worst = 0.0
    for j, i in enumerate(idx):
        mine = walk(flat, offsets, targets, baseline, X[i], n_cls)
        worst = max(worst, float(np.max(np.abs(np.array(mine) - ref[j]))))
    print(f'max |mine - sklearn decision_function| over 400 tracks = {worst:.3e}')
    assert worst < 1e-6, 'exported trees do not reproduce the model'

    def chunk(lines, per):
        for i in range(0, len(lines), per):
            yield lines[i:i + per]

    b = []
    b.append('#ifndef BPMCORE_RHYTHM_MODEL_H')
    b.append('#define BPMCORE_RHYTHM_MODEL_H')
    b.append('')
    b.append('// GENERATED FILE - do not edit by hand.')
    b.append('// Produced by scripts/train_rhythm_model.py; see docs/tango-analysis.md.')
    b.append('//')
    b.append('// Gradient boosted decision trees over the features built by')
    b.append('// bpmcore::build_features. Class order is '
             + ', '.join(c.capitalize() for c in CLS) + ',')
    b.append('// matching bpmcore::rhythm_class.')
    b.append('//')
    b.append(f'// Fitted on {X.shape[0]} hand-labelled tracks, {X.shape[1]} features,')
    b.append(f'// {len(offsets)} trees, {len(flat)} nodes.')
    b.append('')
    b.append('namespace bpmcore')
    b.append('{')
    b.append('namespace rhythm_model')
    b.append('{')
    b.append('\t//! An interior node compares one feature against a threshold and')
    b.append('\t//! branches; a leaf has feature < 0 and carries the score to add.')
    b.append('\tstruct node')
    b.append('\t{')
    b.append('\t\tshort feature;')
    b.append('\t\tshort left;')
    b.append('\t\tshort right;')
    b.append('\t\t// Both are double so the walk branches exactly where')
    b.append('\t\t// scikit-learn branched: rounding a threshold to float')
    b.append('\t\t// can send a feature down the other side of a split.')
    b.append('\t\tdouble threshold;')
    b.append('\t\tdouble value;')
    b.append('\t};')
    b.append('')
    b.append(f'\tconst int feature_count = {X.shape[1]};')
    b.append(f'\tconst int class_count = {n_cls};')
    b.append(f'\tconst int tree_count = {len(offsets)};')
    b.append(f'\tconst int node_count = {len(flat)};')
    b.append('')
    b.append(f'\tconst double baseline[{n_cls}] = {{ '
             + ', '.join(f'{v:.10e}' for v in baseline) + ' };')
    b.append('')
    b.append(f'\tconst int tree_offset[{len(offsets)}] = {{')
    for c in chunk([f'{v}' for v in offsets], 16):
        b.append('\t\t' + ', '.join(c) + ',')
    b[-1] = b[-1].rstrip(',')
    b.append('\t};')
    b.append('')
    b.append(f'\tconst short tree_target[{len(targets)}] = {{')
    for c in chunk([f'{v}' for v in targets], 32):
        b.append('\t\t' + ', '.join(c) + ',')
    b[-1] = b[-1].rstrip(',')
    b.append('\t};')
    b.append('')
    b.append(f'\tconst node nodes[{len(flat)}] = {{')
    for f_, l_, r_, t_, v_ in flat:
        b.append(f'		{{ {f_}, {l_}, {r_}, {t_!r}, {v_!r} }},')
    b[-1] = b[-1].rstrip(',')
    b.append('\t};')
    b.append('}   // namespace rhythm_model')
    b.append('}   // namespace bpmcore')
    b.append('')
    b.append('#endif // BPMCORE_RHYTHM_MODEL_H')
    with open(OUT, 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(b) + '\n')
    print('wrote', OUT, os.path.getsize(OUT), 'bytes')

    # Reference features + expected output, for the C++ parity test.
    sel = list(range(0, len(paths), max(1, len(paths) // 60)))[:60]
    # An even stride over a set this lopsided can miss a small class entirely -
    # reggae is 68 of 12,165 - and then nothing checks that the C++ agrees about
    # what that class is called. Top up to two cases each.
    for c in range(len(CLS)):
        have = [i for i in sel if y[i] == c]
        for i in np.flatnonzero(y == c)[:max(0, 2 - len(have))]:
            sel.append(int(i))
    sel = sorted(set(sel))
    cases = config.REFERENCE_CASES
    with open(cases, 'w', encoding='utf-8') as fh:
        for i in sel:
            probs = clf.predict_proba(X[i:i + 1])[0]
            fh.write(paths[i] + '\t' + CLS[y[i]] + '\t' + CLS[int(np.argmax(probs))] + '\t'
                     + f'{probs.max():.6f}' + '\t'
                     + ' '.join(repr(float(v)) for v in X[i]) + '\n')
    print('wrote reference_cases.tsv')


if __name__ == '__main__':
    main()
