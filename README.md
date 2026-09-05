BPM Analyser for foobar2000
===========================

Originally written by Michael Balzer.

Bug fixes and refactoring by Holger Stenger.

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

`build\`, `external\` and `dist\` are all ignored by git.

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
