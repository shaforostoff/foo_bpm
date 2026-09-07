Rubato BPM Analyzer for foobar2000
==================================

Change Log
----------

### Version 0.1.0

* Renamed. BPM Analyser is now Rubato BPM Analyzer, the component is
  `foo_rubato.dll` rather than `foo_bpm.dll`, and the version starts again at
  0.1.0. Nothing about the analysis changed with the name; 0.4.2 below and
  0.1.0 here are the same code.
* It installs alongside BPM Analyser. foobar2000 tells components apart by
  filename, and every GUID is new, so the two keep separate preferences pages,
  separate context menus and separate settings, and can be run side by side to
  compare them. Point them at different BPM tag names before doing that, or
  they will overwrite each other's answers.
* The results window shows a **BPM from tag** column when at least one of the
  scanned tracks already carried a BPM tag, so a fresh measurement can be read
  against the value that was there - a hand tap, on this collection. The column
  is absent when no track had one. It shows the string the file carried rather
  than a reformatted number, because a whole number and a decimal mean
  different things here.
* The legacy 2009 engine is deleted, and the preferences page with it. It was
  off by default and unreachable without the advanced switch, had no test
  coverage - both harnesses link bpmcore alone and it lived in the component -
  and had not been touched substantively since the 2025 SDK port. It also
  produced two wrong outputs whenever it was switched on: it filled in only a
  BPM, so the rhythm column read "Other" for every track where no classifier
  had run, and its results were stamped `BpmAlgorithm=Rubato` for a number the
  2009 algorithm produced, which defeats the point of an attribution.
* That removes 873 lines - the engine, its FFT wrapper and its maths header -
  and nine of the preferences page's controls with them: seconds per sample,
  samples per song, sample offset range, calculated BPM range, BPM candidate
  result, interpolate flux, and the three FFT settings. Every one of them was
  read only by the deleted engine; the tango engine's geometry is fixed by the
  model it was fitted to. *Calculated BPM range* was the one worth being rid
  of: it defaulted to 75-195, and a milonga at 54 or a vals at 61 sits outside
  that, so had it ever applied to the new engine it would have broken it.
* What is left on the page is what a user would actually choose: **Tagging**
  (BPM precision, tag name, write without showing the results), **Manual
  Analysis** (taps to average, reset pause), and **Diagnostics** (console
  output). The console switch was the one live control stranded in the dead
  group, and its label no longer promises a BPM candidate list. The page is
  122 dialog units tall rather than 296.
* Nothing is registered under Preferences > Advanced > Tools any more, so the
  branch is not registered either - an empty node would be worse than none.
* The about box says what the component now does rather than "automatically
  analysing the BPM of audio files", and carries a 2026 copyright for Nick
  Shaforostov alongside the original authors'.
* The foobar2000 SDK release the about box quotes is read out of
  scripts\get_sdk.ps1, where the download and its checksum are already pinned,
  instead of being a second copy of the date. CMake reconfigures when that
  script changes, so the two cannot drift the way the version once did.
* The rhythm is no longer written to a tag, and the two advanced-config entries
  that controlled it - *Write the detected rhythm to a tag* and *Rhythm tag
  name* - are gone with it rather than left doing nothing. All of it is
  commented out rather than deleted: the write in `rhythm_tag_or_empty`, the
  factories in `preferences.cpp` and their declarations in `globals.h`. An
  entry has no way to register itself and stay out of the preferences tree, so
  not registering it is what hiding it amounts to; the settings themselves are
  keyed by GUID in foobar2000's configuration and survive untouched, so
  restoring those lines restores the entries and their old values. The rhythm
  is still detected and still shown in the results window.
* The tempo a track opens at is measured, shown in the results window as
  *Initial BPM* and written to an `INITIALBPM` tag, named after the
  `INITIALKEY` other taggers write. It is the median of the first three
  autocorrelation windows - about the first 18 seconds, or a tango's
  introduction - quoted at the same metrical level as the BPM and scaled with
  it when a result is doubled or halved. On this repertoire it is a genuinely
  different number: the classic orchestras open a median 1.8 BPM above where
  they settle on shellac and 2.0 on vinyl, higher in eight sides of twelve
  either way, while the strict-tempo Orquesta Tipica Victor sides run the other
  way. On a synthesised ramp from 116 to 124 BPM it reads 117.4 where the ramp
  is at 116.6, so the figure leans toward the whole-track tempo and understates
  a real opening slightly - fitting a peak in a window whose tempo is moving
  does that, and the `tempo_spread` case pins the size of it.
