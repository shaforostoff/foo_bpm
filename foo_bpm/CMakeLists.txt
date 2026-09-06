# The component itself.
#
# foobar2000 loads components by filename, and foo_bpm.cpp asserts its own with
# VALIDATE_COMPONENT_FILENAME, so the DLL is named foo_bpm.dll on every
# architecture. The .fb2k-component archive that scripts\build_release.ps1
# assembles puts the x64 build in an x64\ subfolder, which is how foobar2000
# tells the two apart.

add_library(foo_bpm MODULE
    bpm_analysis.cpp
    bpm_auto_analysis.cpp
    bpm_auto_analysis_thread.cpp
    bpm_contextmenu_item.cpp
    bpm_fft.cpp
    bpm_fft_impl_kissfft.cpp
    bpm_manual_dialog.cpp
    bpm_preferences_page.cpp
    bpm_result_dialog.cpp
    file_info_filter_bpm.cpp
    file_info_filter_scale_bpm.cpp
    foo_bpm.cpp
    format_bpm.cpp
    preferences.cpp
    foo_bpm.rc

    bpm_analysis.h
    bpm_auto_analysis.h
    bpm_auto_analysis_thread.h
    bpm_contextmenu_item.h
    bpm_fft.h
    bpm_fft_impl_kissfft.h
    bpm_manual_dialog.h
    bpm_preferences_page.h
    bpm_result_dialog.h
    bpm_math.h
    file_info_filter_bpm.h
    file_info_filter_scale_bpm.h
    format_bpm.h
    globals.h
    guid.h
    preferences.h
    resource.h
    stdafx.h)

set_target_properties(foo_bpm PROPERTIES
    OUTPUT_NAME "foo_bpm"
    PREFIX ""
    SUFFIX ".dll")

target_link_libraries(foo_bpm PRIVATE fb2k::sdk kiss_fft bpmcore)

# windows.h defines min and max as macros, which breaks every std::min/std::max
# in the analysis code. Neither the SDK nor WTL relies on the macros, so they
# are simply turned off for this target.
target_compile_definitions(foo_bpm PRIVATE NOMINMAX)

# stdafx.h is the precompiled header: it pulls in the SDK, ATL and WTL, which
# dwarf this component's own sources.
target_precompile_headers(foo_bpm PRIVATE stdafx.h)

if(MSVC)
    # /wd4100: ATL's message crackers and the SDK's callback interfaces fix the
    # parameter list of every handler, so unused parameters are the norm here
    # and flagging them would bury the warnings worth reading.
    target_compile_options(foo_bpm PRIVATE /W4 /wd4100)
endif()

set_target_properties(foo_bpm PROPERTIES FOLDER "foo_bpm")
