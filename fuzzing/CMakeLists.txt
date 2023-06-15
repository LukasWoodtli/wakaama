if(FUZZING)

include(../wakaama.cmake)

if (CMAKE_C_COMPILER_ID STREQUAL "Clang")
    add_executable(fuzzing_test)
    target_sources(fuzzing_test PRIVATE fuzz_lwm2m_handle_packet.c)
    target_compile_options(fuzzing_test PRIVATE -g -O1 -fsanitize=fuzzer,address)

    target_compile_definitions(fuzzing_test PRIVATE LWM2M_SERVER_MODE)

    target_link_options(fuzzing_test PRIVATE -fsanitize=fuzzer,address)

    target_sources_wakaama(fuzzing_test)
    target_sources_shared(fuzzing_test)

    add_custom_target(run_fuzzing_test
            COMMAND ${CMAKE_CURRENT_BINARY_DIR}/fuzzing_test -timeout=360 -max_total_time=30
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})

    add_dependencies(run_fuzzing_test fuzzing_test)
else ()
    message(FATAL_ERROR "Fuzzing is only available with clang")
endif ()

endif ()