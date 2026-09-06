BPM Analyser for foobar2000
===========================

Change Log
----------

### Version 0.4.1

* A tapped level is now only offered where the metre has one. Two beats is not
  a position in a 3/4 bar, and offering it sent slow valses - a Peruvian vals at
  56 to the bar - to the two-beat rate instead; vals goes from 90.5% to 94.1%
  within 2 BPM of the tap. For "other", which states no metre of its own, the
  level set follows the metre the grid search found, so a duple piece is no
  longer read at two thirds of its beat: *Chan Chan* was coming out at 112
  rather than 84, and *Guantanamera* at 83 rather than 125.
* The milonga tempo prior now spans 42 to 65 to the bar rather than 44 to 63.
  The collection holds nothing above 61, so this changes no measured result; it
  makes room for a fast milonga it does not happen to contain.
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
