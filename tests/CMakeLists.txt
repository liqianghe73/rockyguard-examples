add_executable(api_canary api_canary.cpp)
target_link_libraries(api_canary PRIVATE RockyGuard::RockyGuard)
add_test(NAME api_canary COMMAND api_canary)

# Geometry tests for the extruder. Needs QtCore only -- no GUI, no SDK, no display,
# which is the payoff for keeping core/ free of both. Runs on fork PRs that cannot
# see the licence secret.
if(TARGET Qt6::Core)
    add_executable(mesh_tests
        mesh_tests.cpp
        ../examples/02-qt-cad/core/mesh.cpp
        ../examples/02-qt-cad/core/document.cpp)
    target_link_libraries(mesh_tests PRIVATE Qt6::Core)
    add_test(NAME mesh_tests COMMAND mesh_tests)
    set_tests_properties(mesh_tests PROPERTIES TIMEOUT 30)
endif()
