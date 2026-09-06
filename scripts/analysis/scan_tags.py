# -*- coding: utf-8 -*-
"""Walk the music collections and dump every tag we care about to JSONL."""
import json, os, sys
import mutagen

AUDIO_EXT = {'.mp3', '.flac', '.m4a', '.mp4', '.ogg', '.opus', '.wav', '.wma', '.aac', '.wv', '.ape'}

# Collection roots are given on the command line.


def first(v):
    if v is None:
        return None
    if isinstance(v, (list, tuple)):
        return first(v[0]) if v else None
    if isinstance(v, bytes):
        try:
            return v.decode('utf-8', 'replace')
        except Exception:
            return None
    return str(v)


def get_tags(path):
    try:
        f = mutagen.File(path)
    except Exception as e:
        return {'error': repr(e)}
    if f is None:
        return {'error': 'unrecognized'}
    out = {}
    info = getattr(f, 'info', None)
    if info is not None:
        out['duration'] = round(float(getattr(info, 'length', 0) or 0), 3)
        out['samplerate'] = getattr(info, 'sample_rate', None)
        out['channels'] = getattr(info, 'channels', None)
        out['bitrate'] = getattr(info, 'bitrate', None)
        out['codec'] = type(info).__module__.split('.')[-1]
    tags = f.tags
    if tags is None:
        return out
    keymap = {
        'bpm':    ['TBPM', 'tmpo', 'bpm', 'BPM', '----:com.apple.iTunes:BPM', 'WM/BeatsPerMinute'],
        'genre':  ['TCON', '\xa9gen', 'genre', 'GENRE'],
        'artist': ['TPE1', '\xa9ART', 'artist', 'ARTIST'],
        'title':  ['TIT2', '\xa9nam', 'title', 'TITLE'],
        'album':  ['TALB', '\xa9alb', 'album', 'ALBUM'],
        'date':   ['TDRC', 'TYER', '\xa9day', 'date', 'DATE', 'year', 'YEAR'],
        'comment': ['COMM::eng', 'COMM', '\xa9cmt', 'comment', 'COMMENT'],
    }
    flat = {}
    try:
        allkeys = list(tags.keys())
    except Exception:
        allkeys = []
    for k in allkeys:
        if not isinstance(k, str):
            continue
        try:
            v = tags[k]
        except Exception:
            continue
        flat[k.lower()] = v
    for out_key, cands in keymap.items():
        for k in cands:
            lk = k.lower()
            if lk in flat:
                out[out_key] = first(flat[lk])
                break
        else:
            if out_key in flat:
                out[out_key] = first(flat[out_key])
    return out


def main():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import config
    if len(sys.argv) < 2:
        print('usage: scan_tags.py <collection root> [more roots...]', file=sys.stderr)
        raise SystemExit(64)
    ROOTS = sys.argv[1:]
    config.ensure_work()
    outpath = config.TAGS
    n = 0
    with open(outpath, 'w', encoding='utf-8') as fh:
        for root in ROOTS:
            if not os.path.isdir(root):
                print('missing root', root, file=sys.stderr)
                continue
            for dirpath, dirnames, filenames in os.walk(root):
                for fn in filenames:
                    ext = os.path.splitext(fn)[1].lower()
                    if ext not in AUDIO_EXT:
                        continue
                    p = os.path.join(dirpath, fn)
                    rec = {'path': p, 'root': root, 'ext': ext}
                    rec.update(get_tags(p))
                    fh.write(json.dumps(rec, ensure_ascii=False) + '\n')
                    n += 1
                    if n % 500 == 0:
                        print(n, file=sys.stderr)
    print('total', n, file=sys.stderr)


main()
