Tango tempo and rhythm analysis
===============================

How `bpmcore` works, why it is built the way it is, and how well it does.

The short version: the tempo a dancer taps is not a property of the audio alone
— it depends on which rhythm is playing, because tango, vals and milonga are
tapped on different metrical levels. So the analysis settles the rhythm first
and reports the tempo on the level that rhythm implies.


What the reference data says
----------------------------

Everything here was derived from and measured against a collection of about
12,000 tracks with genre tags, of which **3,664 carry a hand-tapped BPM**. The
tapping is the ground truth for tempo; the genre tag is the ground truth for
rhythm, and is never consulted at analysis time.

Two things fell out of the tapped values immediately.

**The tapping convention differs per rhythm.** Grouped by genre, the hand-tapped
values are strikingly tight, and they sit on three different metrical levels:

| rhythm  |   n  | median | p5–p95  | what is being tapped        |
|---------|-----:|-------:|---------|-----------------------------|
| tango   | 2732 |    126 | 116–136 | the beat (quarter note)     |
| vals    |  455 |     69 |  58–76  | the 3/4 bar                 |
| milonga |  425 |     53 |  41–59  | the 2/4 bar                 |

Anchoring on the tapped value itself and asking which multiple of that period
the autocorrelation likes best confirms it: for tango the tapped period *is* the
strongest short periodicity, for vals the audio's beat sits at one third of it,
and for milonga at one half or one quarter.

**Human tapping repeats to about ±2 BPM.** 469 recordings appear more than once
in the collection — a shellac transfer, a declicked copy, a different
compilation — and were tapped independently each time. Comparing those pairs:

| tap-to-tap difference | share |
|-----------------------|------:|
| identical             |  41%  |
| within 1 BPM          |  68%  |
| within 2 BPM          |  84%  |
| within 3 BPM          |  93%  |

That is the ceiling. An estimator that agrees with a tap to within 2 BPM is
already as close as the same person tapping the same track twice.


The pipeline
------------

### 1. Onset envelope (`odf.cpp`)

Mono, normalised by the track's overall RMS, then a short-time Fourier transform
with a 46ms window every 11.6ms. Magnitudes are compressed as `log(1 + 100·m)`,
differenced between adjacent frames, half-wave rectified and summed into **six
frequency bands** (60, 200, 400, 800, 1600, 3200, 8000 Hz).

Six bands rather than one broadband figure matters here. The original 2009
analysis summed a flux weighted by bin index, which puts most of the weight at
the top of the spectrum — on a 1935 shellac transfer that is surface noise, so
the beat being tracked was largely hiss. Keeping the bands apart lets the tempo
stage normalise each one by its own variation before mixing, and gives the
rhythm classifier something to read.

The geometry is fixed in *seconds*, not samples, and the band edges in Hz,
because a component cannot assume a resampler is available. The window is the
nearest even 5-smooth number of samples to 46.4ms, so it lands within 1% of that
at any rate - 2048 at 44.1kHz, 2250 at 48kHz, 1500 at 32kHz. Restricting to
factors of 2, 3 and 5 is what keeps the transform fast; rounding to a power of
two instead, as this once did, gives 48kHz a 42.7ms window and 32kHz a 64ms one.

That is close, not identical. A file at 22.05kHz and the same file at 44.1kHz do
produce the same envelope, because the sizes are exactly proportional; a 48kHz
file does not, and on a sample of 40 native 48kHz tracks the rhythm it is given
differs from the 22.05kHz reading about one time in twelve. Making that exact
needs a resampler in front of the analysis, which is not there yet. 8% of the
reference collection is 48kHz.

### 2. Tempo (`tempo.cpp`)

The bands are normalised and summed, smoothed, local-mean-subtracted and
rectified into a novelty curve; then autocorrelated over **12-second windows
every 3 seconds**, and the windows reduced lag by lag with a **median**.

The median is the part that matters for this repertoire. These are human
performances, often from shellac: the tempo drifts, a singer stretches a phrase,
a bandoneon variation drops behind for a few bars. Taking the median across
windows lets those pass without dragging the answer down — which is what the
hand tapping did too. It also handles a beatless introduction: those windows
simply carry no periodicity and the median ignores them.

A joint search over (beat period, meter) then picks the grid, scoring each
candidate by the autocorrelation at the beat, the bar, two bars and the
subdivision the meter implies, with a penalty for leaving a strong periodicity
off the grid. The period is sharpened against the first eight multiples of
itself at once — a single autocorrelation peak is a couple of frames wide, its
harmonics are not.

### 3. Rhythm (`rhythm.cpp`)

358 features, all metrical rather than timbral:

* the autocorrelation sampled at 24 musically meaningful multiples of the beat
  (the thirds expose a 3/4 bar, the halves and quarters the habanera
  subdivision, the long ones the phrase structure);
