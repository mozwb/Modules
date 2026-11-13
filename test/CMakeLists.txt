find_package(Threads REQUIRED)

add_executable(network_smoke network_smoke.cpp
    )
target_link_libraries(network_smoke PRIVATE Network Threads::Threads)

add_test(NAME Network.Smoke COMMAND network_smoke)
set_tests_properties(Network.Smoke PROPERTIES TIMEOUT 15)



add_executable(json json_test.cpp)
target_link_libraries(json PRIVATE IO Json)
add_test(NAME Json.Parser COMMAND json)
if(WIN32)
    get_filename_component(_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set_tests_properties(Json.Parser PROPERTIES
        ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Memory>\;${_compiler_bin}\;$ENV{PATH}")
endif()
