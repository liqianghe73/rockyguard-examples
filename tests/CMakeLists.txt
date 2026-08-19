add_executable(api_canary api_canary.cpp)
target_link_libraries(api_canary PRIVATE RockyGuard::RockyGuard)
add_test(NAME api_canary COMMAND api_canary)