* The results window shows how much the tempo moves over each track, as a
  plus-or-minus in BPM beside the BPM itself. The autocorrelation was already
  measuring the tempo of every 12-second window and throwing all but the median
  away; the figure is half the 10th-to-90th percentile span of those, so the
  middle 80% of a track sits inside it and a beatless introduction cannot set
  it. A synthesised metronome reads 0.07 and a linear ramp from 116 to 124 BPM
  reads 2.91 against the 2.88 the window geometry predicts, which is what the
  new `tempo_spread` test case checks. On the collection, milonga and vals read
  0.8 to 2.0, most tango sides 1.3 to 3.5, and Pugliese and Fresedo 3.9 to 6.5
  - which is the order a dancer would put them in. It costs nothing measurable.
* Windows the analysis could not track are dropped rather than counted at the
  edge of the search. The first attempt searched 15% either side of the settled
  beat and took whatever was best, which on a weak passage was whichever end
  the search stopped at; one Fresedo side came out at +/-14 BPM, an 11% swing,
  entirely from windows piled on the boundary. The search is 8% either side
  now - wider than any real drift, narrow enough to hold no competing
  periodicity - and a window whose autocorrelation is still climbing where the
  search ends is discarded. That side now reads 4.98 over the 27 windows that
  did track, which its trajectory bears out: about 133 BPM through the
  instrumental opening and 125 once the singer enters.
* `bpmcore_test trajectory` prints the tempo of each window, for asking why a
  particular track reads the way it does.
* Every column in the results window except the title is now sized to the
  widest string in it, header included, and the title takes what is left. The
  widths were hardcoded, and at 270 plus 50 plus 70 dialog units they already
  overflowed the 381-unit list slightly before a fourth column existed.
* An analysis now records itself in a `BpmAlgorithm` tag, written alongside
  the BPM as `Rubato;v=<version>` - the same field name shape and the same
  `<name>;v=<version>` value as the `KeyAlgorithm` and `TuningAlgorithm`
  fields other taggers write. Only a BPM the analysis stands behind gets it:
  tapping one by hand in the manual dialog, doubling or halving one from the
  context menu, and doubling or halving a result in the dialog before
  committing all remove the field instead, since the analysis no longer
  stands behind the value. Telling a measured BPM from a corrected or
  hand-tapped one no longer means guessing from whether it has a decimal
  point.
* The version has one home. It was written out twice - `project(VERSION)` in
  `CMakeLists.txt` and again in `DECLARE_COMPONENT_VERSION` - and the two had
  drifted, so 0.4.2 shipped an about box reading 0.4.1. CMake now generates a
  `version.h` from the project version, and the about box, the `BpmAlgorithm`
  tag and the release archive name all read it from there.
* Settings do not carry over, for the same reason. An existing BPM Analyser
  install keeps its own configuration and this one starts at its defaults, so
  the BPM tag name, the rhythm tag name and the STFT settings all need setting
  again if they were ever changed.

Earlier releases, as BPM Analyser
---------------------------------

### Version 0.4.2

* Files at 48kHz are analysed correctly. The onset envelope's geometry is fixed
  in seconds, which gets the window and hop right in time at any rate, but the
  window has to be a power of two as well - and at 48kHz that is 2048 points
  covering 42.7ms where the geometry asks for 46.4, with the six band edges on
  different bins. A 48kHz file and a 44.1kHz transfer of the same side were two
  different analyses, and could disagree on the metre and the rhythm. Anything
  whose rate is not 22.05kHz times a power of two is now resampled to 22.05kHz
  first, which is the rate the model was fitted at; 32kHz, 96kHz and 192kHz were
  wrong for the same reason and are fixed with it.
* The resampler is a rational polyphase FIR with a Kaiser-windowed sinc, 80dB of
  alias rejection and a passband flat to 0.01% out to 8kHz. It is cheap because
  the envelope never reads above 8kHz, so it only has to be clean from 14kHz up
  rather than from 11kHz up - six kilohertz of transition band instead of one.
* Audio is downmixed and resampled as the decoder produces it rather than
  afterwards, so what is held is bounded by the track's duration and not by its
  sample rate. A long file at 192kHz used to need 690MB of buffer and now needs
  79MB.
* Faster, with no change to any answer. The autocorrelation and the resampler
  are now spread across cores as the envelope already was, and all three give
  the same result at any thread count. For a 169-second track on all cores:
  22.05kHz 0.055s to 0.039s, 32kHz 0.103s to 0.059s, 48kHz 0.122s to 0.057s.
  48kHz is now quicker than 44.1kHz despite the extra stage, because it is
  analysed at 22.05kHz where the transform is a quarter of the size.
* Tracks are opened for a sequential read, so a decoder need not build a
  seektable that will never be used, and a file carrying looping metadata is no
  longer decoded round and round until the length cap stops it.
* With *output debug information* on, each track now logs how long it took to
  read and how long to analyse, as separate numbers. Reading is usually the
  larger of the two by a wide margin - a three-minute side is around 0.05s of
  analysis - so this is the first thing to look at if a scan feels slow.
