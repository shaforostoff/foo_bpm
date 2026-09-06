# Verification and benchmark harness for the analysis.
#
# It links bpmcore alone, so the test runs without foobar2000 installed - which
# is only possible because the analysis carries no host dependencies.

add_executable(bpmcore_test main.cpp)

target_link_libraries(bpmcore_test PRIVATE bpmcore kiss_fft)

if(MSVC)
    target_compile_options(bpmcore_test PRIVATE /W4)
endif()

# Checks the exported decision trees still reproduce the classifier they were
# generated from. Regenerated alongside the model by scripts/train_rhythm_model.py.
add_test(NAME rhythm_model
         COMMAND bpmcore_test model "${CMAKE_CURRENT_SOURCE_DIR}/reference_cases.tsv")

set_target_properties(bpmcore_test PROPERTIES FOLDER "Tests")
