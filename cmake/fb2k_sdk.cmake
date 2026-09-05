# ---------------------------------------------------------------------------
# fb2k_sdk.cmake
#
# Builds the foobar2000 SDK as static libraries and exposes them through the
# interface target `fb2k::sdk`.
#
# Unlike a DSP-only component, foo_bpm puts up dialogs and a preferences page,
# so it needs the whole stack: pfc, the SDK proper, helpers/ (input_helper,
# preferences_page_impl, uSetDlgItemText) and libPPUI (listview_helper). The
# last two pull in ATL, which comes with Visual Studio, and WTL, which
# scripts\get_sdk.ps1 fetches alongside the SDK.
# ---------------------------------------------------------------------------

if(NOT EXISTS "${FB2K_SDK_DIR}/foobar2000/SDK/foobar2000.h")
    message(FATAL_ERROR "FB2K_SDK_DIR does not look like a foobar2000 SDK: ${FB2K_SDK_DIR}")
endif()
if(NOT EXISTS "${FB2K_WTL_DIR}/Include/atlapp.h")
    message(FATAL_ERROR "FB2K_WTL_DIR does not look like a WTL tree: ${FB2K_WTL_DIR}")
endif()

# --- settings every SDK library and the component itself share --------------
add_library(fb2k_common INTERFACE)
# Both roots, matching the stock .vcxproj files: SDK sources include <pfc/...>
# and <libPPUI/...> from the top, and <SDK/...> and <helpers/...> from
# foobar2000/. SYSTEM keeps third party warnings out of our build log.
target_include_directories(fb2k_common SYSTEM INTERFACE
    "${FB2K_SDK_DIR}"
    "${FB2K_SDK_DIR}/foobar2000"
    "${FB2K_WTL_DIR}/Include")
# WIN32_LEAN_AND_MEAN is deliberately NOT set: pfc/timers.h calls timeGetTime,
# which only appears once windows.h has pulled in mmsystem.h.
# NOMINMAX is deliberately NOT set: libPPUI calls min() and max() expecting
# windows.h to have defined them. The cost is that our own code has to write
# (std::min)(a, b) to keep the macro from eating the call.
target_compile_definitions(fb2k_common INTERFACE
    UNICODE _UNICODE
    _CRT_SECURE_NO_WARNINGS
    _WIN32_WINNT=${FOO_BPM_WIN32_WINNT}
    WINVER=${FOO_BPM_WIN32_WINNT})

# --- pfc -------------------------------------------------------------------
file(GLOB FB2K_PFC_SOURCES CONFIGURE_DEPENDS "${FB2K_SDK_DIR}/pfc/*.cpp")
# POSIX-only translation unit; win-objects.cpp is its counterpart.
list(FILTER FB2K_PFC_SOURCES EXCLUDE REGEX "synchro_nix\\.cpp$")
add_library(fb2k_pfc STATIC ${FB2K_PFC_SOURCES})
target_link_libraries(fb2k_pfc PUBLIC fb2k_common winmm)

# --- SDK + component client ------------------------------------------------
file(GLOB FB2K_SDK_SOURCES CONFIGURE_DEPENDS "${FB2K_SDK_DIR}/foobar2000/SDK/*.cpp")
list(APPEND FB2K_SDK_SOURCES
     "${FB2K_SDK_DIR}/foobar2000/foobar2000_component_client/component_client.cpp")
add_library(fb2k_sdk_core STATIC ${FB2K_SDK_SOURCES})
target_link_libraries(fb2k_sdk_core PUBLIC fb2k_pfc)

# --- libPPUI ---------------------------------------------------------------
file(GLOB FB2K_PPUI_SOURCES CONFIGURE_DEPENDS "${FB2K_SDK_DIR}/libPPUI/*.cpp")
add_library(fb2k_ppui STATIC ${FB2K_PPUI_SOURCES})
target_link_libraries(fb2k_ppui PUBLIC fb2k_pfc gdiplus uxtheme comctl32 shlwapi)

# --- helpers ---------------------------------------------------------------
file(GLOB FB2K_HELPER_SOURCES CONFIGURE_DEPENDS "${FB2K_SDK_DIR}/foobar2000/helpers/*.cpp")
add_library(fb2k_helpers STATIC ${FB2K_HELPER_SOURCES})
target_link_libraries(fb2k_helpers PUBLIC fb2k_sdk_core fb2k_ppui)

# --- shared.dll import library ---------------------------------------------
# shared.dll ships with foobar2000 itself; the SDK only carries import libs.
if(FOO_BPM_TARGET_ARCH STREQUAL "x64")
    set(_fb2k_shared_lib "${FB2K_SDK_DIR}/foobar2000/shared/shared-x64.lib")
elseif(FOO_BPM_TARGET_ARCH STREQUAL "ARM64EC")
    set(_fb2k_shared_lib "${FB2K_SDK_DIR}/foobar2000/shared/shared-ARM64EC.lib")
else()
    set(_fb2k_shared_lib "${FB2K_SDK_DIR}/foobar2000/shared/shared-Win32.lib")
endif()
if(NOT EXISTS "${_fb2k_shared_lib}")
    message(FATAL_ERROR "Missing foobar2000 import library: ${_fb2k_shared_lib}")
endif()
target_link_libraries(fb2k_sdk_core PUBLIC "${_fb2k_shared_lib}")

# --- umbrella --------------------------------------------------------------
add_library(fb2k_sdk INTERFACE)
target_link_libraries(fb2k_sdk INTERFACE fb2k_helpers)
add_library(fb2k::sdk ALIAS fb2k_sdk)

set_target_properties(fb2k_pfc fb2k_sdk_core fb2k_ppui fb2k_helpers
                      PROPERTIES FOLDER "foobar2000 SDK")

# The SDK is third party code; do not let its warnings drown out ours.
# /wd4996 covers the SDK deliberately calling its own deprecated APIs.
if(MSVC)
    foreach(_t fb2k_pfc fb2k_sdk_core fb2k_ppui fb2k_helpers)
        target_compile_options(${_t} PRIVATE /W3 /wd4996)
    endforeach()
endif()
