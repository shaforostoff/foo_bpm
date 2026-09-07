# Verifies kiss_fft against stored reference spectra. It used to exist for the
# half-complex packing that the legacy engine's FFT wrapper performed; that
# wrapper is gone, and bpmcore reads kiss_fftr's bins directly rather than a
# packed buffer, so what is left is a guard on the vendored library - which
# bpmcore does depend on, through kiss_fftr.
# The data files stay next to the sources, so their directory is passed in
# rather than the test depending on the working directory.

add_executable(kiss_fft_test main.cpp)
target_link_libraries(kiss_fft_test PRIVATE kiss_fft)

if(MSVC)
    target_compile_options(kiss_fft_test PRIVATE /W4)
endif()

add_test(NAME fft_packing
         COMMAND kiss_fft_test "${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(kiss_fft_test PROPERTIES FOLDER "Tests")