* explicit 3-against-4 and 6-against-4 contrasts;
* **bar-synchronous patterns**: one bar of onset energy per band, folded and
  rotated so the strongest low-band accent lands in bin 0. Folded at the
  detected bar length, at one beat, and — so a mis-read meter cannot corrupt the
  whole picture — at a fixed 2, 3 and 4 beats regardless;
* per-band flux share and variability, which is mostly what separates a shellac
  side from a modern cortina;
* novelty curve kurtosis and skew: a sharply articulated marcato and a smooth
  legato line look very different at the same tempo.

Gradient boosted trees over those features (150 iterations, 15 leaves, five
classes: tango, vals, milonga, swing, other). Logistic regression on the same features reaches only 85% — the
interactions are real — so the trees are exported verbatim into
`bpmcore/rhythm_model.h` and walked directly. Thresholds and leaf values are
stored as `double`: rounding a threshold to `float` is enough to send a feature
down the other side of a split.

**Swing** is the fifth class, and the loosest. It gathers the foxtrots the tango
orchestras recorded alongside their tangos and the swing-era dance band music
among the cortinas - 167 tracks, against 8,523 tangos. Foxtrot and swing are put
together deliberately: the foxtrot is the *dance*, swing is the *idiom* that
filled it, and 1930s labels printed "Fox Trot" on records we would now file under
swing. What separates them is phrasing and improvisation, which these features do
not look at; what they share is the metre, which is all these features see.

### 4. The tapped level (`tempo.cpp`, `tapped_bpm`)

Given the rhythm, the beat period is projected onto the levels that rhythm
allows — powers of two for tango and milonga, thirds and sixths for vals — and
each candidate is scored by its autocorrelation support plus a log-normal tempo
prior fitted to the hand tapping.

The prior only ever chooses between levels, which are a factor of at least 1.33
apart. **The value itself always comes from the autocorrelation peak**, so a
prior cannot pull a tempo towards its mean. That separation is deliberate: the
priors are tight (tango is 125.5 with a log sigma of 0.075) and would otherwise
flatten every tango to the same number. Milonga is the widest of the three, at
52.5 with a log sigma of 0.101, which puts its 5th to 95th percentile at 44 to
62 to the bar.

How wide that one is turns out not to matter: anything from 0.07 to 0.13 gives
the same answer on every track in the collection, because the levels a milonga
chooses between are an octave apart and four sigma still does not reach half way.
The number is set to say what a milonga's bar rate is, not to tune a result.

**The levels on offer have to be levels of the metre.** Two beats is not a
metrical position in a 3/4 bar, and while it was offered a slow vals could be
reported at the two-beat rate instead of the bar: a Peruvian vals at 56 to the
bar sits below anything the Argentine prior expects, so the prior stops
defending the right answer and a spurious two-beat periodicity wins. Removing
that one level took vals from 90.5% to 94.1% within 2 BPM.

"Other" is the exception in two ways. It spans bossa to disco to chacarera and
has no useful tempo prior, so the autocorrelation is weighted nearly four times
as heavily and the prior only breaks ties. And because the class states no metre
of its own, the level set follows the metre the grid search found — duple or
triple — rather than allowing both. Allowing both is what let a son cubano be
read at two thirds of its beat: *Chan Chan* came out at 112 and *Guantanamera*
at 83, neither of which is a rate anything in those recordings moves at.

A constant **+0.5 BPM** is added at the end. Taps run marginally ahead of the
measured pulse across the whole collection; this is a calibration to that habit,
not a correction to the measurement.


Results
-------

Rhythm classification, 5-fold cross-validated with recordings **grouped**, so
the same performance never appears in both halves (the collections overlap
heavily, and many sides exist as a transfer, a declicked copy and a retuned
copy):

```
n=12118   accuracy=93.22%   balanced=78.46%

actual        tango     vals  milonga    swing    other   recall
tango          8374       27       22        2       98    98.3%
vals             22      935        8        4       37    92.9%
milonga          65       18      623        5       42    82.7%
swing             9       17        6       68       67    40.7%
other           202       84       65       22     1296    77.7%
precision     96.6%    86.5%    86.0%    67.3%    84.2%
```

Restricted to the tango-era collections alone - where every track is a shellac
transfer, so recording quality cannot be doing the work - accuracy is 93.94%.

Swing is the weak one, and the trade is worth stating plainly. Adding it costs
about nine tenths of a point of overall accuracy, all of it in the swing row and
in "other" losing tracks to it; the three tango rhythms are untouched, and vals
and milonga each move up a little. Two thirds of what it calls swing is swing,
but it finds only two fifths of what is there - and almost everything it misses
goes to "other" rather than to a tango rhythm, which is the failure to prefer.
The cause is the class size: 167 examples against 8,523 tangos. A few hundred
labelled foxtrots would make this a strong class.

BPM against the 3,664 hand-tapped tracks, using the **predicted** rhythm:

