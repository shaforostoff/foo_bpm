Cortina genres beyond reggae
============================

Reggae became the fifth rhythm class (see `docs/tango-analysis.md`). Disco,
funk, cumbia and salsa were measured at the same time and left alone. This is
what the measurements said, so that picking them up later starts from evidence
rather than from scratch.

The short version: **reggae was the only one of the five with a tempo problem,
and the only one the existing features can find.** The others are already
reported at the tempo they are tapped at, and separating them is a timbre
problem, which this engine deliberately does not look at.


What was measured
-----------------

All 1,153 tracks under `C:\cortinas`, of which 1,152 produced an envelope (one
Patsy Cline MP3 has an ID3 block where its RIFF header should be and ffmpeg
refuses it). Labels came from the folder, taking the deepest folder word, so
`disco/funk` is funk and `latino/cumbia/bardo` is cumbia.

| folder | tracks | hand taps |
|--------|-------:|----------:|
| disco  |     89 |         2 |
| reggae |     67 |        14 |
| cumbia |     46 |         6 |
| funk   |     33 |         0 |
| salsa  |     27 |         3 |
| rest   |    890 |        30 |

The rest is not a negative class. `cortinas` is organised partly by genre and
partly by how a track functions in a milonga — `melodic`, `playful`,
`support-casual`, `careful`, `cool_slow` — and those folders certainly hold
disco, funk and reggae sides too. Every recall figure below is therefore a
floor, and every precision figure is pessimistic.


Does it have a tempo problem?
-----------------------------

What the engine reported before the reggae class existed, against the hand taps
in each folder. All of these took the `other` path, so this is the near-flat
prior deciding the metrical level on autocorrelation support alone.

| folder | taps | within 2 BPM | reported at 2× |
|--------|-----:|-------------:|---------------:|
| reggae |   14 |            4 |         **10** |
| disco  |    2 |            2 |              0 |
| cumbia |    6 |            5 |              0 |
| salsa  |    3 |            2 |              0 |
| funk   |    0 |            — |              — |
| rest   |   30 |           19 |              3 |

Reggae is the outlier and the reason is structural: it is the only one of the
five whose tapping convention is not the beat. Disco, cumbia and salsa are
tapped on the beat, and the beat is exactly what the `other` prior already
reports, so a class for them would buy a label and not a better number.

Two disco taps and three salsa taps are not much to conclude from. If these are
picked up later, tap thirty of each first — that is a morning's work and it
settles the question the cheap way. The specific thing to look for is a genre
whose taps cluster at half or twice the beat the engine finds, which is what
made reggae worth a class.


Can the existing features separate them?
----------------------------------------

Grouped 5-fold over the 1,152, features from `features.npz` unchanged,
`HistGradientBoostingClassifier(max_iter=150, max_leaf_nodes=15,
learning_rate=0.1)`, `StratifiedGroupKFold` grouped by `build_features.base_key`
so a track that exists twice cannot appear in both halves.

| class  |   n | recall | precision |
|--------|----:|-------:|----------:|
| reggae |  67 | 44.8%  |   78.9%   |
| disco  |  89 | 18.0%  |   53.3%   |
| cumbia |  46 |  6.5%  |   37.5%   |
| salsa  |  27 |  3.7%  |   50.0%   |
| funk   |  33 |  3.0%  |   33.3%   |
| rest   | 890 | 97.5%  |   81.0%   |

Accuracy 79.8%, which is only the 890 carrying it.

Reggae separates and the rest do not, and the reason is what the features are.
346 of the 358 are metrical — where the accents fall, folded over a bar, a beat
and a fixed 2, 3 and 4 beats. Only 12 are timbral, the per-band flux share and
variability. Reggae is *defined* by where the accents fall: a hole on beat one,
the kick on three, the skank on the offbeats. That is a fold pattern, and the
same machinery separates the habanera from the tango marcato.

Disco and funk are four-on-the-floor, and so is a good deal of what sits in
`rest`. What marks them is instrumentation, production and the century they were
recorded in. Cumbia and salsa do carry a clave and a tumbao that the fold
patterns partly see, but not at these counts.

The six tracks in `rest` that the model called reggae are worth knowing about,
because at least one is not an error: Grace Jones, *I've Seen That Face Before*
— Compass Point, reggae in everything but the tag. The others were a flamenco
side, a 50 Cent track, two Morricone pieces and a Beatles track.


The labels are worse than the counts look
-----------------------------------------

`C:\Dev\genre_findings.json` — produced by `genre_findings.py`, written into
files by `apply_genres.py` — covers 171 tracks across `reggae`, `latino/salsa`,
`latino/cumbia` and `jazz`, at four confidence levels. It changes the picture:

* **salsa: 8 of the 27 are salsa.** The folder also holds five bachata, two
  Afro-Cuban, and one each of reggaeton, sertanejo, guaracha, mambo, boogaloo
  and cha-cha-cha, with six unidentified. The 3.7% recall above was measured
  against a label that is mostly wrong, so it says nothing about salsa. The real
  count available is eight.
* **cumbia: 31 of the 40 are cumbia**, plus 6 more under `bardo`. Usable.
* **reggae: 23 of the 25 covered were confirmed reggae**, the other two
  unidentified rather than wrong. The folder as a whole was confirmed by hand.
* **disco and funk are not covered at all.** Their 89 and 33 are folder names
  and nothing more.

So before anything else: a genre that is going to be a class needs its labels
checked one by one, the way the reggae folder was. A folder name is a hypothesis.


What it would actually take
---------------------------

For a genre with no tempo problem, a class buys a name in the results window's
Rhythm column and nothing else — the rhythm is not written to a tag at all
today, only the BPM, `INITIALBPM` and `BpmAlgorithm` are. Worth being clear
about that before spending on it.

If it is still wanted:

1. **Labels.** Several hundred per class, verified. Milonga gets 83% recall
   from 753 examples and is the weakest of the four named rhythms; 30 verified
   salsa sides will not produce a class anyone would trust.
2. **Timbral features.** Something describing spectral shape rather than
   accent placement — the usual answer is cepstral coefficients and their
   variation over the track. This is a real change in what the engine is: the
   metrical-only feature set is why a 1935 shellac transfer and a modern
   cortina of the same rhythm land in the same class, and why the classifier
   generalises across recording eras at all. Adding timbre risks trading that
   away, and the tango-era-only figures in `tango-analysis.md` are the check
   that would catch it.
3. **A tempo prior per class, or no class.** The priors are the mechanism by
   which a rhythm earns its place. A class whose prior is the same shape as
   `other` changes no tempo and should be a genre tagger instead — which is a
   different program, and `genre_findings.py` is already most of the way to
   being one.


Candombe
--------

Nothing to do. Candombes tagged `other` already classify as milonga, and that is
what puts their BPM on the level they were tapped at — the milonga prior does
the work. Splitting candombe out would cost milonga recall, already the weakest
of the four at 83%, and would gain no tempo.
