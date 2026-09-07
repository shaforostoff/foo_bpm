# KISS FFT, built for double precision to match the analyser's maths.
#
# kiss_fft_scalar has to agree everywhere the headers are included, so it is
# PUBLIC: bpmcore, the only consumer that includes them, sees the same
# kiss_fft_cpx layout this library was compiled with.

add_library(kiss_fft STATIC
    src/kiss_fft.c
    src/kiss_fftr.c)

# Consumers say <kiss_fft/kiss_fft.h>; the library's own sources include their
# siblings unqualified, so they need the inner directory as well.
target_include_directories(kiss_fft
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/kiss_fft")
target_compile_definitions(kiss_fft PUBLIC kiss_fft_scalar=double)

if(MSVC)
    # Third party C; do not let its warnings drown out ours.
    target_compile_options(kiss_fft PRIVATE /W3 /wd4996 /wd4267)
endif()

set_target_properties(kiss_fft PROPERTIES FOLDER "Third party")
