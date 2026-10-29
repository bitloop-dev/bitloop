find_package(Catch2 3 CONFIG REQUIRED)
find_package(PkgConfig)

include(Catch)
include(CTest)

message(STATUS "Loading tests...")

if(TARGET bitloop::webgpu)
    add_subdirectory(webgpu)
endif()

file(GLOB UTILITY_TESTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/utility/*.cpp"
)

add_executable(bitloop_tests
    ${UTILITY_TESTS}
)

target_link_libraries(bitloop_tests PRIVATE
    bitloop
    Catch2::Catch2WithMain
)
target_compile_features(bitloop_tests PRIVATE cxx_std_23)

catch_discover_tests(bitloop_tests
    REPORTER console
    EXTRA_ARGS -s
)
set_property(TARGET bitloop_tests PROPERTY
    VS_DEBUGGER_COMMAND_ARGUMENTS "--reporter console --success"
)

# ===== Compile tests under different conditions (e.g. with gcem support vs without) =====

file(GLOB COMPILE_TESTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/compile/*.cpp"
)

add_library(bitloop_compile_tests OBJECT
    ${COMPILE_TESTS}
)
target_link_libraries(bitloop_compile_tests PRIVATE
    bitloop
)
target_compile_features(bitloop_compile_tests PRIVATE cxx_std_23)

set(config_arg "")
if (CMAKE_CONFIGURATION_TYPES)
    set(config_arg --config $<CONFIG>)
endif()

add_test(
    NAME bitloop_compile_tests
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target bitloop_compile_tests ${config_arg}
)

if (TARGET reconstruct_full_frame)
    add_dependencies(bitloop_tests reconstruct_full_frame)
    add_test(
        NAME bitloop_trace_reconstruct_sparse
        COMMAND ${CMAKE_COMMAND}
            "-DRECONSTRUCT_EXE=$<TARGET_FILE:reconstruct_full_frame>"
            "-DTRACE_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/trace_reconstruct_sparse"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/trace_reconstruct_sparse_test.cmake"
    )
endif()
