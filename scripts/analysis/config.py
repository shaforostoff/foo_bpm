# -*- coding: utf-8 -*-
"""Where the pipeline keeps its intermediates and finds its tools.

Everything is overridable so the pipeline can be pointed at another collection
without editing code:

    TANGO_WORK    scratch directory for tags.jsonl, the ODF cache and features
                  (default: <repo>/build/analysis)
    TANGO_FFMPEG  ffmpeg executable (default: whatever is on PATH)
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

WORK = os.environ.get('TANGO_WORK') or os.path.join(REPO, 'build', 'analysis')
FFMPEG = os.environ.get('TANGO_FFMPEG') or 'ffmpeg'

CACHE = os.path.join(WORK, 'cache')
TAGS = os.path.join(WORK, 'tags.jsonl')
FEATURES = os.path.join(WORK, 'features.npz')
BPM_HYPOTHESES = os.path.join(WORK, 'bpm_hypotheses.json')

#! Generated artefacts that land in the source tree.
MODEL_HEADER = os.path.join(REPO, 'bpmcore', 'rhythm_model.h')
REFERENCE_CASES = os.path.join(REPO, 'bpmcore_test', 'reference_cases.tsv')


def ensure_work():
    os.makedirs(WORK, exist_ok=True)
    os.makedirs(CACHE, exist_ok=True)
    return WORK
