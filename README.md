BPM Analyser for foobar2000
===========================

Originally written by Michael Balzer.

Bug fixes and refactoring by Holger Stenger.

Detects the tempo of a track and which of Tango, Vals, Milonga or Swing it is -
or none of those - from the audio alone, without reading the genre tag.

The two answers are linked. The tempo a dancer taps is not a property of the
audio by itself: a tango is tapped on the beat, a vals once per 3/4 bar, a
milonga once per 2/4 bar. So the rhythm is settled first and the tempo reported
on the level that rhythm implies. Measured against 3,664 hand-tapped tracks the
estimate lands within 2 BPM of the tap 89.1% of the time, which is about as
close as the same person tapping the same track twice; rhythm classification is
93% accurate. [docs/tango-analysis.md](docs/tango-analysis.md) has the method
and the full numbers.

Building
--------

    .\scripts\build_release.ps1

Visual Studio with the C++ workload, and CMake, are the only prerequisites. The
script fetches the foobar2000 SDK and WTL into `external\` on first run, builds
both architectures, runs the tests and writes

    dist\foo_bpm-<version>.fb2k-component
      foo_bpm.dll        32 bit, foobar2000 1.x and 2.x (x86)
      x64/foo_bpm.dll    64 bit, foobar2000 2.x (x64)

foobar2000 ignores subfolders it does not understand, so that single file
installs everywhere. Symbols are packaged separately as
`dist\foo_bpm-<version>-symbols.zip`; keep them so crash reports can be
resolved, but do not ship them.

To work on it in Visual Studio, configure once and open the generated solution:

    cmake -S . -B build\x64 -A x64
    cmake --build build\x64 --config Release
    ctest --test-dir build\x64 -C Release

### Layout

* `bpmcore/` is the analysis, and has no host in it - no foobar2000, no pfc, no
  ATL, no `windows.h`. Only the standard library and KISS FFT, so the same
  sources build for a command line tool, a macOS host or an ARM target. Start at
  `bpmcore/bpmcore.h`.
* `foo_bpm/` is the foobar2000 component: decoding, tag writing, dialogs and
  preferences. It hands `bpmcore` mono PCM and gets a tempo and a rhythm back.
* `bpmcore_test/` verifies the analysis without foobar2000 running, and can
  benchmark and profile it.
* `scripts/analysis/` is the Python reference implementation and the training
  pipeline that generates `bpmcore/rhythm_model.h`. See its README.

### Settings

The preferences page is unchanged. Three entries live under **Preferences >
Advanced > Tools > BPM Analyser**:

* *Use the legacy BPM engine* - the original 2009 algorithm. The preferences
  page's STFT and candidate-selection controls only apply to it.
* *Write the detected rhythm to a tag* and *Rhythm tag name* - default `RHYTHM`.

### How the build hangs together

* `scripts\get_sdk.ps1` downloads the SDK and WTL, checks both against a pinned
  SHA256, and unpacks them into `external\`. CMake runs it by itself when they
  are missing, so a fresh checkout needs no manual setup. WTL is a separate
  download because the SDK's helpers include `<atlapp.h>` but do not ship it;
  ATL itself comes with Visual Studio.
* `cmake\fb2k_sdk.cmake` builds the SDK from source as four static libraries -
  pfc, the SDK proper, libPPUI and helpers - behind the `fb2k::sdk` target.
  This component needs the whole stack rather than the SDK core alone, because
  it has dialogs, a preferences page and a preferences-backed tag writer.
* `kiss_fft` is built with `kiss_fft_scalar=double` as a PUBLIC define, so the
  library and the code including its headers cannot disagree about the layout
  of `kiss_fft_cpx`.
* `kiss_fft_test` verifies the half-complex packing that `bpm_fft_impl_kissfft`
  depends on, and is wired into CTest.
* `bpmcore_test` checks that the decision trees compiled into
  `bpmcore/rhythm_model.h` still reproduce the classifier they were exported
  from, and is wired into CTest too. It also runs the analysis over raw PCM:

      bpmcore_test pipeline track.f32 44100      # tempo and rhythm
      bpmcore_test bench    track.f32 44100 5    # timing
      bpmcore_test profile  track.f32 44100 5    # timing per stage

`build\`, `external\` and `dist\` are all ignored by git.

Using the analysis elsewhere
----------------------------

`bpmcore` is a static library with one public header. Its only dependency is
KISS FFT, built with `kiss_fft_scalar=double`; there is no foobar2000, Windows
or ATL in it.

```cpp
#include <bpmcore/bpmcore.h>

bpmcore::collector c(sample_rate);
while (decode(...)) c.add_interleaved(buffer, frames, channels);

const bpmcore::analysis a = c.finish();
// a.bpm, a.rhythm, a.confidence, a.beat_bpm, a.meter
```

`analyse()` takes mono PCM directly if the caller already has it. Both float and
double samples are accepted. Pass a `bpmcore::listener` for progress and
cancellation, and a `bpmcore::options` to control threading - the spectral stage
is over 90% of the run time and is spread across cores by default, with an
answer that does not depend on the thread count.

References
----------

1. [Tempo and Beat Estimation of Musical Signals](http://ismir2004.ismir.net/proceedings/p032-page-158-paper191.pdf)
2. [Onset Detection Revisited](http://www.dafx.ca/proceedings/papers/p_133.pdf)
3. [A Comparison of Sound Onset Detection Algorithms with Emphasis on Psychoacoustically Motivated Detection Functions](http://www.cogs.susx.ac.uk/users/nc81/research/comparison.pdf)
4. [Window Functions](http://en.wikipedia.org/wiki/Window_function#Window_examples)

Links
-----

* [foobar2000 home page](http://www.foobar2000.org/)
* [BPM Analyser](http://www.hydrogenaudio.org/forums/index.php?showtopic=77142) on the foobar2000 forum
* [KISS FFT](http://sourceforge.net/projects/kissfft/)
