BPM Analyser for foobar2000
===========================

Originally written by Michael Balzer.

Bug fixes and refactoring by Holger Stenger.

Building
--------

    .\scripts\build_release.ps1

Visual Studio with the C++ workload is the only prerequisite. The script pulls
the foobar2000 SDK and WTL into `external\` on first run, builds Release|Win32
and writes `dist\foo_dsp_bpm.fb2k-component`.

The component is 32 bit and targets the 2011-03-11 SDK, so it loads in
foobar2000 1.x and in the 32 bit builds of 2.x.

### How the build hangs together

* `scripts\get_sdk.ps1` downloads the SDK and WTL, checks both against a pinned
  SHA256 and unpacks them into `external\`. `build_release.ps1` runs it by
  itself when they are missing, so a fresh checkout needs no manual setup.
* The 2011 SDK predates C++11 and today's Windows SDK, so it needs help in four
  places. Two are patches applied to the SDK as it is unpacked - both are listed
  in `get_sdk.ps1` with the reason they exist, and each refuses to apply unless
  it matches the pinned SDK exactly. The other two are shim headers in
  `scripts\compat\` (`tmschema.h`, dropped from the Windows SDK after Windows 7,
  and `afxres.h`, which ships only with the optional MFC component), so the
  resource script and the SDK sources compile unmodified.
* `scripts\external.props` is imported into every project of the build to put
  WTL and those shims on the include path.
* The project files pin the Visual Studio 2010 toolset, which no current Visual
  Studio can install, so the build overrides it with the newest one present.

`external\`, `dist\` and the build output are all ignored by git.

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
