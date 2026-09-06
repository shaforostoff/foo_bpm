# The analysis, with no host in it.
#
# Nothing here includes foobar2000, pfc, ATL or windows.h - only the standard
# library and KISS FFT - so the same sources build for a command line tool, a
# macOS host or an ARM target. The foobar2000 component is a shell around this.

add_library(bpmcore STATIC
    analyse.cpp
    odf.cpp
    resample.cpp
    rhythm.cpp
    tempo.cpp

    bpmcore.h
    internal.h
    rhythm_model.h)

# Consumers say #include <bpmcore/bpmcore.h>.
target_include_directories(bpmcore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_link_libraries(bpmcore PRIVATE kiss_fft)

target_compile_features(bpmcore PUBLIC cxx_std_11)

if(MSVC)
    target_compile_options(bpmcore PRIVATE /W4)
    # windows.h is not included here, but a consumer's precompiled header may
    # have dragged in its min/max macros before this target's headers.
    target_compile_definitions(bpmcore PUBLIC NOMINMAX)
endif()

set_target_properties(bpmcore PROPERTIES FOLDER "bpmcore")