* The transform size is now the nearest even number with no prime factor above 5
  rather than the nearest power of two. Every rate that reaches the analysis is
  resampled to one where those are the same value, so nothing measured changes;
  it keeps the window near 46.4ms on the one path left over, where a ratio the
  resampler cannot approximate means the track is analysed at its own rate.

### Version 0.4.1

* A tapped level is now only offered where the metre has one. Two beats is not
  a position in a 3/4 bar, and offering it sent slow valses - a Peruvian vals at
  56 to the bar - to the two-beat rate instead; vals goes from 90.5% to 94.1%
  within 2 BPM of the tap. For "other", which states no metre of its own, the
  level set follows the metre the grid search found, so a duple piece is no
  longer read at two thirds of its beat: *Chan Chan* was coming out at 112
  rather than 84, and *Guantanamera* at 83 rather than 125.
* Overall: within 2 BPM of the tap on 89.0% of 3,664 hand-tapped tracks, right
  metrical level on 96.6%.

### Version 0.4.0

* New tempo engine, aimed at Argentine tango. It reports the tempo on the
  metrical level a dancer taps - the beat for a tango, the bar for a vals or a
  milonga - which needs the rhythm, so the rhythm is detected first. Against
  3,664 hand-tapped tracks it lands within 2 BPM of the tap 88.5% of the time
  and picks the right metrical level for 96.2%; the previous behaviour of
  treating everything alike managed 66.9% within 2 BPM.
* New rhythm classifier: Tango, Vals, Milonga or other, from the audio alone,
  with no reference to the genre tag. 94.1% accurate over 12,118 tracks. The
  result is shown in the results dialog and written to a `RHYTHM` tag.
* The analysis is now a standalone library, `bpmcore`, with no foobar2000,
  Windows or ATL dependency, so it can be reused elsewhere and on other
  platforms. The component is a shell around it.
* Whole tracks are analysed rather than a few short excerpts, and local tempo is
  reduced with a median across overlapping windows, so a passage that drifts,
  a rubato phrase or a beatless introduction no longer moves the answer.
* Onset detection is now per frequency band. The old broadband flux was weighted
  towards the top of the spectrum, which on a shellac transfer is surface noise.
* The spectral stage is spread across cores, with a result that is identical
  whatever the thread count, and only the frequency bins the analysis actually
  uses leave the transform. A 48kHz file is about twice as fast as before for a
  second reason: its analysis window was being rounded up to 4096 points, which
  also made it a different analysis from the same track at 44.1kHz.
* The 2009 algorithm is still available: *Preferences > Advanced > Tools > BPM
  Analyser > Use the legacy BPM engine*. The preferences page's STFT and
  candidate-selection controls only apply to it.

### Version 0.3.0

* 64 bit support: the component now loads in 64 bit foobar2000 2.x. One
  .fb2k-component file carries both architectures.
* Ported from the 2011-03-11 SDK to the 2025-03-07 SDK, and from the Visual
  Studio 2010 project files to CMake.
* Fixed a crash analysing tracks whose decoder reports a sample rate below
  100Hz, or hands back less audio than one FFT window.
* Fixed the BPM tag name being read from the preferences page as ANSI into a
  fixed size buffer: non-ASCII tag names were mangled, and a long enough name
  overran the buffer.
* The "output debug information" preference now controls the per-track
  diagnostics that were previously compiled out.
* Peak picking sorts with std::sort rather than a bubble sort.

### Version 0.2.4.6

* Show tag progress window delayed
* Refactored tag writing for doubling and halving BPM tag

### Version 0.2.4.5

* Windows 7 taskbar indicates total track progress
* Fixed automatic tag writing

### Version 0.2.4.4

* Replaced FFTW with KISS FFT

### Version 0.2.4.3

* Preferences page fresh up
* Enabled dialog navigation in result and manual dialogs
* Introduced wrapper for FFTW to prepare replacement

### Version 0.2.4.2

* Fixed abort checks in worker thread
* Added basic exception handling in worker thread
* Numerous refactorings

### Version 0.2.4.1

* Numerous bug fixes

### Version 0.2.4

* Crash report fix (component about message)

### Version 0.2.3

* Updated to foobar2000 1.0 SDK
* Added double/halve buttons to results dialog
* Added option to auto write tags after analysis
* Limit preference range inputs
* Crash report fix (using info not yet cached)

### Version 0.2.2

* Crash report fix
* Candidate bpm selection can be mode, mean, median added to preferences
* Debug output added to preferences

### Version 0.2.1

* Miscellaneous bug fixes

### Version 0.2.0

* Added confirmation to rescan already tagged files
* Added ReplayGain style results dialog
* Added preferences page (with destination BPM tag)
* Added manual bpm calculation window

### Version 0.1.1

* Refactored code (with processing time decrease)
* Initial source code release

### Version 0.1.0

* Initial release
