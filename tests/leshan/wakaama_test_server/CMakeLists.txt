cmake_minimum_required(VERSION 3.13)

project(wakaama_test_server C)

include(../../../wakaama.cmake)

set(TARGET_NAME wakaama_test_server)

add_executable(${TARGET_NAME} wakaama_test_server.c)
target_compile_definitions(${TARGET_NAME} PRIVATE LWM2M_SERVER_MODE)
target_sources_wakaama(${TARGET_NAME})
target_sources_shared(${TARGET_NAME})

target_compile_options(${TARGET_NAME} PRIVATE --coverage)
target_link_options(${TARGET_NAME} PRIVATE --coverage)


# Run test server with leshan client
set(WAKAAMA_TEST_SERVER_EXECUTABLE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}")

set(RUN_TESTS_TARGET run_leshan_test)
configure_file(${CMAKE_SOURCE_DIR}/tests/leshan/run_leshan_client_tests_template.txt
    ${CMAKE_CURRENT_BINARY_DIR}/${RUN_TESTS_TARGET}.sh
    USE_SOURCE_PERMISSIONS
    @ONLY)

add_custom_target(${RUN_TESTS_TARGET}
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${RUN_TESTS_TARGET}.sh
    DEPENDS ${TARGET_NAME}
    USES_TERMINAL
    COMMENT "Running tests: ${TARGET_NAME}")
