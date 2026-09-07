# Draws foo_rubato's dialog templates and checks their labels fit.
#
# It reads the built DLL as a data file rather than linking anything, so it
# needs neither foobar2000 nor the SDK - a dialog template is just a resource,
# and the dialog manager will build one from any process. That is the whole
# reason this is worth having: the component's own dialogs are otherwise
# unopenable without foobar2000 running, and two layout faults reached a
# release because they were never drawn.

add_executable(dialog_test main.cpp)

target_link_libraries(dialog_test PRIVATE comctl32 gdiplus gdi32 user32)

if(MSVC)
    target_compile_options(dialog_test PRIVATE /W4)
    # Common Controls 6, so the dialogs are drawn with the control set a
    # current foobar2000 would show rather than the classic one.
    set_target_properties(dialog_test PROPERTIES LINK_FLAGS
        "/MANIFESTUAC:NO /MANIFESTDEPENDENCY:\"type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
endif()

# It reads foo_rubato.dll, so that has to exist first. CTest does not build
# anything itself, and a plain `cmake --build` gives no ordering between two
# targets that share no library.
add_dependencies(dialog_test foo_rubato)

# No images by default: the check is the part that can fail on its own, and a
# test that writes files into the build tree every run earns nothing. Pass
# --out to look at them - scripts\render_dialogs.ps1 does.
add_test(NAME dialog_labels COMMAND dialog_test "$<TARGET_FILE:foo_rubato>")

# A build with no desktop - a service, a container, a session with no window
# station - cannot draw a dialog and cannot answer the question either way.
# Saying so beats a red test that means nothing.
set_tests_properties(dialog_labels PROPERTIES SKIP_RETURN_CODE 77)

set_target_properties(dialog_test PROPERTIES FOLDER "Tests")
