#pragma once

// Precompiled header. foobar2000+atl.h is the SDK's umbrella: it pulls in
// pfc, the SDK proper, ATL and WTL, and sets the Windows version macros, so
// nothing here should define _WIN32_WINNT or STRICT by hand - the build does
// that (see FOO_BPM_WIN32_WINNT in CMakeLists.txt).

#include <helpers/foobar2000+atl.h>
#include <helpers/atl-misc.h>
#include <helpers/input_helpers.h>
#include <libPPUI/listview_helper.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
