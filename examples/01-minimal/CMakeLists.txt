add_executable(01-minimal main.cpp)
target_link_libraries(01-minimal PRIVATE RockyGuard::RockyGuard)

# The most common Qt/Windows first-run failure is a SUCCESSFUL build whose exe
# cannot find Qt6Widgets.dll. This example has no Qt, but the shared-library SDK
# variant has the same shape, so copy any SDK DLLs next to the binary if they
# exist. Non-fatal, and it only ever copies within the build tree.
if(WIN32 AND NOT ROCKYGUARD_IS_STUB)
    file(GLOB _rg_dlls "${ROCKYGUARD_ROOT}/lib/shared/*.dll")
    if(_rg_dlls)
        add_custom_command(TARGET 01-minimal POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_rg_dlls}
                    "$<TARGET_FILE_DIR:01-minimal>"
            COMMENT "Copying RockyGuard shared libraries next to 01-minimal")
    endif()
endif()

# Runs with no license present. Asserts the unlicensed path is graceful: exit 0,
# and the Draft tier reported rather than a crash or a lockout. This is the path
# most users hit first, so it is the one worth testing.
add_test(NAME minimal_unlicensed COMMAND 01-minimal "does-not-exist.lic")
set_tests_properties(minimal_unlicensed PROPERTIES
    PASS_REGULAR_EXPRESSION "tier +: Draft \\(2D only\\)")
