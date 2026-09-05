# Verifies the half-complex packing that bpm_fft_impl_kissfft relies on.
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