| rhythm  |   n  | exact | ≤1 BPM | ≤2 BPM | ≤3 BPM | right level |
|---------|-----:|------:|-------:|-------:|-------:|------------:|
| tango   | 2732 | 34.6% |  74.6% |  88.4% |  93.7% |       98.3% |
| vals    |  455 | 44.6% |  87.9% |  95.6% |  96.0% |       96.5% |
| milonga |  425 | 41.4% |  80.7% |  88.2% |  88.9% |       88.9% |
| other   |   52 | 34.6% |  71.2% |  80.8% |  82.7% |       84.6% |
| **all** | 3664 | 36.6% |  76.9% |  89.1% |  93.3% |       96.8% |

Swing has no row because it has no hand taps: not one foxtrot in the collection
carries a tapped BPM, so there is no tapping convention to fit and its prior is
a placeholder that leaves the choice to the autocorrelation.

Set against the tap-to-tap repeatability above (68% within 1, 84% within 2, 93%
within 3), the estimator agrees with a tap about as closely as the same person
tapping twice.

The rhythm classifier is what buys most of this. Skipping it and treating every
track as a tango gives 66.9% within 2 BPM instead of 89.1%.

### Where it still misses

* **Metrical level, ~4% of tracks.** Almost all of these are cases where the tap
  itself sat on an unusual level — around 20 milongas tapped on the beat rather
  than the bar, a dozen tangos tapped at half rate. Nothing in the audio
  distinguishes them; the same recording tapped on another day would land
  differently.
* **Milonga recall, 83%.** Milonga is the smallest of the tango rhythms and
  shades into candombe and *milonga tangueada*, which are genuinely intermediate.
* **Swing recall, 41%.** Too few examples, and it competes with everything else
  in 4/4. It rarely loses a foxtrot to a tango rhythm, which is what matters.
* **48kHz input.** See the note under the onset envelope: the geometry is within
  1% at any rate, but exact only where the sizes are proportional.
* **"Other" tempo.** With 52 tapped examples spanning Glenn Miller to Daft Punk
  there is no convention to learn, and what is left is an octave choice with
  nothing to settle it: *Bitter Sweet Symphony* and *La Tanga* have beats within
  1 BPM of each other and were tapped at opposite levels, 85 and 171. Both
  readings are defensible and the engine can only be right about one of them.
  Interestingly the *predicted* rhythm does better here than the true one (80.8%
  vs 65.4% within 2 BPM): the candombes tagged "other" get classified as
  milonga, and the milonga prior then puts them on the level they were actually
  tapped on.
* **Beat search floor, ~96 BPM.** Eight multiples of the beat have to fit inside
  the five-second autocorrelation. Every rhythm here sits well above that — a
  tango beat is 110–140, a vals beat around 205 — but a genuinely slow piece is
  found through a subdivision and divided back down.


Performance
-----------

Measured on a 169-second track, one core of a Ryzen 7 PRO 250, and after the
optimisations below:

| input rate | 1 thread | 2 threads | all cores |
|------------|---------:|----------:|----------:|
| 22050      |   0.137s |    0.089s |    0.053s |
| 44100      |   0.207s |    0.128s |    0.073s |
| 48000      |   0.241s |    0.150s |    0.086s |

The spectral stage is 90–97% of the run time; nothing else is worth optimising
until it is. What was done:

* **Only the used bins leave the transform.** The bands stop at 8kHz, so on a
  44.1kHz file 370 bins of the 1025 produced are turned into magnitudes. The
  logarithm is the single most expensive operation in the loop.
* **The window is the nearest even 5-smooth size, not the nearest power of two.**
  It has to last 46.4ms at every rate or the analysis is not the one the model
  was trained on, and powers of two cannot do that: 48kHz gets 42.7ms and 32kHz
  64ms. Sizes whose factors are 2, 3 and 5 are the ones kiss_fft has butterflies
  for, so the constraint costs little - 44.1kHz still lands exactly on 2048, and
  48kHz pays about 15% for its 2250.
* **Two tight loops, not one fused one.** Computing the whole span of logarithms
  and differencing afterwards measured a third faster than interleaving them:
  the transcendental loop pipelines cleanly only when nothing else is storing
  alongside it.
* **`log(1 + z)` rather than `log1p(z)`.** At these magnitudes the difference
  never reaches the sums, and `log` is measurably faster.
* **Optional threading over frame blocks.** Blocks are handed out from a shared
  counter, and each block re-derives the frame before its first, so the envelope
  is **bit-identical whatever the thread count** — verified in the test harness.
  On two cores this is worth about 1.55×.

Deliberately *not* done: decimating a 44.1kHz input to 22.05kHz before the
transform. It would halve the transform, but a decimating FIR good enough to
keep aliasing out of the 3200–8000Hz band costs about as much as it saves.


Reproducing the model
---------------------

See `scripts/analysis/README.md`. The pipeline regenerates
`bpmcore/rhythm_model.h` and `bpmcore_test/reference_cases.tsv`; the
`rhythm_model` CTest case then checks the exported trees still reproduce the
classifier they came from.
