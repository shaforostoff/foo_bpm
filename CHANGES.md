BPM Analyser for foobar2000
===========================

Change Log
----------

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
