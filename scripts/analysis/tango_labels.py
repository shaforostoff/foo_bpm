# -*- coding: utf-8 -*-
"""Shared ground-truth labelling for the tango collections."""
import json, os, re

CLASSES = ['tango', 'vals', 'milonga', 'other']


def parse_bpm(rec):
    """Return (value, is_decimal). Decimal values are machine-written, not hand-tapped."""
    b = rec.get('bpm')
    if b is None:
        return None, None
    s = str(b).strip()
    if not s:
        return None, None
    dec = ('.' in s) or (',' in s)
    try:
        v = float(s.replace(',', '.'))
    except ValueError:
        return None, None
    if not (10.0 <= v <= 400.0):
        return None, None
    return v, dec


def hand_tapped(rec):
    """BPM the user tapped by hand, or None. Integers only, decimals are rejected."""
    v, dec = parse_bpm(rec)
    if v is None or dec:
        return None
    return v


# genre-tag phrases, longest/most specific first
_RULES = [
    ('milonga', ['milonga candombe', 'milonga tangueada', 'milonga criolla',
                 'milonga campera', 'milonga portena', 'tango milonga', 'milongon',
                 'milonga']),
    ('vals',    ['vals criollo', 'vals cancion', 'vals peruano', 'vals pasillo',
                 'vals serenata', 'valsecito', 'vals', 'waltz', 'walzer']),
    ('tango',   ['tango cancion', 'tango sinfonico', 'tango canyengue', 'tango negro',
                 'tango campero', 'tango electronico', 'tango nuevo', 'tango']),
]

_ACC = str.maketrans('áàâäãéèêëíìîïóòôöõúùûüñç', 'aaaaaeeeeiiiiooooouuuunc')


def _norm(s):
    return (s or '').lower().translate(_ACC)


def _match(text):
    t = _norm(text)
    for cls, phrases in _RULES:
        for p in phrases:
            if re.search(r'(?<![a-z])' + re.escape(p) + r'(?![a-z])', t):
                return cls
    return None


def label(rec):
    """Ground-truth rhythm class from the genre tag, falling back to the file name.

    The full path is deliberately NOT used: a collection directory is usually
    named after the music in it, so every file under a "TangoTunes" folder would
    otherwise match 'tango' - including the pasodobles and foxtrots.
    """
    g = rec.get('genre')
    if g:
        m = _match(g)
        if m:
            return m, 'genre'
    # File names in these collections end with " - <Genre>.<ext>"
    base = os.path.splitext(os.path.basename(rec['path']))[0]
    tail = base.rsplit(' - ', 1)[-1] if ' - ' in base else ''
    m = _match(tail)
    if m:
        return m, 'filename-tail'
    m = _match(base)
    if m:
        return m, 'filename'
    return 'other', ('genre-other' if g else 'no-genre')


def load(work_dir=None):
    """Read the tag dump produced by scan_tags.py."""
    import config
    path = os.path.join(work_dir, 'tags.jsonl') if work_dir else config.TAGS
    return [json.loads(l) for l in open(path, encoding='utf-8')]
