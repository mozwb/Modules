find_package(Threads REQUIRED)

add_executable(network_smoke network_smoke.cpp)
target_link_libraries(network_smoke PRIVATE Network Threads::Threads)

add_test(NAME Network.Smoke COMMAND network_smoke)
set_tests_properties(Network.Smoke PROPERTIES TIMEOUT 15)
